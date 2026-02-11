# 2weso Debug Session History

## Context

- Platform: Windows CI (`windows-latest`)
- Symptom: hard runtime crashes in asm-enabled paths (`0xC0000005` and `0xC000001D`)
- Affected binaries during debugging: `vdf_bench`, `1weso_test`, `2weso_test`
- Goal: keep asm path enabled and make Windows CI pass

## Root Causes Found

1. **ASLR-incompatible absolute addressing in generated asm**
   - Some global and table accesses were emitted as absolute addresses.
   - Under Windows ASLR, those addresses were invalid at runtime.

2. **Indirect jump-table dispatch instability in `gcd_unsigned`**
   - Crashes landed in regions decoding as non-code bytes near dispatch/table areas.
   - Dispatch behavior was made deterministic by using compare/branch dispatch for Windows.

3. **Workflow false failure after runtime stabilization**
   - The Windows smoke helper captured process stdout and exit code together, causing a bad gate check despite successful probes.

## Final Fixes Applied

- `src/asm_base.h`
  - Kept tracking-data accesses RIP-relative on Windows:
    - `track_asm_rax`
    - `asm_tracking_data`
    - `asm_tracking_data_comments`

- `src/asm_gcd_base_divide_table.h`
  - Switched divide-table indexed loads to RIP-relative addressing on Windows.

- `src/asm_gcd_base_continued_fractions.h`
  - Switched `gcd_base_table` loads to RIP-relative addressing on Windows.

- `src/asm_gcd_unsigned.h`
  - Replaced Windows `multiply_uv` indirect jump-table dispatch with explicit compare/branch dispatch (same style as macOS path).

- `.github/workflows/test.yaml`
  - Fixed helper return handling to compare integer exit codes only.
  - Removed temporary disassembly/probe debug instrumentation after validation.

## Validation Evidence

- Passing run: `https://github.com/Chia-Network/chiavdf/actions/runs/21924184980`
- Result: `Test optimized=1 windows-latest` passed with asm enabled.
- Verified stages:
  - `vdf_bench` smoke (default): exit `0`
  - `vdf_bench` smoke (forced CEL): exit `0`
  - `1weso_test` probe (default): exit `0`
  - `1weso_test` probe (forced CEL): exit `0`
  - Windows `Test vdf-client` and benchmark step completed successfully

## Cleanup Status

- Temporary runtime debug instrumentation has been removed from source and workflow files.
- Functional asm fixes remain in place.
