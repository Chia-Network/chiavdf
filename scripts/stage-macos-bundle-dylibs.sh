#!/usr/bin/env bash
# Stage the dylibs that ship inside the macOS release zip.
#
# FTDI is already relocatable (@rpath ids from scripts/get-libft4222.sh).
# GMP is not: the Makefile links Homebrew's absolute install names, and the
# hardened runtime will not load those on a machine that does not have that
# Homebrew prefix. Both land in the same libexec directory, and every absolute
# non-system install name is rewritten to @rpath/<basename>. Executables also
# drop @executable_path/hw/libft4222 and gain @loader_path/../libexec/chiavdf
# in that same install_name_tool pass. The caller signs the resulting files once.
set -euo pipefail

# shellcheck source=scripts/macos-install-names.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/macos-install-names.sh"

usage() {
  echo "Usage: scripts/stage-macos-bundle-dylibs.sh BIN_DIR LIBEXEC_DIR [FTDI_DIR]" >&2
}

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  usage
  exit 1
fi

BIN_DIR=$1
LIBEXEC_DIR=$2
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FTDI_DIR="${3:-${ROOT_DIR}/src/hw/libft4222}"

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

need_cmd otool
need_cmd install_name_tool
need_cmd realpath

if [ ! -d "$BIN_DIR" ]; then
  echo "Missing bundle bin directory: $BIN_DIR" >&2
  exit 1
fi
if [ ! -d "$LIBEXEC_DIR" ]; then
  echo "Missing bundle libexec directory: $LIBEXEC_DIR" >&2
  exit 1
fi
if [ ! -d "$FTDI_DIR" ]; then
  echo "Missing FTDI directory: $FTDI_DIR" >&2
  exit 1
fi

bin_count=0
for f in "$BIN_DIR"/*; do
  if [ -f "$f" ] && [ ! -L "$f" ]; then
    bin_count=$((bin_count + 1))
  fi
done
if [ "$bin_count" -eq 0 ]; then
  echo "No binaries to stage in $BIN_DIR" >&2
  exit 1
fi

shopt -s nullglob
ftdi_dylibs=("$FTDI_DIR"/*.dylib)
if [ "${#ftdi_dylibs[@]}" -eq 0 ]; then
  echo "No FTDI dylibs found in $FTDI_DIR" >&2
  exit 1
fi

for src in "${ftdi_dylibs[@]}"; do
  base="$(basename "$src")"
  if [ -L "$src" ]; then
    target="$(readlink "$src")"
    case "$target" in
      /*)
        echo "Refusing absolute FTDI symlink: $src -> $target" >&2
        exit 1
        ;;
      */*)
        echo "Refusing nested FTDI symlink: $src -> $target" >&2
        exit 1
        ;;
    esac
    if [ ! -e "$FTDI_DIR/$target" ]; then
      echo "Broken FTDI symlink: $src -> $target" >&2
      exit 1
    fi
    ln -sfn "$target" "$LIBEXEC_DIR/$base"
  else
    cp -p "$src" "$LIBEXEC_DIR/$base"
    chmod u+w "$LIBEXEC_DIR/$base"
  fi
done

if [ ! -e "$LIBEXEC_DIR/libftd2xx.dylib" ] || [ ! -e "$LIBEXEC_DIR/libft4222.dylib" ]; then
  echo "FTDI staging did not produce libftd2xx.dylib and libft4222.dylib in $LIBEXEC_DIR" >&2
  exit 1
fi

queue="$(mktemp)"
rewrites="$(mktemp)"
cleanup() {
  rm -f "$queue" "$rewrites"
}
trap cleanup EXIT

