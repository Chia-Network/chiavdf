#!/usr/bin/env bash
# Fail unless a macOS release zip ships the GMP from scripts/build-macos-gmp.sh.
#
# That build links a __TEXT,__chiavdf_gmp section into libgmp and libgmpxx with
# the identity lines from scripts/macos-gmp-release.sh (version, tarball hash,
# flags, deployment target). Homebrew's bottle has no such section, so a bundle
# that silently fell back to Homebrew GMP fails here, as does a build from
# another tarball or with other flags. Each library must also be a two-level
# namespace dylib for the release deployment target.
set -euo pipefail

# shellcheck source=scripts/macos-gmp-release.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/macos-gmp-release.sh"

if [ "$#" -ne 1 ]; then
  echo "Usage: scripts/check-macos-gmp-build.sh <bundle.zip>" >&2
  exit 1
fi

ZIP_PATH=$1
if [ ! -f "$ZIP_PATH" ]; then
  echo "Missing zip: $ZIP_PATH" >&2
  exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
unzip -q "$ZIP_PATH" -d "$tmp"

identity="$tmp/identity.txt"
gmp_release_identity > "$identity"

fail=0
gmp_count=0
gmpxx_count=0
while IFS= read -r dylib; do
  [ -n "$dylib" ] || continue
  case "$(basename "$dylib")" in
    libgmpxx.*) gmpxx_count=$((gmpxx_count + 1)) ;;
    libgmp.*) gmp_count=$((gmp_count + 1)) ;;
  esac
  name="${dylib#"$tmp"/}"
  info="$tmp/build-info.txt"
  rm -f "$info"
  if ! segedit "$dylib" -extract __TEXT __chiavdf_gmp "$info" >/dev/null 2>&1 || [ ! -s "$info" ]; then
    echo "$name has no __TEXT,__chiavdf_gmp section (not the chiavdf GMP build)" >&2
    fail=1
    continue
  fi
  printf '== %s ==\n' "$name"
  cat "$info"
  while IFS= read -r line; do
    if ! grep -Fxq -- "$line" "$info"; then
      echo "$name does not match the release build: missing [$line]" >&2
      fail=1
    fi
  done < "$identity"
  # GMP 6.3.0's libtool falls back to -flat_namespace without the deployment
  # target; require the two-level namespace and the release minos.
  if ! otool -hv "$dylib" | grep -qw TWOLEVEL; then
    echo "$name is not a two-level namespace dylib" >&2
    fail=1
  fi
  minos="$(otool -l "$dylib" | awk '/cmd LC_BUILD_VERSION/ { found = 1 } found && $1 == "minos" { print $2; exit }')"
  if [ "$minos" != "$GMP_DEPLOYMENT_TARGET" ]; then
    echo "$name has minos [$minos], expected $GMP_DEPLOYMENT_TARGET" >&2
    fail=1
  fi
done < <(find "$tmp" -path '*/libexec/chiavdf/*' -type f \( -name 'libgmp.*.dylib' -o -name 'libgmpxx.*.dylib' \))

if [ "$gmp_count" -ne 1 ] || [ "$gmpxx_count" -ne 1 ]; then
  echo "Expected one bundled libgmp and one libgmpxx in $ZIP_PATH, found $gmp_count and $gmpxx_count" >&2
  exit 1
fi
if [ "$fail" -ne 0 ]; then
  exit 1
fi
echo "Verified $ZIP_PATH ships the $GMP_CFLAGS GMP $GMP_VERSION build"
