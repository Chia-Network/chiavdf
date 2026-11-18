#!/usr/bin/env bash
# Run gmp_microbench.cpp against a GMP install, with system malloc and with
# mimalloc (built from a pinned source release), appending human-readable output plus
# "RESULT|label|metric|value" lines to OUTPUT_FILE.
#   run-gmp-bench.sh PREFIX LABEL OUTPUT_FILE
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 PREFIX LABEL OUTPUT_FILE" >&2
  exit 2
fi

case "$(uname -s)" in
  MINGW*|MSYS*) to_unix() { cygpath -u "$1"; }; mimalloc_syslibs="-lbcrypt -lpsapi -ladvapi32" ;;
  *) to_unix() { printf '%s\n' "$1"; }; mimalloc_syslibs="-lpthread" ;;
esac

# Pinned mimalloc release, built from source so every OS uses the same version.
mimalloc_version=2.1.7
mimalloc_sha256=0eed39319f139afde8515010ff59baf24de9e47ea316a315398e8027d198202d

script_dir=$(cd "$(dirname "$0")" && pwd)
prefix=$(to_unix "$1")
label=$2
out=$(to_unix "$3")
mkdir -p "$(dirname "$out")"
work=$(mktemp -d)

build() {  # build <exe> <extra compile flags> <extra libs>
  # shellcheck disable=SC2086  # flags are intentionally word-split
  g++ -O2 -std=c++17 $2 "$script_dir/gmp_microbench.cpp" -I"$prefix/include" \
    "$prefix/lib/libgmp.a" $3 -o "$1"
}

echo "=== gmp_microbench $label/system ===" | tee -a "$out"
build "$work/mb-system" "" ""
BENCH_LABEL="$label/system" "$work/mb-system" 2>&1 | tee -a "$out"

build_mimalloc() {  # called from an if, where set -e is off: chain every step
  local tarball="$work/mimalloc-$mimalloc_version.tar.gz"
  local src="$work/mimalloc-$mimalloc_version"
  curl --fail --silent --show-error --location -o "$tarball" \
    "https://github.com/microsoft/mimalloc/archive/refs/tags/v$mimalloc_version.tar.gz" &&
    echo "$mimalloc_sha256  $tarball" | sha256sum --check --status &&
    tar -xzf "$tarball" -C "$work" &&
    gcc -O2 -DNDEBUG -I"$src/include" -c "$src/src/static.c" -o "$work/mimalloc.o" &&
    mimalloc_include="$src/include"
}

echo "=== gmp_microbench $label/mimalloc ===" | tee -a "$out"
if build_mimalloc &&
   build "$work/mb-mimalloc" "-DUSE_MIMALLOC -I$mimalloc_include" "$work/mimalloc.o $mimalloc_syslibs"; then
  BENCH_LABEL="$label/mimalloc" "$work/mb-mimalloc" 2>&1 | tee -a "$out"
else
  echo "(mimalloc build failed)" | tee -a "$out"
fi
