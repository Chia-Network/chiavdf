#!/usr/bin/env bash
# Build the GMP that ships in the macOS arm64 release bundle.
#
# Homebrew's bottle uses GMP's default two-limb division in mpn_hgcd2. The
# Lehmer loops in mpz_gcdext dominate class-group squaring and composition, and
# HGCD2_DIV2_METHOD=1 is markedly faster on Apple cores. That method is a
# compile-time choice, so the release builds GMP from the pinned upstream
# tarball. No -mcpu flag: the bundle has to run on every Apple Silicon Mac, and
# -mcpu=apple-m1 measured no faster than plain -O3.
#
# Every dylib this links carries a __TEXT,__chiavdf_gmp section with the
# tarball hash and configure flags. install_name_tool and codesign keep it, so
# scripts/check-macos-gmp-build.sh can confirm which GMP a bundle ships.
set -euo pipefail

GMP_VERSION=6.3.0
GMP_SHA256=a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898
GMP_URLS=(
  "https://ftp.gnu.org/gnu/gmp/gmp-${GMP_VERSION}.tar.xz"
  "https://gmplib.org/download/gmp/gmp-${GMP_VERSION}.tar.xz"
)
GMP_CFLAGS="-O3 -DHGCD2_DIV2_METHOD=1"
GMP_CXXFLAGS="-O3"
# GMP 6.3.0's libtool links -flat_namespace when the deployment target is unset
# or 10.0-10.2. 11.0 is the first Apple Silicon release.
GMP_DEPLOYMENT_TARGET=11.0

usage() {
  cat >&2 <<'EOF'
Usage: scripts/build-macos-gmp.sh PREFIX

Builds, checks (make check), and installs GMP into the absolute PREFIX.

Environment:
  GMP_TARBALL     use this local gmp tarball instead of downloading (hash still checked)
  GMP_BUILD_DIR   build directory (default: a new temporary directory)
  GMP_BUILD_JOBS  make -j value (default: hw.ncpu)
  GMP_EXTRA_CFLAGS  appended to the C flags (benchmarking only; not for releases)
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

cflags="$GMP_CFLAGS${GMP_EXTRA_CFLAGS:+ $GMP_EXTRA_CFLAGS}"
jobs=${GMP_BUILD_JOBS:-$(sysctl -n hw.ncpu)}
build_dir=${GMP_BUILD_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/chiavdf-gmp.XXXXXX")}
mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd)"
tarball="$build_dir/gmp-${GMP_VERSION}.tar.xz"

if [ -n "${GMP_TARBALL:-}" ]; then
  cp "$GMP_TARBALL" "$tarball"
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
  printf 'gmp %s sha256 %s\n' "$GMP_VERSION" "$GMP_SHA256"
  printf 'CFLAGS=%s\n' "$cflags"
  printf 'CXXFLAGS=%s\n' "$GMP_CXXFLAGS"
  printf 'MACOSX_DEPLOYMENT_TARGET=%s\n' "$GMP_DEPLOYMENT_TARGET"
  printf 'compiler=%s\n' "$(clang --version | head -n 1)"
} > "$build_info"

cd "$src_dir"
# --build matches Homebrew's formula: generic aarch64 mpn code, no host probe.
MACOSX_DEPLOYMENT_TARGET="$GMP_DEPLOYMENT_TARGET" ./configure \
  --prefix="$prefix" \
  --build="aarch64-apple-darwin$(uname -r | cut -d. -f1)" \
  --enable-cxx \
  --disable-static \
  --with-pic \
  CC=clang CXX=clang++ \
  CFLAGS="$cflags" \
  CXXFLAGS="$GMP_CXXFLAGS" \
  LDFLAGS="-Wl,-sectcreate,__TEXT,__chiavdf_gmp,$build_info"
MACOSX_DEPLOYMENT_TARGET="$GMP_DEPLOYMENT_TARGET" make -j "$jobs"
MACOSX_DEPLOYMENT_TARGET="$GMP_DEPLOYMENT_TARGET" make -j "$jobs" check
make install

printf 'GMP %s (%s) installed at %s\n' "$GMP_VERSION" "$cflags" "$prefix"
