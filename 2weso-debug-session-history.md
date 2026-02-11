# 2weso Debug Session History

## Context

- Platform: Windows CI (`windows-latest`)
- Failing target: `1weso_test.exe` during `Test vdf-client (Windows)`
- Symptom: hard process exit with code `-1073741819` (`0xC0000005`, access violation)

## Timeline

1. **Initial Windows integration**
   - Added Windows CI matrix/build/test flow in `.github/workflows/test.yaml`.
   - Enabled GNU asm pipeline on Windows and generated asm artifacts.

2. **Build failure (linker)**
   - Error: undefined symbol for `comment_label`.
   - Fix: corrected label emission in `src/asm_base.h` to use allocated label token expansion.

3. **Runtime crash after linker fix**
   - `1weso_test` crashed in Windows run.
   - Added logs around fast path and GCD selection (`H1`, `H3`, `H6`, `H9`, `H12`, `H13`, `H14`, `H15`, `H21`).

4. **SEH instrumentation in GCD wrapper**
   - Added `__try/__except` around AVX2/CEL GCD calls in `src/threading.h` with `H22`.
   - Result: crash persisted; `H22` did not appear.

5. **Worker-level SEH instrumentation**
   - Added `H23` around `repeated_square_fast_work` loop in `src/vdf_fast.h`.
   - Result: crash persisted; `H23` did not appear.

6. **Process-wide exception capture**
   - Added vectored exception handler in `src/1weso_test.cpp` with `H24`.
   - Result: captured first-chance access violation.

## Latest Evidence (most recent run)

From CI commit `bac43ae48d142bf8eb55a85580bf70e7905211e9`:

- Last normal path logs:
  - `H13 p0_master_before_gcd`
  - `H14 gcd_enter ... a_limbs=5 b_limbs=4 ...`
  - `H15 gcd_impl_select ... use_avx2=1 force_cel=0 ...`
- Exception log:
  - `H24 veh_exception code=0xc0000005 ...`
  - `ip_rva=0x25378`
  - `d_avx2_gcd_unsigned=0xda`
  - `d_cel_gcd_unsigned=0x5b9b`
  - `d_avx2_gcd_128=0xaa0`
  - `d_cel_gcd_128=0x6c8d`

## Interpretation

- The crash IP is very near the entry of `asm_avx2_func_gcd_unsigned` (`+0xDA`), strongly indicating the fault is **inside AVX2 unsigned GCD generated asm**.
- The exception is an access violation (`0xC0000005`) and is not being caught by local `H22`/`H23` handlers before process termination.
- No `H25 asm_track_at_crash` lines appeared in the provided run output (either tracking did not emit before termination, or no nonzero tracked counters were available at crash point).

## Current Status

- Compile-time blockers previously seen on Windows are resolved.
- Runtime fault remains reproducible in phase-0 fast path at base iteration where `a_bits=257`, `b_bits=256`.
- Root-cause area is narrowed to early instructions in generated `asm_avx2_func_gcd_unsigned`.
