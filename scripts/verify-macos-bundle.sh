#!/usr/bin/env bash
# Fail unless a shipped macOS zip is relocatable and vdf_client actually starts.
#
# Notarization submits the zip; it does not execute it. Homebrew absolute
# paths survive codesign and are rejected later by the hardened runtime's
# library validation. This check unzips the artifact that would be published.
set -euo pipefail

# shellcheck source=scripts/macos-install-names.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/macos-install-names.sh"

if [ "$#" -ne 1 ]; then
  echo "Usage: scripts/verify-macos-bundle.sh <bundle.zip>" >&2
  exit 1
fi

ZIP_PATH=$1
if [ ! -f "$ZIP_PATH" ]; then
  echo "Missing zip: $ZIP_PATH" >&2
  exit 1
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

need_cmd otool
need_cmd unzip
need_cmd find

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
unzip -q "$ZIP_PATH" -d "$tmp"

top=()
while IFS= read -r dir; do
  [ -n "$dir" ] || continue
  top+=("$dir")
done < <(find "$tmp" -mindepth 1 -maxdepth 1 -type d)
if [ "${#top[@]}" -ne 1 ]; then
  echo "Expected one top-level directory in $ZIP_PATH, found ${#top[@]}" >&2
  exit 1
fi
root="${top[0]}"
libexec="$root/libexec/chiavdf"

if [ ! -d "$libexec" ]; then
  echo "Missing libexec/chiavdf in $ZIP_PATH" >&2
  exit 1
fi

for tool in vdf_client vdf_bench hw_vdf_client emu_hw_vdf_client hw_test emu_hw_test; do
  if [ ! -x "$root/bin/$tool" ]; then
    echo "Missing executable bin/$tool in $ZIP_PATH" >&2
    exit 1
  fi
done

portable_dep() {
  case "$(macos_install_name_kind "$1")" in
    system|bundled) return 0 ;;
    *) return 1 ;;
  esac
}

rpaths_of() {
  otool -l "$1" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" { in_rpath = 1; next }
    in_rpath && $1 == "path" { print $2; in_rpath = 0 }
  '
}

fail=0
checked=0
while IFS= read -r macho; do
  [ -n "$macho" ] || continue
  checked=$((checked + 1))
  if ! loads="$(otool -L "$macho")"; then
    echo "otool -L failed for $macho" >&2
    exit 1
  fi
  if [ -z "$loads" ]; then
    echo "otool -L produced no output for $macho" >&2
    exit 1
  fi
  printf '== %s ==\n%s\n' "$macho" "$loads"
  if printf '%s\n' "$loads" | grep -Eq '/opt/homebrew|/usr/local'; then
    echo "Found /opt/homebrew or /usr/local in otool -L output for $macho" >&2
    fail=1
  fi

  while IFS= read -r dep; do
    [ -n "$dep" ] || continue
    if ! portable_dep "$dep"; then
      echo "Non-portable load path in $macho: $dep" >&2
      fail=1
    fi
    if [ "$(macos_install_name_kind "$dep")" = "bundled" ]; then
      rel="$(macos_bundled_relpath "$dep")"
      case "$rel" in
        */*)
          echo "Nested bundled install name in $macho: $dep" >&2
          fail=1
          ;;
        *)
          if [ ! -e "$libexec/$rel" ]; then
            echo "Missing bundled library $libexec/$rel required by $macho" >&2
            fail=1
          fi
          ;;
      esac
    fi
  done < <(printf '%s\n' "$loads" | awk 'NR > 1 { print $1 }')

  case "$macho" in
    "$root/bin/"*)
      if ! rpath_list="$(rpaths_of "$macho")"; then
        echo "otool -l failed for $macho" >&2
        exit 1
      fi
      if ! printf '%s\n' "$rpath_list" | grep -Fxq "@loader_path/../libexec/chiavdf"; then
        echo "Missing @loader_path/../libexec/chiavdf rpath on $macho" >&2
        fail=1
      fi
      if printf '%s\n' "$rpath_list" | grep -Eq '^/|/opt/homebrew|/usr/local'; then
        echo "Non-portable rpath on $macho" >&2
        fail=1
      fi
      ;;
  esac
done < <(find "$root" -type f)

if [ "$checked" -eq 0 ]; then
  echo "No files found to verify in $ZIP_PATH" >&2
  exit 1
fi
if [ "$fail" -ne 0 ]; then
  exit 1
fi

client="$root/bin/vdf_client"
gmp_dep="$(otool -L "$client" | awk 'NR > 1 { print $1 }' | grep -E '^@rpath/libgmp\.[[:digit:]]+\.dylib$' || true)"
gmpxx_dep="$(otool -L "$client" | awk 'NR > 1 { print $1 }' | grep -E '^@rpath/libgmpxx\.[[:digit:]]+\.dylib$' || true)"
if [ -z "$gmp_dep" ] || [ -z "$gmpxx_dep" ]; then
  echo "vdf_client is not linked to bundled libgmp and libgmpxx (@rpath)" >&2
  exit 1
fi
if ! otool -L "$root/bin/hw_vdf_client" | awk 'NR > 1 { print $1 }' | grep -Fxq "@rpath/libft4222.dylib"; then
  echo "hw_vdf_client lost its bundled FTDI dependency (@rpath/libft4222.dylib)" >&2
  exit 1
fi

set +e
launch_out="$(
  env -u DYLD_LIBRARY_PATH \
      -u DYLD_FALLBACK_LIBRARY_PATH \
      -u DYLD_FRAMEWORK_PATH \
      -u DYLD_FALLBACK_FRAMEWORK_PATH \
      -u DYLD_INSERT_LIBRARIES \
      -u DYLD_IMAGE_SUFFIX \
      "$client" --version 2>&1
)"
launch_status=$?
set -e
printf '%s\n' "$launch_out"
if [ "$launch_status" -ne 0 ]; then
  echo "vdf_client --version failed with status $launch_status" >&2
  exit 1
fi
if [ -z "$launch_out" ]; then
  echo "vdf_client --version produced no output" >&2
  exit 1
fi

echo "Verified $ZIP_PATH: no Homebrew load paths, vdf_client --version ok"
