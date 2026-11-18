#!/usr/bin/env bash
# Benchmark a GMP install produced by scripts/build-windows-mingw-gmp.sh.
#   run-gmp-bench.sh PREFIX GMP_SOURCE_TREE OUTPUT_FILE
# Runs (1) gmp_microbench.cpp (mpz ops at chiavdf operand sizes) and
# (2) GMP's own tune/speed on the mpn kernels at 8-32 limbs.
set -uo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 PREFIX GMP_SOURCE_TREE OUTPUT_FILE" >&2
  exit 2
fi

case "$(uname -s)" in
  MINGW*|MSYS*) to_unix() { cygpath -u "$1"; }; static=-static ;;
  *) to_unix() { printf '%s\n' "$1"; }; static= ;;
esac

script_dir=$(cd "$(dirname "$0")" && pwd)
prefix=$(to_unix "$1")
tree=$(to_unix "$2")
out=$(to_unix "$3")
mkdir -p "$(dirname "$out")"
: > "$out"
log() { tee -a "$out"; }

{
  echo "=== host ==="
  uname -a
  if [ -r /proc/cpuinfo ]; then grep -m1 'model name' /proc/cpuinfo; fi
  (cd "$tree" && echo "GMP config.guess: $(./config.guess)")
  grep -m1 '^MPN_PATH\|^mpn_objs_in_libgmp\|path=' "$tree/config.log" 2>/dev/null | head -1
  echo
} | log

echo "=== gmp_microbench ===" | log
exe="$script_dir/gmp_microbench.exe"
g++ -O2 -std=c++17 $static "$script_dir/gmp_microbench.cpp" \
  -I"$prefix/include" "$prefix/lib/libgmp.a" -o "$exe"
"$exe" 2>&1 | log
echo | log

echo "=== tune/speed (seconds per call, 8..32 limbs) ===" | log
if make -C "$tree/tune" speed >/dev/null 2>&1; then
  for fn in mpn_add_n mpn_addmul_1 mpn_mul_1 mpn_mul_basecase mpn_sqr_basecase \
            mpn_mul_n mpn_sqr mpn_gcdext mpn_gcd mpn_tdiv_qr; do
    echo "--- $fn ---" | log
    "$tree/tune/speed" -s 8-32 -t 8 "$fn" 2>&1 | log || echo "(speed $fn failed)" | log
  done
else
  echo "(could not build tune/speed)" | log
fi
