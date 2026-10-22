#!/usr/bin/env bash
# Hermetic tests for scripts/package-macos-pkg.sh.
#
# pkgbuild, productsign, and pkgutil only exist on macOS, so this test puts
# recording fakes first on PATH and asserts the arguments and payload checks
# the packaging script relies on. The real tools still run in the macOS CI cell.
set -euo pipefail

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
script="${root}/scripts/package-macos-pkg.sh"

# shellcheck source=../scripts/macos-bundle-tools.sh disable=SC1091
source "${root}/scripts/macos-bundle-tools.sh"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fakebin="${tmp}/fakebin"
log="${tmp}/calls.log"
payload="${tmp}/payload.txt"
signature="${tmp}/signature.txt"
mkdir -p "$fakebin"

cat > "${fakebin}/pkgbuild" << 'EOF'
#!/usr/bin/env bash
set -euo pipefail
out=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --root|--identifier|--version|--install-location|--ownership)
      printf '%s=%s\n' "${1#--}" "$2" >> "$PKG_LOG"
      shift 2
      ;;
    *)
      out=$1
      shift
      ;;
  esac
done
printf 'fake package\n' > "$out"
EOF

cat > "${fakebin}/productsign" << 'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'productsign=%s\n' "$*" >> "$PKG_LOG"
cp "$3" "$4"
EOF

cat > "${fakebin}/pkgutil" << 'EOF'
#!/usr/bin/env bash
set -euo pipefail
case "${1:-}" in
  --payload-files) cat "${PKG_PAYLOAD_FILE:?}" ;;
  --check-signature) cat "${PKG_SIGNATURE_FILE:?}" ;;
  *)
    echo "unexpected pkgutil arguments: $*" >&2
    exit 1
    ;;
esac
EOF
chmod +x "${fakebin}/pkgbuild" "${fakebin}/productsign" "${fakebin}/pkgutil"

write_payload() {
  local tool
  {
    echo .
    echo ./bin
    for tool in "${MACOS_BUNDLE_TOOLS[@]}"; do
      echo "./bin/$tool"
    done
    echo ./bin/._vdf_client
    echo ./libexec
    echo ./libexec/chiavdf
    echo ./libexec/chiavdf/._libft4222.dylib
    echo ./libexec/chiavdf/libft4222.dylib
    echo ./libexec/chiavdf/libgmp.10.dylib
  } > "$1"
}

write_payload "$payload"
cat > "$signature" << 'EOF'
Package "chiavdf.pkg":
   Status: signed by a developer certificate issued by Apple for distribution
   Certificate Chain:
    1. Developer ID Installer: Chia Network Inc. (ABCDE12345)
EOF

make_bundle() {
  local dest=$1 tool
  rm -rf "$dest"
  mkdir -p "$dest/bin" "$dest/libexec/chiavdf"
  for tool in "${MACOS_BUNDLE_TOOLS[@]}"; do
    printf '#!/bin/sh\nexit 0\n' > "$dest/bin/$tool"
    chmod +x "$dest/bin/$tool"
  done
  : > "$dest/libexec/chiavdf/libft4222.dylib"
  : > "$dest/libexec/chiavdf/libgmp.10.dylib"
}

run_pkg() {
  PKG_LOG="$log" \
    PKG_PAYLOAD_FILE="$payload" \
    PKG_SIGNATURE_FILE="$signature" \
    PATH="${fakebin}:${PATH}" \
    bash "$script" "$@"
}

bundle="${tmp}/bundle"
make_bundle "$bundle"

# Developer ID Installer: productsign runs and the numeric pkgbuild version
# drops the tag suffix.
: > "$log"
run_pkg "$bundle" "${tmp}/signed.pkg" "v1.2.3-rc1" "Developer ID Installer: Chia Network Inc. (ABCDE12345)"
[ -f "${tmp}/signed.pkg" ] || fail "signed package was not produced"
for expected in \
  "root=${bundle}" \
  "identifier=com.chia.chiavdf" \
  "version=1.2.3" \
  "install-location=/usr/local" \
  "ownership=recommended"; do
  grep -Fxq "$expected" "$log" || fail "pkgbuild did not receive ${expected}: $(cat "$log")"
