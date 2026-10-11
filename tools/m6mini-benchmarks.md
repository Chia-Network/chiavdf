# M6 proof-delivery benchmarks

Run these tools from the prepared M6 checkout. They exercise three engines on
FTDI location **12305**, at **1540 MHz and 1.0 V**, using synthetic test
discriminants and a loopback timelord. The temperature stop is 55 C. These are
prototype test settings, not a device rating. Every run restores the board to
1100 MHz and 0.88 V and requires a successful restoration proof.

Build the hardware client with `make -C src -f Makefile.vdf-client hw_vdf_client
hw_test`. Build `src/verify_delivery` using the command at the top of
`m6mini_verify_delivery.cpp`.

Example alternating comparison:

```sh
caffeinate -is python3 tools/m6mini_compare.py \
  --baseline ./baseline-wrapper --candidate ./candidate-wrapper \
  --rounds 3 --scenario balanced --output results/comparison
```

Wrappers must forward all arguments. Keep real client executables in `src/` so
their executable-relative USB library lookup continues to work. Wrappers can
select private GMP libraries through `DYLD_LIBRARY_PATH` and pass client options.
The comparison preflights both executables with `--version` before touching the
board. `--baseline-threads` and `--candidate-threads` default to four auxiliary
threads and set proof threads to one fewer per engine.

The client option `--checkpoint-threshold 500000` improved regular 1M/2M/4M
delivery in the repeated M6 tests. Irregular requests were mostly unchanged,
with a small loss at 100K, so the client default remains 1000000. The option
accepts 8000 through 4294967295 iterations; its minimum prevents checkpoints
rounding to zero. See `m6mini-proof-parameters-results.json` for both workloads,
rejected parameter screens, correctness checks and individual samples.

`balanced` requests 1, 2, 4, 8, 12, 16 and 20 million iterations simultaneously
on all three chains. `irregular` includes short requests and iterations not
aligned to ASIC value spacing. `staggered` starts challenges at different times.
`boundaries` checks requests around the 4000-iteration value spacing and the
minimum configurable checkpoint threshold. It is a correctness workload.
`m6mini_proof_delivery.py --stop-after 1 --output results/cancel.json` exercises
cancellation while later requests remain queued.

Delivery time starts before the challenge is sent and ends after the complete
proof arrives. Verification happens after timing: every serialized proof must
verify, and the same proof with an incorrect iteration count must fail. Runs
also require clean client shutdown, no hardware restarts or invalid segment
reports, and a successful restoration proof. JSON records retain proof bytes,
logs, temperatures and individual timing samples; comparison summaries report
medians and geometric means.

These measurements are not `chia_timelord_estimated_ips` observations from
mainnet and do not establish 24-hour stability. Use alternating repeated pairs,
then a different request scenario, before retaining a performance change.

## Retained proof-construction changes

The measured changes are thread-local NUCOMP scratch storage, opt-in native
GMP with two-limb division method 1, publication wakeups for proof readiness,
the configurable checkpoint threshold, and per-worker modular remainder reuse.
The latter preserves the stateless bucket calculation as a reference and avoids
repeating modular exponentiation for consecutive buckets. Its three alternating
pairs improved regular delivery by 4.94% at 1M iterations and 0.86% at 20M;
the irregular holdout improved every target by 0.77–4.10%. All 252 comparison
proofs, 27 boundary proofs and three cancellation receipts verified. The serial
100K proof smoke test and all 28 regressions passed. See
`m6mini-remainder-cache-results.json` for individual samples.

Worker pools, deadline priority, fixed partition sizes, changed value spacing,
smaller helper counts, explicit Apple M6 compiler targeting, PGO, bucket-vector
reuse and allocation-free bucket extraction did not provide consistent useful
proof-delivery improvements and were removed. Their experiment results remain
as evidence; they are not production build options. The older stashed ARM work
was reviewed without applying it. It targets the software VDF fixed-limb path,
and contains a borrow-overflow issue that needs fixing before any reuse.

## Final combined comparison

The retained stack was compared with the preserved original client in three
alternating pairs on each workload, using one ASIC with all three engines.
The table reports medians across nine receipts per version and target; it is
per-engine proof delivery, not the sum of three engines.

| Iterations | Original IPS | Final IPS | Median change |
| --- | ---: | ---: | ---: |
| 1M | 545,509 | 772,322 | +41.58% |
| 2M | 668,697 | 853,392 | +27.62% |
| 4M | 810,407 | 915,740 | +13.00% |
| 8M | 882,660 | 950,495 | +7.69% |
| 12M | 921,426 | 968,131 | +5.07% |
| 16M | 939,147 | 974,830 | +3.80% |
| 20M | 950,267 | 980,523 | +3.18% |

Every irregular holdout target also improved. All 252 final comparison proofs
verified, all runs shut down cleanly and restored the board, and the maximum
observed temperature was 48.21 C. Per-engine medians at 20M were 980523,
980076 and 980815 IPS. These results remain below 1M proof-delivery IPS at
1540 MHz and do not establish a mainnet or 24-hour average. See
`m6mini-final-proof-results.json` for samples and verification metadata.

Each retained performance change has its own commit: `95ed39f`, `295d4b5`,
`a5c23fe`, `293d7d5`, `f92efa0`, and `21ada1a`. The independent benchmark
infrastructure is in `3966303`.
