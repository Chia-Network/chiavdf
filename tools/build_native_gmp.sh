#!/usr/bin/env bash
# Build a private Apple Silicon GMP; never replace Homebrew/system libraries.
# Usage: bash tools/build_native_gmp.sh /absolute/install/prefix [build-directory]
# To use: DYLD_LIBRARY_PATH=/absolute/install/prefix/lib ./src/vdf_bench square 3000000
set -euo pipefail
if [[ $# -lt 1 || "$1" != /* ]]; then
    echo 'Usage: build_native_gmp.sh /absolute/install/prefix [build-directory]' >&2
    exit 2
fi
if [[ $(uname -s) != Darwin || $(uname -m) != arm64 ]]; then
    echo 'This measured configuration targets Apple Silicon macOS.' >&2
    exit 2
fi
gmp_prefix=$1
gmp_build=${2:-$(mktemp -d "${TMPDIR:-/tmp}/chiavdf-gmp.XXXXXX")}
mkdir -p "$gmp_build"
cd "$gmp_build"
curl --fail --location https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz -o gmp-6.3.0.tar.xz
echo 'a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898  gmp-6.3.0.tar.xz' | shasum -a 256 -c -
tar -xf gmp-6.3.0.tar.xz
cd gmp-6.3.0
./configure --prefix="$gmp_prefix" --enable-cxx CC=clang CXX=clang++ \
    CFLAGS='-O3 -mcpu=native -DHGCD2_DIV2_METHOD=1' CXXFLAGS='-O3 -mcpu=native'
make -j "${GMP_BUILD_JOBS:-4}"
make -j "${GMP_BUILD_JOBS:-4}" check
make install
printf 'Private GMP installed at %s\n' "$gmp_prefix"