done
grep -Fq 'productsign=--sign Developer ID Installer: Chia Network Inc. (ABCDE12345)' "$log" \
  || fail "productsign did not receive the installer identity: $(cat "$log")"

# No identity: the package stays unsigned and productsign never runs.
: > "$log"
run_pkg "$bundle" "${tmp}/unsigned.pkg" "1.2.3"
[ -f "${tmp}/unsigned.pkg" ] || fail "unsigned package was not produced"
if grep -q '^productsign=' "$log"; then
  fail "productsign ran without an installer identity: $(cat "$log")"
fi

# A non-numeric version still yields a valid pkgbuild version.
: > "$log"
run_pkg "$bundle" "${tmp}/fallback.pkg" "unknown-no-.git-directory"
grep -Fxq "version=0.0.1" "$log" || fail "fallback version was not numeric: $(cat "$log")"

# Identifier and install location stay overridable for other taps.
: > "$log"
PKG_IDENTIFIER=org.example.chiavdf PKG_INSTALL_LOCATION=/opt/chiavdf \
  run_pkg "$bundle" "${tmp}/override.pkg" "1.2.3"
grep -Fxq "identifier=org.example.chiavdf" "$log" || fail "identifier override was ignored"
grep -Fxq "install-location=/opt/chiavdf" "$log" || fail "install location override was ignored"

# The rpath depends on bin/ and libexec/chiavdf staying siblings.
broken="${tmp}/broken"
make_bundle "$broken"
rm -rf "${broken}/libexec"
if run_pkg "$broken" "${tmp}/broken.pkg" "1.2.3"; then
  fail "bundle without libexec/chiavdf was accepted"
fi

# Missing executables are rejected before pkgbuild runs.
missing="${tmp}/missing"
make_bundle "$missing"
rm -f "${missing}/bin/hw_test"
if run_pkg "$missing" "${tmp}/missing.pkg" "1.2.3"; then
  fail "bundle without a required executable was accepted"
fi

# A payload that puts files outside bin/ or libexec/ is rejected.
write_payload "${tmp}/bad-payload.txt"
printf './etc/chiavdf.conf\n' >> "${tmp}/bad-payload.txt"
payload="${tmp}/bad-payload.txt"
if run_pkg "$bundle" "${tmp}/bad-layout.pkg" "1.2.3"; then
  fail "payload with an unexpected top-level directory was accepted"
fi

# A top-level file outside bin/ or libexec/ is rejected too.
write_payload "${tmp}/top-level-file-payload.txt"
printf './README.txt\n' >> "${tmp}/top-level-file-payload.txt"
payload="${tmp}/top-level-file-payload.txt"
if run_pkg "$bundle" "${tmp}/top-level-file.pkg" "1.2.3"; then
  fail "payload with an unexpected top-level file was accepted"
fi

# Finder/AppleDouble metadata at the top level is not a payload entry.
write_payload "${tmp}/metadata-payload.txt"
printf './._bin\n./._README.txt\n' >> "${tmp}/metadata-payload.txt"
payload="${tmp}/metadata-payload.txt"
run_pkg "$bundle" "${tmp}/metadata.pkg" "1.2.3" \
  || fail "top-level AppleDouble metadata was rejected"

# A signed package must carry a Developer ID Installer signature.
payload="${tmp}/payload.txt"
write_payload "$payload"
printf 'Package "chiavdf.pkg":\n   Status: no signature\n' > "$signature"
if run_pkg "$bundle" "${tmp}/unsigned-signature.pkg" "1.2.3" "Developer ID Installer: Chia Network Inc. (ABCDE12345)"; then
  fail "package without a Developer ID Installer signature was accepted"
fi

echo "macOS pkg packaging checks passed"