enqueue_dir() {
  local dir="$1"
  local f
  for f in "$dir"/*; do
    if [ -f "$f" ] && [ ! -L "$f" ]; then
      printf '%s\n' "$f" >> "$queue"
    fi
  done
}

enqueue_dir "$BIN_DIR"
enqueue_dir "$LIBEXEC_DIR"

record_rewrite() {
  local old="$1"
  local new="$2"
  local line
  line="$(printf '%s\t%s' "$old" "$new")"
  if [ ! -s "$rewrites" ] || ! grep -Fxq "$line" "$rewrites"; then
    printf '%s\n' "$line" >> "$rewrites"
  fi
}

line_no=0
while true; do
  line_no=$((line_no + 1))
  bin="$(awk -v n="$line_no" 'NR == n { print; exit }' "$queue")" || {
    echo "Failed to read dylib work queue" >&2
    exit 1
  }
  if [ -z "$bin" ]; then
    break
  fi
  if [ ! -f "$bin" ]; then
    echo "Bundle entry disappeared while staging dylibs: $bin" >&2
    exit 1
  fi

  loads="$(otool -L "$bin" | awk 'NR > 1 { print $1 }')" || {
    echo "otool -L failed for $bin" >&2
    exit 1
  }
  self_id="$(otool -D "$bin" | awk 'NR == 2 { print $1 }')" || {
    echo "otool -D failed for $bin" >&2
    exit 1
  }

  while IFS= read -r dep; do
    [ -n "$dep" ] || continue
    if [ -n "$self_id" ] && [ "$dep" = "$self_id" ]; then
      continue
    fi
    case "$(macos_install_name_kind "$dep")" in
      system)
        continue
        ;;
      bundled)
        rel="$(macos_bundled_relpath "$dep")"
        case "$rel" in
          */*)
            echo "Refusing nested relative install name: $dep (from $bin)" >&2
            exit 1
            ;;
        esac
        if [ ! -e "$LIBEXEC_DIR/$rel" ]; then
          echo "Bundled dylib $rel is missing from $LIBEXEC_DIR (required by $bin)" >&2
          exit 1
        fi
        ;;
      absolute)
        if [ ! -e "$dep" ]; then
          echo "Dependency does not exist, refusing to ship: $dep (from $bin)" >&2
          exit 1
        fi
        base="$(basename "$dep")"
        case "$base" in
          ""|.*|*/*)
            echo "Refusing to bundle install name: $dep" >&2
            exit 1
            ;;
        esac
        dest="$LIBEXEC_DIR/$base"
        src="$(realpath "$dep")" || {
          echo "Could not resolve $dep" >&2
          exit 1
        }
        if [ -L "$dest" ]; then
          echo "Refusing to overwrite symlink $dest with $dep" >&2
          exit 1
        fi
        if [ -e "$dest" ]; then
          if ! cmp -s "$src" "$dest"; then
            echo "Bundled name collision for $base: $dep and $dest differ" >&2
            exit 1
          fi
        else
          cp -p "$src" "$dest"
          chmod u+w "$dest"
          printf '%s\n' "$dest" >> "$queue"
        fi
        record_rewrite "$dep" "@rpath/$base"
        ;;
      *)
        echo "Unrecognized install name, refusing to ship: $dep (from $bin)" >&2
        exit 1
        ;;
    esac
  done <<< "$loads"
done

rewrite_macho() {
  local macho="$1"
  local loads self_id old new
  loads="$(otool -L "$macho" | awk 'NR > 1 { print $1 }')" || {
    echo "otool -L failed for $macho" >&2
    exit 1
  }
  self_id="$(otool -D "$macho" | awk 'NR == 2 { print $1 }')" || {
    echo "otool -D failed for $macho" >&2
    exit 1
  }
  case "$(macos_install_name_kind "$self_id")" in
    empty|system|bundled) ;;
    absolute)
      install_name_tool -id "@rpath/$(basename "$self_id")" "$macho"
      ;;
    *)
      echo "Unrecognized dylib id on $macho: $self_id" >&2
      exit 1
      ;;
  esac

  if [ -s "$rewrites" ]; then
    while IFS=$'\t' read -r old new; do
      [ -n "${old:-}" ] || continue
      if [ -n "$self_id" ] && [ "$old" = "$self_id" ]; then
        continue
      fi
      if printf '%s\n' "$loads" | grep -Fxq "$old"; then
        install_name_tool -change "$old" "$new" "$macho"
      fi
    done < "$rewrites"
  fi

  # Executables only. This is the same install_name_tool pass as the id and
  # dependency edits above, not a later walk in the workflow.
  case "$macho" in
    "$BIN_DIR"/*) rewrite_executable_rpath "$macho" ;;
  esac
}

rewrite_executable_rpath() {
  local macho="$1"
  local rpaths
  rpaths="$(macos_rpaths "$macho")" || {
    echo "otool -l failed for $macho" >&2
    exit 1
  }
  if printf '%s\n' "$rpaths" | grep -Fxq "@executable_path/hw/libft4222"; then
    install_name_tool -delete_rpath "@executable_path/hw/libft4222" "$macho"
  fi
  if ! printf '%s\n' "$rpaths" | grep -Fxq "@loader_path/../libexec/chiavdf"; then
    install_name_tool -add_rpath "@loader_path/../libexec/chiavdf" "$macho"
  fi
}

for dir in "$BIN_DIR" "$LIBEXEC_DIR"; do
  for macho in "$dir"/*; do
    if [ -f "$macho" ] && [ ! -L "$macho" ]; then
      rewrite_macho "$macho"
    fi
  done
done

# Same download-attribute cleanup as scripts/get-libft4222.sh. Missing
# attributes make xattr exit non-zero; that is not a bundle failure.
if command -v xattr >/dev/null 2>&1; then
  xattr -dr com.apple.provenance "$LIBEXEC_DIR" || true
  xattr -dr com.apple.quarantine "$LIBEXEC_DIR" || true
fi
