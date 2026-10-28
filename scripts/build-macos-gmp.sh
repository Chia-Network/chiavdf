#!/usr/bin/env bash
# Build the GMP that ships in the macOS arm64 release bundle, as defined in
# scripts/macos-gmp-release.sh (version, tarball hash and flags). The DIV2
# method is a compile-time choice, so the release builds GMP from the pinned
# upstream tarball.
#
# Every dylib this links carries a __TEXT,__chiavdf_gmp section with the
# release identity lines. install_name_tool and codesign keep it, so
# scripts/check-macos-gmp-build.sh can confirm which GMP a bundle ships.
set -euo pipefail

# shellcheck source=scripts/macos-gmp-release.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/macos-gmp-release.sh"

GMP_URLS=(
  "https://ftp.gnu.org/gnu/gmp/gmp-${GMP_VERSION}.tar.xz"
  "https://gmplib.org/download/gmp/gmp-${GMP_VERSION}.tar.xz"
)

usage() {
  cat >&2 <<'EOF'
Usage: scripts/build-macos-gmp.sh PREFIX

Builds, checks (make check), and installs GMP into the absolute PREFIX.

Environment:
  GMP_TARBALL     use this local gmp tarball instead of downloading (hash still checked)
  GMP_BUILD_DIR   build directory, kept afterwards (default: a temporary
                  directory, removed when the script exits); must not
                  contain a comma
  GMP_BUILD_JOBS  make -j value (default: hw.ncpu)
EOF
}

if [ "$#" -ne 1 ]; then
  usage
  exit 1
fi
prefix=$1
case "$prefix" in
  /*) ;;
  *)
    echo "PREFIX must be absolute: $prefix" >&2
    exit 1
    ;;
esac
if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
  echo "This GMP build is for macOS arm64 only." >&2
  exit 1
fi

jobs=${GMP_BUILD_JOBS:-$(sysctl -n hw.ncpu)}
if [ -n "${GMP_BUILD_DIR:-}" ]; then
  build_dir=$GMP_BUILD_DIR
else
  build_dir="$(mktemp -d "${TMPDIR:-/tmp}/chiavdf-gmp.XXXXXX")"
  trap 'rm -rf "$build_dir"' EXIT
fi
mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd)"
case "$build_dir" in
  *,*)
    echo "Build directory path must not contain a comma: $build_dir" >&2
    exit 1
    ;;
esac
tarball="$build_dir/gmp-${GMP_VERSION}.tar.xz"

if [ -n "${GMP_TARBALL:-}" ]; then
  if [ "$GMP_TARBALL" -ef "$tarball" ]; then
    :
  else
    cp "$GMP_TARBALL" "$tarball"
  fi
else
  downloaded=false
  for url in "${GMP_URLS[@]}"; do
    if curl --fail --silent --show-error --location --retry 3 "$url" -o "$tarball"; then
      downloaded=true
      break
    fi
    echo "Download failed: $url" >&2
  done
  if [ "$downloaded" != true ]; then
    echo "Could not download gmp-${GMP_VERSION}.tar.xz" >&2
    exit 1
  fi
fi
echo "$GMP_SHA256  $tarball" | shasum -a 256 -c -

src_dir="$build_dir/gmp-${GMP_VERSION}"
rm -rf "$src_dir"
tar -xf "$tarball" -C "$build_dir"

build_info="$build_dir/chiavdf-gmp-build-info.txt"
{
  gmp_release_identity
  printf 'compiler=%s\n' "$(clang --version | head -n 1)"
} > "$build_info"

export MACOSX_DEPLOYMENT_TARGET="$GMP_DEPLOYMENT_TARGET"
cd "$src_dir"
# --build matches Homebrew's formula: generic aarch64 mpn code, no host probe.
./configure \
  --prefix="$prefix" \
  --build="aarch64-apple-darwin$(uname -r | cut -d. -f1)" \
  --enable-cxx \
  --disable-static \
  --with-pic \
  CC=clang CXX=clang++ \
  CFLAGS="$GMP_CFLAGS" \
  CXXFLAGS="$GMP_CXXFLAGS" \
  LDFLAGS="-Wl,-sectcreate,__TEXT,__chiavdf_gmp,$build_info"
make -j "$jobs"
make -j "$jobs" check
make install

printf 'GMP %s (%s) installed at %s\n' "$GMP_VERSION" "$GMP_CFLAGS" "$prefix"
