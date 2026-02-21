#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${ROOT_DIR}/src"
WORK_DIR="${SRC_DIR}/libft4222"
HW_DIR="${SRC_DIR}/hw/libft4222"

# Upstream FT4222 driver downloads can be found at https://ftdichip.com/products/ft4222h/
# on the Downloads tab in the middle of the page as of 2026-02-05.
# There is an updated Linux version available(v1.4.4.233) also as of 2026-02-05.
# We currently fetch known-good versions mirrored at download.chia.net.

LINUX_URL="https://download.chia.net/vdf/libft4222-linux-1.4.4.170.tgz"
LINUX_ARCHIVE="${WORK_DIR}/libft4222-linux-1.4.4.170.tgz"
MAC_URL="https://download.chia.net/vdf/LibFT4222-mac-v1.4.4.190.zip"
MAC_ARCHIVE="${WORK_DIR}/LibFT4222-mac-v1.4.4.190.zip"
MAC_DMG="libft4222.1.4.4.190.dmg"

usage() {
  cat <<'EOF'
Usage: scripts/get-libft4222.sh <install|clean>

install  Download and place FT4222 driver headers/libs.
clean    Remove downloaded artifacts and symlinks.
EOF
}

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

fetch_once() {
  local url="$1"
  local dest="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -L -o "$dest" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$dest" "$url"
  else
    echo "Missing curl or wget for downloads" >&2
    exit 1
  fi
}

validate_archive() {
  local kind="$1"
  local path="$2"
  case "$kind" in
    tgz)
      tar -tzf "$path" >/dev/null 2>&1
      ;;
    zip)
      unzip -t "$path" >/dev/null 2>&1
      ;;
    *)
      echo "Unknown archive type: $kind" >&2
      return 1
      ;;
  esac
}

fetch_with_retry() {
  local url="$1"
  local dest="$2"
  local kind="$3"
  local attempts="${4:-3}"
  local attempt=1

  while [ "$attempt" -le "$attempts" ]; do
    rm -f "$dest"
    fetch_once "$url" "$dest" || true
    if [ -f "$dest" ] && validate_archive "$kind" "$dest"; then
      return 0
    fi
    echo "Downloaded archive failed validation (attempt $attempt/$attempts)." >&2
    sleep $((2 * attempt))
    attempt=$((attempt + 1))
  done

  echo "Failed to download a valid archive from $url after $attempts attempts." >&2
  return 1
}

install_linux() {
  need_cmd tar

  mkdir -p "$WORK_DIR"
  fetch_with_retry "$LINUX_URL" "$LINUX_ARCHIVE" tgz 3
  tar -xzf "$LINUX_ARCHIVE" -C "$WORK_DIR"

  rm -rf "$HW_DIR"
  ln -s "$WORK_DIR" "$HW_DIR"
  ln -sf "$WORK_DIR/build-x86_64/libft4222.so.1.4.4.170" \
    "$WORK_DIR/build-x86_64/libft4222.so"
}

install_macos() {
  need_cmd unzip
  need_cmd hdiutil
  need_cmd install_name_tool

  mkdir -p "$WORK_DIR"
  fetch_with_retry "$MAC_URL" "$MAC_ARCHIVE" zip 3
  unzip -q "$MAC_ARCHIVE" -d "$WORK_DIR"

  local mount_dir="${WORK_DIR}/ft4222-mount"
  local mounted=0
  cleanup_mount() {
    if [ "$mounted" -eq 1 ]; then
      hdiutil detach "$mount_dir" >/dev/null 2>&1 || true
      mounted=0
    fi
  }
  trap cleanup_mount EXIT

  # If a previous run failed, try to detach before re-attaching.
  hdiutil detach "$mount_dir" >/dev/null 2>&1 || true
  mkdir -p "$mount_dir"
  hdiutil attach -nobrowse -readonly -mountpoint "$mount_dir" "${WORK_DIR}/${MAC_DMG}"
  mounted=1

  cp "$mount_dir/ftd2xx.h" "$WORK_DIR/"
  cp "$mount_dir/libft4222.h" "$WORK_DIR/"
  cp "$mount_dir/WinTypes.h" "$WORK_DIR/"
  cp "$mount_dir/build/libft4222.1.4.4.190.dylib" "$WORK_DIR/"
  cp "$mount_dir/build/libftd2xx.dylib" "$WORK_DIR/"
  cleanup_mount
  trap - EXIT

  ln -sf "libft4222.1.4.4.190.dylib" "${WORK_DIR}/libft4222.dylib"

  install_name_tool -id "@rpath/libftd2xx.dylib" "${WORK_DIR}/libftd2xx.dylib"
  install_name_tool -id "@rpath/libft4222.dylib" "${WORK_DIR}/libft4222.1.4.4.190.dylib"
  install_name_tool -change "libftd2xx.dylib" "@rpath/libftd2xx.dylib" \
    "${WORK_DIR}/libft4222.1.4.4.190.dylib"

  # Clear download metadata that can trigger loader trust checks on macOS.
  if command -v xattr >/dev/null 2>&1; then
    xattr -dr com.apple.provenance "$WORK_DIR" || true
    xattr -dr com.apple.quarantine "$WORK_DIR" || true
  fi
  # Optional escape hatch for local troubleshooting only.
  if [ "${CHIAVDF_ADHOC_SIGN_FTDI:-0}" = "1" ]; then
    need_cmd codesign
    codesign --force --sign - \
      "${WORK_DIR}/libftd2xx.dylib" \
      "${WORK_DIR}/libft4222.1.4.4.190.dylib" \
      "${WORK_DIR}/libft4222.dylib"
  fi

  rm -rf "$HW_DIR"
  ln -s "$WORK_DIR" "$HW_DIR"
}

clean_all() {
  local mount_dir="${WORK_DIR}/ft4222-mount"
  hdiutil detach "$mount_dir" >/dev/null 2>&1 || true
  rm -rf "$WORK_DIR"
  if [ -L "$HW_DIR" ] || [ -d "$HW_DIR" ]; then
    rm -rf "$HW_DIR"
  fi
}

case "${1:-}" in
  install)
    case "$(uname -s)" in
      Linux) install_linux ;;
      Darwin) install_macos ;;
      *) echo "Unsupported OS: $(uname -s)" >&2; exit 1 ;;
    esac
    ;;
  clean)
    clean_all
    ;;
  *)
    usage
    exit 1
    ;;
esac
