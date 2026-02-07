# Windows perf notes (vdf_bench square_asm)

## Summary
- Ubuntu baseline: ~160.3K ips
- Windows baseline: ~128.3K ips
- Gap: ~20% slower on Windows in current clang++ GNU-style asm build

## Ubuntu perf experiment deltas
- lto: -0.7%
- march: -2.1%
- lto-march: 0.0%
- pgo: -0.5%
- pgo-march: -0.9%

## Windows perf experiment deltas
- lto: -10.4%
- march: +0.4%
- lto-march: +0.9%
- pgo: +1.3%
- pgo-march: -4.1%

## Next investigation hooks (in CI)
- Baseline asm hashes are captured for Ubuntu/Windows to compare generated `.s`.
- Optional Windows WPR trace (`wpr-baseline.etl`) is captured when available.
- Additional Windows variants (clang-cl, lld-link, nosec) are available for parity testing.
