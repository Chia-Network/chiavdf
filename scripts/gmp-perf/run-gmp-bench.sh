#!/usr/bin/env bash
# Run gmp_microbench.cpp against a GMP install, with system malloc and with
# mimalloc (if available), appending human-readable output plus
# "RESULT|label|metric|value" lines to OUTPUT_FILE.
#   run-gmp-bench.sh PREFIX LABEL OUTPUT_FILE
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 PREFIX LABEL OUTPUT_FILE" >&2
  exit 2
fi

case "$(uname -s)" in
  MINGW*|MSYS*) to_unix() { cygpath -u "$1"; } ;;
  *) to_unix() { printf '%s\n' "$1"; } ;;
esac

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

echo "=== gmp_microbench $label/mimalloc ===" | tee -a "$out"
if build "$work/mb-mimalloc" "-DUSE_MIMALLOC" "-lmimalloc"; then
  BENCH_LABEL="$label/mimalloc" "$work/mb-mimalloc" 2>&1 | tee -a "$out"
else
  echo "(mimalloc build failed)" | tee -a "$out"
fi
