#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${ROOT_DIR}/src"
WORK_DIR="${SRC_DIR}/libft4222"
HW_DIR="${SRC_DIR}/hw/libft4222"

# Upstream FT4222 driver downloads can be found at https://ftdichip.com/drivers/d2xx-drivers/
# on the Downloads tab in the middle of the page as of 2026-02-05.
# We currently fetch known-good versions mirrored at download.chia.net.

# Updated Linux LibFT4222 releases can be found at https://ftdichip.com/software-examples/ft4222h-software-examples/
LINUX_URL="https://download.chia.net/vdf/libft4222-linux-1.4.4.232.zip"
# ftdichip.cn is currently down; confirm this URL before enabling it as a fallback:
# LINUX_URL="https://ftdichip.cn/Support/SoftwareExamples/libft4222-linux-1.4.4.232.zip"
LINUX_ARCHIVE="${WORK_DIR}/libft4222-linux-1.4.4.232.zip"
LINUX_SHA256="9e7141d252b67c74b585e85652079e801c7e820e7bf271fc0fa061ef849ceff0"
LINUX_TGZ="libft4222-linux-1.4.4.232.tgz"
MAC_DMG_URL="https://download.chia.net/vdf/libft4222-mac-v1.4.4.221.dmg"
# ftdichip.cn is currently down; confirm this URL before enabling it as a fallback:
# MAC_DMG_URL="https://ftdichip.cn/Support/SoftwareExamples/libft4222-mac-v1.4.4.221.dmg"
MAC_DMG="${WORK_DIR}/libft4222-mac-v1.4.4.221.dmg"
MAC_DMG_SHA256="ef2be6003d9286c4c50d3f3db329dced9f800ea53894cce41cc568100fb3d43f"
# D2XX 1.4.36 ships in its own image, pinned by SHA-256.
D2XX_DMG_URL="https://download.chia.net/vdf/D2XX1.4.36.dmg"
# ftdichip.cn is currently down; confirm this URL before enabling it as a fallback:
# D2XX_DMG_URL="https://ftdichip.cn/Drivers/D2XX/MacOSX/D2XX1.4.36.dmg"
D2XX_DMG="${WORK_DIR}/D2XX1.4.36.dmg"
D2XX_DMG_SHA256="6cc05c59762fe3ceb949cb5d9f0ee39265de19780d25da0c3e4c12dc8c146e07"

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

sha256_of() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    echo "Missing shasum or sha256sum for checksums" >&2
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
    linuxzip)
      unzip -t "$path" >/dev/null 2>&1 && [ "$(sha256_of "$path")" = "$LINUX_SHA256" ]
      ;;
    dmg)
      [ "$(sha256_of "$path")" = "$D2XX_DMG_SHA256" ]
      ;;
    ft4222dmg)
      [ "$(sha256_of "$path")" = "$MAC_DMG_SHA256" ]
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
  need_cmd unzip

  mkdir -p "$WORK_DIR"
  fetch_with_retry "$LINUX_URL" "$LINUX_ARCHIVE" linuxzip 3
  unzip -q -o "$LINUX_ARCHIVE" -d "$WORK_DIR"
  tar -xzf "$WORK_DIR/$LINUX_TGZ" -C "$WORK_DIR"

  local linux_arch
  linux_arch="$(uname -m)"
  local libft4222_dir
  case "$linux_arch" in
    x86_64) libft4222_dir="build-x86_64" ;;
    aarch64|arm64) libft4222_dir="build-arm-v8" ;;
    *)
      echo "Unsupported Linux architecture for libft4222: $linux_arch" >&2
      exit 1
      ;;
  esac

  rm -rf "$HW_DIR"
  ln -s "$WORK_DIR" "$HW_DIR"
  ln -sf "$WORK_DIR/$libft4222_dir/libft4222.so.1.4.4.232" \
    "$WORK_DIR/$libft4222_dir/libft4222.so"
}

install_macos() {
  need_cmd hdiutil
  need_cmd install_name_tool

  mkdir -p "$WORK_DIR"
  fetch_with_retry "$MAC_DMG_URL" "$MAC_DMG" ft4222dmg 3

  local mount_dir="${WORK_DIR}/ft4222-mount"
  local d2xx_mount_dir="${WORK_DIR}/d2xx-mount"
  local mounted=0
  local d2xx_mounted=0
  cleanup_mounts() {
    if [ "$mounted" -eq 1 ]; then
      hdiutil detach "$mount_dir" >/dev/null 2>&1 || true
      mounted=0
    fi
    if [ "$d2xx_mounted" -eq 1 ]; then
      hdiutil detach "$d2xx_mount_dir" >/dev/null 2>&1 || true
      d2xx_mounted=0
    fi
  }
  trap cleanup_mounts EXIT

  # If a previous run failed, try to detach before re-attaching.
  hdiutil detach "$mount_dir" >/dev/null 2>&1 || true
  mkdir -p "$mount_dir"
  hdiutil attach -nobrowse -readonly -mountpoint "$mount_dir" "$MAC_DMG"
  mounted=1

  cp "$mount_dir/libft4222.h" "$WORK_DIR/"
  cp "$mount_dir/build/libft4222.1.4.4.221.dylib" "$WORK_DIR/"
  cleanup_mounts

  fetch_with_retry "$D2XX_DMG_URL" "$D2XX_DMG" dmg 3

  # If a previous run failed, try to detach before re-attaching.
  hdiutil detach "$d2xx_mount_dir" >/dev/null 2>&1 || true
  mkdir -p "$d2xx_mount_dir"
  hdiutil attach -nobrowse -readonly -mountpoint "$d2xx_mount_dir" "$D2XX_DMG"
  d2xx_mounted=1

  cp "$d2xx_mount_dir/release/build/libftd2xx.1.4.36.dylib" "${WORK_DIR}/libftd2xx.dylib"
  cp "$d2xx_mount_dir/release/ftd2xx.h" "$WORK_DIR/"
  cp "$d2xx_mount_dir/release/WinTypes.h" "$WORK_DIR/"
  cleanup_mounts
  trap - EXIT

  ln -sf "libft4222.1.4.4.221.dylib" "${WORK_DIR}/libft4222.dylib"

  install_name_tool -id "@rpath/libftd2xx.dylib" "${WORK_DIR}/libftd2xx.dylib"
  install_name_tool -id "@rpath/libft4222.dylib" "${WORK_DIR}/libft4222.1.4.4.221.dylib"
  install_name_tool -change "libftd2xx.dylib" "@rpath/libftd2xx.dylib" \
    "${WORK_DIR}/libft4222.1.4.4.221.dylib"

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
      "${WORK_DIR}/libft4222.1.4.4.221.dylib" \
      "${WORK_DIR}/libft4222.dylib"
  fi

  rm -rf "$HW_DIR"
  ln -s "$WORK_DIR" "$HW_DIR"
}

clean_all() {
  local mount_dir="${WORK_DIR}/ft4222-mount"
  local d2xx_mount_dir="${WORK_DIR}/d2xx-mount"
  hdiutil detach "$mount_dir" >/dev/null 2>&1 || true
  hdiutil detach "$d2xx_mount_dir" >/dev/null 2>&1 || true
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
