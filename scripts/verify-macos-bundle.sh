#!/usr/bin/env bash
# Fail unless a shipped macOS zip is relocatable and every shipped executable starts.
#
# Notarization inspects the pkg payload; it does not execute the binaries.
# Homebrew absolute paths survive codesign and are rejected later by the
# hardened runtime's library validation. This check unzips the artifact that
# would be published.
set -euo pipefail

# shellcheck source=scripts/macos-install-names.sh disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/macos-install-names.sh"
# shellcheck source=scripts/macos-bundle-tools.sh disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/macos-bundle-tools.sh"

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
need_cmd od

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
unzip -q "$ZIP_PATH" -d "$tmp"

# Skip an AppleDouble file only when it sits beside the real file or directory
# and starts with the AppleDouble magic. ditto writes that file for the bundle
# directory itself, next to the archive root. .DS_Store is Finder metadata.
is_skippable_metadata() {
  local path="$1"
  local base sibling magic
  base="$(basename "$path")"
  case "$base" in
    .DS_Store) return 0 ;;
    ._*) ;;
    *) return 1 ;;
  esac
  sibling="$(dirname "$path")/${base#._}"
  if [ ! -e "$sibling" ] && [ ! -L "$sibling" ]; then
    return 1
  fi
  magic="$(od -An -t x1 -N 4 "$path" 2>/dev/null | tr -s '[:space:]' ' ' | sed 's/^ //;s/ $//')" || return 1
  [ "$magic" = "00 05 16 07" ]
}

top=()
while IFS= read -r dir; do
  [ -n "$dir" ] || continue
  if [ "$(basename "$dir")" = "__MACOSX" ]; then
    continue
  fi
  top+=("$dir")
done < <(find "$tmp" -mindepth 1 -maxdepth 1 -type d)
if [ "${#top[@]}" -ne 1 ]; then
  echo "Expected one top-level directory in $ZIP_PATH, found ${#top[@]}" >&2
  exit 1
fi
root="${top[0]}"
stray_files=""
while IFS= read -r stray; do
  [ -n "$stray" ] || continue
  if is_skippable_metadata "$stray"; then
    continue
  fi
  stray_files="${stray_files}${stray}"$'\n'
done < <(find "$tmp" -mindepth 1 -maxdepth 1 -type f)
if [ -n "$stray_files" ]; then
  echo "Unexpected top-level file in $ZIP_PATH:" >&2
  printf '%s' "$stray_files" >&2
  exit 1
fi
libexec="$root/libexec/chiavdf"

if [ ! -d "$libexec" ]; then
  echo "Missing libexec/chiavdf in $ZIP_PATH" >&2
  exit 1
fi

for tool in "${MACOS_BUNDLE_TOOLS[@]}"; do
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

fail=0
checked=0
while IFS= read -r macho; do
  [ -n "$macho" ] || continue
  if is_skippable_metadata "$macho"; then
    continue
  fi
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
      if ! rpath_list="$(macos_rpaths "$macho")"; then
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

launch_bundled() {
  local bin="$1"
  shift
  local launch_out launch_status
  set +e
  launch_out="$(
    env -u DYLD_LIBRARY_PATH \
        -u DYLD_FALLBACK_LIBRARY_PATH \
        -u DYLD_FRAMEWORK_PATH \
        -u DYLD_FALLBACK_FRAMEWORK_PATH \
        -u DYLD_INSERT_LIBRARIES \
        -u DYLD_IMAGE_SUFFIX \
        "$bin" "$@" 2>&1
  )"
  launch_status=$?
  set -e
  printf '%s\n' "$launch_out"
  if [ "$launch_status" -ne 0 ]; then
    echo "$bin $* failed with status $launch_status" >&2
    exit 1
  fi
  if [ -z "$launch_out" ]; then
    echo "$bin $* produced no output" >&2
    exit 1
  fi
}

for tool in "${MACOS_BUNDLE_TOOLS[@]}"; do
  launch_bundled "$root/bin/$tool" --version
done

echo "Verified $ZIP_PATH: no Homebrew load paths, every shipped executable --version ok"
