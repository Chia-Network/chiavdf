#!/usr/bin/env bash
# Fail unless a macOS release zip ships the GMP from scripts/build-macos-gmp.sh.
#
# That build links a __TEXT,__chiavdf_gmp section into libgmp and libgmpxx.
# Homebrew's bottle has no such section, so a bundle that silently fell back to
# Homebrew GMP fails here.
set -euo pipefail

# Keep in sync with GMP_CFLAGS in scripts/build-macos-gmp.sh.
RELEASE_CFLAGS="-O3 -DHGCD2_DIV2_METHOD=1"

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

fail=0
checked=0
while IFS= read -r dylib; do
  [ -n "$dylib" ] || continue
  checked=$((checked + 1))
  info="$tmp/build-info.txt"
  rm -f "$info"
  if ! segedit "$dylib" -extract __TEXT __chiavdf_gmp "$info" >/dev/null 2>&1 || [ ! -s "$info" ]; then
    echo "$dylib has no __TEXT,__chiavdf_gmp section (not the chiavdf GMP build)" >&2
    fail=1
    continue
  fi
  printf '== %s ==\n' "${dylib#"$tmp"/}"
  cat "$info"
  # Exactly the release flags: no -mcpu, no GMP_EXTRA_CFLAGS benchmarking builds.
  if ! grep -Fxq "CFLAGS=$RELEASE_CFLAGS" "$info"; then
    echo "$dylib was not built with CFLAGS=$RELEASE_CFLAGS" >&2
    fail=1
  fi
done < <(find "$tmp" -path '*/libexec/chiavdf/*' -type f \( -name 'libgmp.*.dylib' -o -name 'libgmpxx.*.dylib' \))

if [ "$checked" -lt 2 ]; then
  echo "Expected bundled libgmp and libgmpxx in $ZIP_PATH, found $checked" >&2
  exit 1
fi
if [ "$fail" -ne 0 ]; then
  exit 1
fi
echo "Verified $ZIP_PATH ships the HGCD2_DIV2_METHOD=1 GMP build"
