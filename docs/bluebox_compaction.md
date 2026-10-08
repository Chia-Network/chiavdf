# Bluebox Compaction Optimizations

This document describes the compaction-oriented proving path exposed by
`src/c_bindings/fast_wrapper.h` and implemented in
`src/c_bindings/fast_wrapper.cpp`.

## Scope

These APIs are intended for workloads where the expected VDF output (`y_ref`) is
already known up front (for example, bluebox compaction jobs). They are additive
and do not change the existing `c_wrapper` APIs.

## Optimization 1: Streaming one-wesolowski

Given `y_ref`, the prover computes:

- `B = GetB(D, x, y_ref)` before squaring starts

This enables a streaming algorithm that updates proof buckets at each
checkpoint during repeated squaring, instead of materializing the full
intermediate checkpoint array and scanning it after the loop. In practice this
substantially reduces memory usage for compaction workloads.

## Optimization 2: Incremental GetBlock mapping

For streaming checkpoint updates, bucket index selection repeatedly calls
`GetBlock(p, k, T, B)`. The optimized mode keeps a rolling modular state and
advances sequential `p` values incrementally, avoiding full modular
exponentiation per call and avoiding a large lookup table.

## Optimization 3: Memory-budgeted (k, l) tuning

The wrapper can tune `(k, l)` under a configured memory budget:

- `chiavdf_set_bucket_memory_budget_bytes(...)`

If no tuned candidate is found, the code falls back to the standard parameter
heuristics.

## Operational Notes

- The `fast_wrapper` code path sets one-wesolowski mode and uses `quiet_mode` to
  avoid unsolicited stdout noise when embedded in multi-worker clients.
- Thread-slot assignment for the fast VDF counters is per-thread via
  `vdf_fast_pairindex()`, avoiding slot collisions when multiple VDF computations
  run in one process.
- The production default for `enable_threads` in `parameters.h` is unchanged from
  upstream to preserve timelord expectations.

