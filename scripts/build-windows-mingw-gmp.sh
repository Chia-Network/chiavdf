#!/usr/bin/env bash
# Build a pinned MinGW-w64 static GMP archive for the MSVC linker experiment.
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 PREFIX" >&2
  exit 2
fi

version=6.3.0
sha256=a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898
prefix=$(cygpath -u "$1")
build_root="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/chiavdf-gmp-build"
build_root=$(cygpath -u "$build_root")
mkdir -p "$build_root"
cd "$build_root"

curl --fail --silent --show-error --location \
  "https://ftp.gnu.org/gnu/gmp/gmp-${version}.tar.xz" \
  -o "gmp-${version}.tar.xz"
echo "$sha256  gmp-${version}.tar.xz" | sha256sum --check --status
tar -xf "gmp-${version}.tar.xz"
cd "gmp-${version}"

./configure \
  --build=x86_64-pc-msys \
  --host=x86_64-w64-mingw32 \
  --prefix="$prefix" \
  --disable-shared \
  --enable-static \
  --enable-cxx \
  CFLAGS="-std=gnu17"
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
