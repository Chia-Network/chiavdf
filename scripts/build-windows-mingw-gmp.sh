#!/usr/bin/env bash
# Build the pinned MinGW-w64 static GMP libraries used by the Windows Rust crate.
#
# Optional knobs (defaults reproduce the production build):
#   GMP_HOST_CPU   x86_64 (generic, default) or "native" to target the build
#                  machine's exact CPU as detected by GMP's config.guess.
#   GMP_FAT        1 (default) to pass --enable-fat, 0 to omit it.
#   GMP_CFLAGS     CFLAGS passed to configure (default "-O2 -std=gnu17").
#                  The special value "gmp-default" leaves CFLAGS unset so GMP
#                  picks its own per-CPU flags; -std=gnu17 then goes into CC.
#   GMP_SKIP_CHECK 1 to skip "make check".
#   GMP_BUILD_ROOT where the source tree is unpacked (kept after the build).
#
# Also runs on Linux (for baseline comparisons), where it does a native build.
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 PREFIX" >&2
  exit 2
fi

case "$(uname -s)" in
  MINGW*|MSYS*) on_windows=1 ;;
  *) on_windows=0 ;;
esac

to_unix() { if [ "$on_windows" = 1 ]; then cygpath -u "$1"; else printf '%s\n' "$1"; fi; }

version=6.3.0
sha256=a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898
host_cpu=${GMP_HOST_CPU:-x86_64}
fat=${GMP_FAT:-1}
cflags=${GMP_CFLAGS:--O2 -std=gnu17}

prefix=$(to_unix "$1")
build_root=${GMP_BUILD_ROOT:-${RUNNER_TEMP:-${TMPDIR:-/tmp}}/chiavdf-gmp-build}
build_root=$(to_unix "$build_root")
mkdir -p "$build_root"
cd "$build_root"

if [ ! -f "gmp-${version}.tar.xz" ]; then
  curl --fail --silent --show-error --location \
    "https://ftp.gnu.org/gnu/gmp/gmp-${version}.tar.xz" \
    -o "gmp-${version}.tar.xz"
fi
echo "$sha256  gmp-${version}.tar.xz" | sha256sum --check --status
rm -rf "gmp-${version}"
tar -xf "gmp-${version}.tar.xz"
cd "gmp-${version}"

if [ "$host_cpu" = native ]; then
  host_cpu=$(./config.guess | cut -d- -f1)
  echo "GMP config.guess detected CPU: $host_cpu"
fi

configure_args=(--prefix="$prefix" --disable-shared --enable-static --enable-cxx)
if [ "$on_windows" = 1 ]; then
  configure_args+=(--build=x86_64-pc-msys --host="${host_cpu}-w64-mingw32")
  cc=x86_64-w64-mingw32-gcc
  cxx=x86_64-w64-mingw32-g++
else
  configure_args+=(--host="${host_cpu}-pc-linux-gnu")
  cc=gcc
  cxx=g++
fi
if [ "$fat" = 1 ]; then
  configure_args+=(--enable-fat)
fi

echo "Configuring GMP: host_cpu=$host_cpu fat=$fat cflags=$cflags"
if [ "$cflags" = gmp-default ]; then
  CC="$cc -std=gnu17" CXX="$cxx" ./configure "${configure_args[@]}"
else
  CC="$cc" CXX="$cxx" CFLAGS="$cflags" ./configure "${configure_args[@]}"
fi
make -j"$(nproc)"
if [ "${GMP_SKIP_CHECK:-0}" != 1 ]; then
  make check
fi
make install

test -s "$prefix/lib/libgmp.a"
test -s "$prefix/include/gmp.h"
test -s "$prefix/include/gmpxx.h"
echo "GMP CC:     $(grep -m1 '__GMP_CC ' "$prefix/include/gmp.h")"
echo "GMP CFLAGS: $(grep -m1 '__GMP_CFLAGS ' "$prefix/include/gmp.h")"

if [ "$on_windows" = 1 ]; then
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
fi
echo "Built GMP ${version} static archive at $prefix"
