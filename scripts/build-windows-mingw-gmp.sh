#!/usr/bin/env bash
# Build the pinned MinGW-w64 static GMP libraries used by the Windows Rust crate.
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "Usage: $0 PREFIX [PATCH_FILE]" >&2
  exit 2
fi

# BASH_SOURCE, not $0: CI also sources this script.
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
patch_file=$(cygpath -au "${2:-$script_dir/gmp-6.3.0-fat-isa.patch}")
version=6.3.0
sha256=be5c908a7a836c3a9bd9d62aa58563c5e9e7fef94c43a7f42dbc35bb6d02733c
prefix=$(cygpath -u "$1")
build_root="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/chiavdf-gmp-build"
build_root=$(cygpath -u "$build_root")
mkdir -p "$build_root"
cd "$build_root"

curl --fail --silent --show-error --location \
  "https://download.chia.net/vdf/gmp-${version}.tar.lz" \
  -o "gmp-${version}.tar.lz"
echo "$sha256  gmp-${version}.tar.lz" | sha256sum --check --status
rm -rf "gmp-${version}"  # fresh tree, so the patch below applies on reruns
tar --lzip -xf "gmp-${version}.tar.lz"
cd "gmp-${version}"
patch -p1 < "$patch_file"

m4_path=/usr/bin/m4.exe
if [ ! -x "$m4_path" ]; then
  echo "GNU m4 was not found at $m4_path" >&2
  exit 1
fi
echo "Using m4 at $m4_path"
"$m4_path" --version

M4="$m4_path" CFLAGS="-O3 -std=gnu17" CXXFLAGS="-O3 -std=gnu++17" ./configure \
  --build=x86_64-pc-msys \
  --host=x86_64-w64-mingw32 \
  --prefix="$prefix" \
  --enable-fat \
  --disable-shared \
  --enable-static \
  --enable-cxx
make -j"$(nproc)"
make check
make install

test -s "$prefix/lib/libgmp.a"
test -s "$prefix/include/gmp.h"
test -s "$prefix/include/gmpxx.h"
libmingwex=$(x86_64-w64-mingw32-gcc -print-file-name=libmingwex.a)
libgcc=$(x86_64-w64-mingw32-gcc -print-file-name=libgcc.a)
test -s "$libmingwex"
test -s "$libgcc"
cp "$libmingwex" "$prefix/lib/libmingwex.a"
cp "$libgcc" "$prefix/lib/libgcc.a"
if [ -n "${GITHUB_ENV:-}" ]; then
  github_env=$(cygpath -u "$GITHUB_ENV")
  printf 'CHIAVDF_GMP_PREFIX=%s\n' "$(cygpath -w "$prefix")" >> "$github_env"
  printf 'MINGW_LIBMINGWEX=%s\n' "$(cygpath -w "$prefix/lib/libmingwex.a")" >> "$github_env"
  printf 'MINGW_LIBGCC=%s\n' "$(cygpath -w "$prefix/lib/libgcc.a")" >> "$github_env"
fi
echo "Built GMP ${version} static archive at $prefix"
