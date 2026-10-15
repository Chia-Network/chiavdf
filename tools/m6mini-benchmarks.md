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


## Second round: profiling, lifetimes and ASIC endpoints (2026-10-12)

This round used board 12305 with **three engines on one ASIC**, 1540 MHz,
1.0 V requested, four auxiliary threads and three proof threads per engine,
and the 500K checkpoint threshold. The synthetic harness loaded the Homebrew
GMP bottle; the live capture used native GMP DIV2=1. The paired baseline
was the deployed remainder-cache client, not the original pre-optimization
client in the earlier table. These are synthetic challenge-to-complete-proof
IPS measurements; they are not mainnet Grafana averages.

`--proof-profile` records monotonic stage timing, challenge inputs, request
arrivals, natural resets, complete-proof sends, worker CPU time and sampled
CPU numbers. `tools/m6mini_profile_summary.py` summarizes the logs and exports
a client-observed request trace with `--trace-output`. Trace export requires
one continuous log so job IDs cannot collide between processes. Sampling at
form-access boundaries does not measure time-weighted residency or pin threads.

Three alternating profiling runs with one versus three active engines found:

| Final segment at requested target | One engine construction | Three engines construction | Three engines initial value wait | Three engines verification |
| --- | ---: | ---: | ---: | ---: |
| 1M (252K segment) | 184.3 ms | 309.2 ms | 1.6 ms | 3.4 ms |
| 4M (188K segment) | 156.4 ms | 223.5 ms | <0.1 ms | 4.4 ms |
| 20M (348K segment) | 232.4 ms | 366.3 ms | 76.9 ms | 2.9 ms |

Median worker CPU/wall ratios were 0.963 for one engine and 0.744 for three.
The three-engine run sampled every CPU, including efficiency cores. These
measurements support investigating arithmetic and scheduling contention,
rather than expecting millisecond hash/verification savings to remove
hundreds of milliseconds of construction. Unsplit 4K/8K/16K proofs took
about 32/38/38 ms even with one active engine: shrinking the tail to a few
thousand iterations does not make construction take a few milliseconds.

The patch fixes proof delivery to copy the full checkpoint chain under the
publication mutex. Serialization then uses owned bytes. Auxiliary workers
are joined before slot reuse and challenge cleanup, with proof accounting
finished before the busy flag is released. The intermediate directory has
fixed size per challenge and publishes lazily allocated blocks atomically;
consumers do not race a resizing vector or acquire its allocation mutex on
the usual path. The focused ThreadSanitizer test exercises publication,
cancellation, worker draining, concurrent checkpoint snapshots and concurrent
block allocation.

`--retain-asic-values` is an opt-in, bounded cache of 64 recent structurally
checked ASIC endpoints. It can replace the final software catch-up from the
4000-iteration grid with a newer endpoint at or below the target. Values
own their GMP storage, and the cache is cleared on challenge reset, cleanup
and internal hardware restart. A missed/expired sample falls back to the
grid. Every completed segment is still verified before publication; wire
verification and incorrect-iteration negative controls remain independent.

QoS changes that boosted proof workers or lowered intermediate recomputation
priority were rejected: gains at 1M concealed large regressions at longer
targets. Background recomputation lost 23–29% at 2M–8M in the final balanced
comparison. Boosting both producer and proof workers also slowed longer
requests. Reusing the prover's hash challenge inside the verifier showed
mixed sub-percent changes and was removed. None of these changes bypasses
proof verification.

Final paired results, validation counts, rejected experiment samples and
live request-trace observations are in `m6mini-round2-results.json`. The
original live binary and launch settings are preserved.

The final candidate (lifetime fixes plus the opt-in endpoint cache, default
worker priority) versus the deployed client measured:

| Target/workload | Deployed IPS | Candidate IPS | Change |
| --- | ---: | ---: | ---: |
| 1M regular | 754,178 | 748,340 | -0.77% |
| 4M regular | 889,202 | 895,576 | +0.72% |
| 20M regular | 974,113 | 973,662 | -0.05% |
| 250,007 irregular | 549,619 | 557,492 | +1.43% |
| 1,000,123 irregular | 731,820 | 752,737 | +2.86% |
| 20,009,999 irregular | 964,305 | 963,947 | -0.04% |

An isolated cache-versus-current-source comparison also improved 250,007 by
1.48% and 1,000,123 by 1.69%. The regular workload is broadly unchanged;
the cache is a modest optional off-grid improvement, not a solution to 1M
delivered IPS. In a profiled holdout it reduced median endpoint catch-up at
250,007 from the required 2,007 grid iterations to 343, saving roughly 5 ms.
At 1,000,123 the profiled median remained 123 iterations, so its larger IPS
gain cannot be attributed to endpoint catch-up alone. The cache remains
disabled by default.

The 180-second mainnet capture exported 24 naturally reset challenge jobs,
181 positive iteration requests, and two jobs with no positive request.
Observed reset times ranged from 8.37 to 36.84 seconds, with a 19.78-second
median. `m6mini-round2-mainnet-trace.json` retains challenge inputs and request
arrival times for the next replay benchmark. It excludes the final
guard-induced cancellation and does not identify CC/RC/ICC chain types.

The final intermediate-only priority repeat gained 1.01% at regular 1M but
lost 1.08% at 4M; it was rejected as inconsistent and all QoS controls were
removed from the cleaned patch. The live-profile median worker CPU/wall
ratio was 0.970, versus 0.744 in the simultaneous synthetic three-engine
workload. Real request timing needs to be part of future optimization
decisions. This short capture saw considerably less concurrent CPU pressure
than simultaneous synthetic requests; it does not establish long-term
scheduling behavior.

Across the round, 88 completed synthetic runs independently verified
1,688 complete wire proofs and their incorrect-iteration negative controls.
Every completed run restored the board and verified its restoration proof;
the maximum observed completed-run temperature was 46.92 C. The cleaned
binary passed another 27 boundary, three cancellation and 15 staggered
receipts. The fresh Python extension passed all eight regressions.

One rejected-QoS staggered run was deliberately interrupted and is excluded
from the completed-run counts. An earlier controller cleanup misidentified
a zombie guard process; the board had already restored successfully, and
the original live client was manually resumed at 10:35:01. The final
controllers check process state and restored the original guarded client.
The cleaned candidate stays in the isolated round-two checkout for review;
the deployed original client is active with its previous launch settings.


A round-three loader audit corrected the GMP designation of round-two
synthetic measurements: macOS strips `DYLD_LIBRARY_PATH` through system
Python and system shell launchers. Those wrappers did not re-export it before
executing the client. Their baseline and candidate used the same Homebrew GMP
bottle, so their relative comparisons remain comparisons of that configuration.
The original live guard sets the environment immediately before directly
launching the executable and uses native GMP, as confirmed from its process
mappings. Native repeat measurements with explicit loader checks follow below.

## Third round: captured requests and zero-weight buckets (2026-10-12)

This round compares the cleaned round-two source with the same source plus
one arithmetic change in `ParallelProver::ProvePart`. Both clients enable
`--retain-asic-values`, use the 500K checkpoint threshold, and leave profiling
off during comparisons. Board 12305 runs **three engines on one ASIC** at
1540 MHz / 1.0 V requested with the same worker counts. Initial hardware
comparisons used Homebrew GMP; the corrected native cohort explicitly verifies
DIV2=1 library loading before testing. The construction microbenchmark and CPU
profile used native GMP. The deployed live binary is a separate, preserved
reference; these comparisons
isolate the new change against the round-two source.

Each bucket's exponent is its high digit times `2^k0` plus its low digit.
Bucket zero contributes the identity, so collecting its product is unnecessary.
Likewise, the outer high-zero and low-zero groups are raised to exponent zero;
building those products cannot affect the result. The optimized parallel prover
omits those operations. Nonzero groups still include inner zero digits where
needed. The serial prover remains a reference, cancellation checks remain in
the bucket-collection loop, and completed segments are still verified before
publication. The default bucket-selection policy is unchanged.

`tools/m6mini_prover_bench.cpp` isolates the actual two-worker parallel prover
on precomputed reduced forms at the hardware's 4000-iteration spacing. It
excludes fixture generation and final verification from construction timing.
`prepare D INITIAL_HEX MAX_ITERS FILE` generates a fixture; `run FILE REPS
LENGTHS K_VALUES` emits JSON lines. A requested k of zero selects the current
hardware heuristic. Every result verifies against the Wesolowski verifier and
checks that different bucket parameters produce identical canonical proofs.
A screen of k=1,2,4,5,8 across ten lengths found the existing heuristic fastest
at every tested length in the original implementation. The zero-weight change
alters some short-length crossovers; no new selection policy is retained.

`tools/m6mini_replay.py` replays the captured round-two trace, retaining each
challenge's discriminant and initial form, positive request arrivals, natural
resets and logical-stream overlap. The protocol does not identify physical
engines: reconnecting engines receive queued jobs in accept order. Receipts
are timed from challenge send, independently verified after the run using the
captured initial form, and compared by `(job, iterations)`. Future targets
cancelled by a recorded reset are expected. This is a controlled replay metric,
not Grafana's mainnet chain-mapping clock or a 24-hour stability measurement.

On the prepared M6 checkout, run with exclusive board access:

```sh
caffeinate -is python3 tools/m6mini_replay.py \
  --trace tools/m6mini-round2-mainnet-trace.json \
  --client ./src/hw_vdf_client --first-job 7 --jobs 6 \
  --gmp-lib-dir /Users/cameron/Code/chiavdf-m6/results/optimization-sweep/gmp/div2-native/lib \
  --checkpoint-threshold 500000 --retain-asic-values \
  --output results/replay-holdout.json
```

Pass the executable directly with `--gmp-lib-dir`. The harness sets the
library path after the system Python launcher, verifies the actual loader
paths with `--version`, and records the probe. Both harnesses hold a macOS
sleep assertion. The replay checks
55 C as the stop threshold, and restores 1100 MHz / 0.88 V with a successful
proof after every hardware run. Paired tests must keep binary, GMP and board
settings identical except for the candidate change. The independent wire
verifier accepts an optional initial-form column while preserving its original
two-column generator form input.

A 15-second macOS `sample` profile at 1 ms intervals of repeated 348K/k=4
construction found `__gmpn_hgcd2` and `mpz_xgcd_partial` to be the largest flat
stack entries (8,376 and 4,090 samples). Multiplication helpers followed at
much smaller counts. These are sampled-stack rankings, not exact runtime
percentages. This identifies extended GCD as the next arithmetic target; it
does not establish a speedup from replacing GMP. Instruments was unavailable
because this host has the command-line developer tools only.


Three alternating pairs on one precomputed captured-input fixture measured:

| Segment iterations | Reference construction | Candidate construction | Time reduction |
| --- | ---: | ---: | ---: |
| 4K | 17.93 ms | 16.10 ms | 10.19% |
| 8K | 22.69 ms | 20.09 ms | 11.50% |
| 16K | 29.58 ms | 24.82 ms | 16.10% |
| 48K | 58.24 ms | 44.84 ms | 23.00% |
| 66K | 67.77 ms | 61.52 ms | 9.22% |
| 184K | 120.86 ms | 108.16 ms | 10.50% |
| 250K | 145.92 ms | 131.81 ms | 9.67% |
| 348K | 183.93 ms | 166.37 ms | 9.55% |
| 1M | 382.75 ms | 362.48 ms | 5.30% |
| 4M | 1081.05 ms | 1051.68 ms | 2.72% |

These are construction times with two workers and free CPU capacity, not
per-engine delivery IPS under simultaneous hardware requests. The greater
short-segment saving reflects eliminating fixed bucket work as well as zero
digit collection. A benchmark's first construction includes cold-start effects;
raw samples are retained and the table uses three-sample medians.


The corrected native-GMP cohort used three alternating pairs per workload,
with actual native library paths verified from dynamic-loader output. Medians
for synthetic targets cover nine receipts per version (three engines times
three runs):

| Target/workload | Round-two reference IPS | Zero-weight candidate IPS | Change |
| --- | ---: | ---: | ---: |
| 1,000,000 balanced | 771,961 | 790,931 | +2.46% |
| 2,000,000 balanced | 856,688 | 864,200 | +0.88% |
| 4,000,000 balanced | 911,415 | 929,787 | +2.02% |
| 8,000,000 balanced | 951,285 | 954,804 | +0.37% |
| 12,000,000 balanced | 967,111 | 969,477 | +0.24% |
| 16,000,000 balanced | 974,044 | 975,538 | +0.15% |
| 20,000,000 balanced | 979,250 | 982,386 | +0.32% |
| 100,003 irregular | 481,332 | 498,216 | +3.51% |
| 250,007 irregular | 573,419 | 602,609 | +5.09% |
| 1,000,123 irregular | 766,173 | 791,291 | +3.28% |
| 2,000,333 irregular | 853,753 | 869,012 | +1.79% |
| 4,000,777 irregular | 911,628 | 925,387 | +1.51% |
| 8,000,111 irregular | 950,265 | 955,698 | +0.57% |
| 20,009,999 irregular | 973,470 | 975,278 | +0.19% |

The captured-mainnet holdout (jobs 7–12) delivered the same 15 target proofs
per version per run, all before their recorded resets. Its three pairs saved
a median 21.31 ms over matching receipts; the geometric mean of paired IPS
ratios improved 0.165%. Individual target medians can vary in either direction;
the ledger retains every paired receipt and missing/late-delivery counts.
The initial Homebrew-GMP capture and synthetic cohorts remain separately
labeled in `m6mini-round3-results.json`; their absolute rates should not be
substituted for the native cohort. Neither cohort measures Grafana mainnet
IPS or network win percentage.

Across both library cohorts, 50 accepted hardware runs independently verified
872 complete wire proofs, including incorrect-iteration negative controls.
Native-GMP runs account for 23 runs and 425 receipts. Every accepted hardware
run restored the board and verified its restoration proof. Maximum observed
accepted-run temperature was 48.63 C. The construction harness verified 390
completed microproofs; candidate proofs exactly matched the frozen reference
across bucket parameters, including lengths 1,2,8,32,264,1000 and grid/off-grid
boundaries on a second captured discriminant and non-generator initial form.
The fresh Python extension passed eight regressions and the focused
ThreadSanitizer test passed. The extended independent verifier also accepted
legacy and explicit generator inputs, accepted the captured initial form, and
rejected using a different initial form.

Three excluded cases are preserved in the result ledger: the initial replay
smoke had an idle EOF during shutdown despite ten verified receipts; the first
repeat pair lost sleep prevention when the live guard was paused; and one
Homebrew-GMP simultaneous-request trial reported a bad ASIC value at iteration
2,185,100 and restarted an engine at 2,056,000. All 21 complete proofs from that
trial verified, but its timing and its paired reference were excluded and the
pair reran cleanly. The raw-value event's cause is not established. It did not
recur in the native repeats, which does not establish long-term hardware
stability. Idle shutdown handling and sleep prevention are now fixed. Native
library selection fails before hardware if its loader probe does not match;
both direct-executable harness paths and missing-library negative controls were
tested on the M6.

The arithmetic change is retained in the isolated worktree. It improves proof
construction and controlled proof delivery, but does not reach 1M delivered IPS
at the present 1540 MHz operating point. The original live binary and guard were
restored, their original binary hash and native GMP mappings were confirmed,
and live proof delivery resumed. No candidate deployment or mainnet IPS claim
is implied by the benchmark. The next arithmetic investigation should measure
actual extended-GCD operands and wrapper/kernel costs before considering a
specialized fixed-size implementation; short-segment bucket-policy calibration
and reset/deadline scheduling remain further experiments, not retained changes.

## Fourth round: partial-GCD allocation reuse (2026-10-12)

This round compares the retained round-three zero-weight-bucket implementation
with two buffer swaps in `mpz_xgcd_partial`. After each matrix application, the
old remainder/cofactor has already been consumed. Swapping the temporary into
the result transfers its limb allocation instead of copying the integer; the
old result allocation becomes scratch for the next multiplication. All sign
handling, stopping conditions, and numerical operations remain unchanged.

The baseline and candidate use native GMP 6.3.0 with the existing DIV2 method 1.
The mainnet guard was drained during measurement, sleep prevention stayed
active, and every hardware run verified its actual GMP mappings before launch.
Hardware comparisons run **three engines on one board**, location 12305, at
1540 MHz / 1.0 V requested, with checkpoint threshold 500,000 and the retained
endpoint cache enabled. These are controlled receipt timings, not Grafana
`chia_timelord_estimated_ips` or network win measurements.

Pure construction used eight alternating pairs, with two repetitions per
length per version. Every measured pair favored the swaps at every length:

| Iterations | Reference construction ms | Swaps construction ms | Change |
| --- | ---: | ---: | ---: |
| 16,000 | 25.100 | 24.825 | -1.10% |
| 64,000 | 62.126 | 61.683 | -0.71% |
| 184,000 | 108.388 | 107.606 | -0.72% |
| 348,000 | 167.210 | 165.749 | -0.87% |
| 1,000,000 | 362.675 | 359.740 | -0.81% |
| 4,000,000 | 1057.491 | 1047.259 | -0.97% |

The standalone mixed-size partial-GCD corpus improved 2.60%. It contains
31,144 distinct inputs: randomized inputs, zero/equal remainders, non-coprime
cases, stopping-bound edges, power-of-two boundaries, and Fibonacci pairs.
Four threads compared every output remainder and cofactor exactly with the
frozen reference, totaling 124,576 comparisons. AddressSanitizer and
UndefinedBehaviorSanitizer passed the same four-thread corpus.

Three alternating hardware pairs per workload produced the following medians
(nine receipts per synthetic target per version). The delivered effect is
small and mixed; long targets receive little benefit from faster construction:

| Workload / target | Reference IPS | Swaps IPS | Change |
| --- | ---: | ---: | ---: |
| balanced / 1,000,000 | 795,076 | 798,704 | +0.456% |
| balanced / 2,000,000 | 865,821 | 871,041 | +0.603% |
| balanced / 4,000,000 | 926,724 | 927,835 | +0.120% |
| balanced / 8,000,000 | 953,810 | 959,350 | +0.581% |
| balanced / 12,000,000 | 971,990 | 970,862 | -0.116% |
| balanced / 16,000,000 | 977,905 | 976,715 | -0.122% |
| balanced / 20,000,000 | 981,840 | 982,199 | +0.037% |
| irregular / 100,003 | 506,072 | 503,085 | -0.590% |
| irregular / 250,007 | 601,659 | 603,837 | +0.362% |
| irregular / 1,000,123 | 779,411 | 792,628 | +1.696% |
| irregular / 2,000,333 | 872,671 | 874,769 | +0.240% |
| irregular / 4,000,777 | 924,567 | 922,946 | -0.175% |
| irregular / 8,000,111 | 956,847 | 958,670 | +0.190% |
| irregular / 20,009,999 | 975,197 | 975,237 | +0.004% |

The jobs 7–12 mainnet replay delivered the same 15 target proofs per version
per run, all before their recorded resets. Matching receipts saved a median
5.63 ms; the geometric mean of paired IPS ratios improved 0.064%. There were
no missing or late matching receipts in the accepted pairs. This does not
establish a comparable increase in live mainnet IPS.

Across 18 accepted hardware runs, the independent verifier checked 342 complete
wire proofs and incorrect-iteration negative controls. Each run also verified
its restoration proof. Maximum accepted-run temperature was 48.21 C. Across
construction, profiling and GMP screens, 452 completed microproofs verified
and matched the frozen canonical reference, including a second captured
discriminant/non-generator initial form and short/grid/off-grid cases. A fresh
Python extension passed all eight regressions (142.65 s), and the hardware
worker/snapshot regression executable passed.

Two attempted timing runs are excluded and their raw evidence is preserved:

- The irregular reference run reported a bad raw ASIC value at iteration
  11,164,486 and restarted at 11,160,000; all 21 complete wire proofs verified.
- A candidate replay reported a bad raw ASIC value at iteration 4,613,667 and
  restarted at 4,608,000; all 15 complete wire proofs verified.

Both affected pairs were repeated cleanly. Thus all 378 completed wire proofs
from accepted and excluded runs verified, but the timing summary uses only
342 receipts from runs without recovery. The recovery cause is not established;
it occurred in both versions and means 1540 MHz / 1.0 V has not demonstrated
sustained stability. Repeating affected pairs does not erase those events.

Benchmark-only `m6mini_gcd_profile.hpp` now measures actual operands and full
versus partial GCD costs. The refined run excludes fixture preparation, tail
squaring and verification. Most operands are 8–9 limbs, with a four-limb
partial-GCD stopping bound. Full GCD accounts for 62.4% of the measured GCD
clock time and partial GCD for 37.6%; these fractions include clock-call
overhead and are not percentages of total proof runtime. Use uninstrumented
builds for performance comparisons.

Two further candidates were screened without changing production:

- A quotient-one shortcut in the partial-GCD loop was 10.04% slower on the
  local Apple Silicon mixed-size corpus. That source change was not retained;
  this was a local screen, not an M6 result.
- Private GMP copies rebuilt the HGCD2 kernel with DIV1 methods 1 and 5,
  retaining DIV2 method 1. Five alternating comparisons verified 120 matching
  proofs. Differences from the current DIV1 method 3 stayed below 0.4%, with
  no common winner across lengths. The current library is retained. These
  private copies were only screened; they were not deployed or represented
  as having passed the full GMP test suite.

Reproduce the corpus with a frozen pre-change `xgcd_partial.c`:

```sh
c++ -O3 -flto -std=c++17 -Isrc -I/opt/homebrew/include \
  '-DCHIAVDF_XGCD_REFERENCE_HEADER="/path/to/frozen/xgcd_partial.c"' \
  tools/m6mini_xgcd_bench.cpp -L/opt/homebrew/lib -lgmpxx -lgmp -pthread \
  -o /tmp/m6mini_xgcd_bench
DYLD_LIBRARY_PATH=/path/to/native/gmp/lib /tmp/m6mini_xgcd_bench
```

For construction-only GCD instrumentation, add
`-include tools/m6mini_gcd_profile.hpp` to the existing construction benchmark
build. It writes per-thread JSON summaries to stderr and proof receipts to
stdout. Instrumentation is absent from normal builds.

`m6mini-round4-results.json` retains all paired receipt timings, proof hashes,
library/source provenance, arithmetic samples, exclusions, and raw artifact
locations. The original live client was restored; its original binary hash
and native GMP mappings were confirmed and live proof delivery resumed.
The swap patch remains uncommitted in the isolated worktree. The tuning
screens leave further kernel and bucket/deadline experiments open, but this
round does not reach 1M delivered IPS at the existing hardware operating point.

## Fifth round: live stability, reset work and checkpoint deadlines (2026-10-13)

The unchanged production client logged 62 raw-value recoveries over 19.14 hours
at 1540 MHz / 1.0 V, all on physical VDF engine 0. No invalid segment proofs
were reported. The AMD MSP clients also reported only engine-0 bad values in
the preceding 24 hours: 12 on host 01, 8 on host 02, and 2 on host 03. Their
frequencies auto-adjust, so these counts are not a controlled hardware or OS
comparison. They do establish that the symptom predates these proof changes
and is not limited to macOS.

Four clock trials used the original production binary with three engines on
one board, rotating the synthetic discriminants between connections. At 1.0 V,
two 120M-iteration trials at 1500 MHz had no recovery; two at 1540 MHz had one
recovery between them. All 72 complete wire proofs and four restoration proofs
verified. The recovering trial is preserved and excluded from timing. These
short trials do not certify a stable clock.

Engine 0 uses a combined telemetry/status burst alias; engines 1 and 2 use
per-engine status registers. `CHIAVDF_HW_DIRECT_STATUS=1` allows a diagnostic
comparison using the latter path for engine 0, while keeping temperature reads.
Two rotated trials per path at 1540 MHz, with 180M iterations per engine,
verified 36 complete wire proofs without recovery in either mode. Both paths
also passed emulator checks. The default read path is unchanged: these results
do not establish a causal explanation or justify changing it.

Two alternating mainnet pairs used 1500 MHz / 1.0 V and a 500K checkpoint
threshold, with 30 seconds of warmup followed by 180 seconds of measurement.
The baseline was the original production binary; the candidate used the retained
proof changes, endpoint cache and opt-in profiling. The local timelord reported
187 proof completions across these measurement windows and no invalid proofs of
time. Neither client reported a recovery or invalid segment in its full phase
log. Maximum observed phase temperature was 46.70 C.

| Window | Completion samples | Mean completion IPS | Median completion IPS |
| --- | ---: | ---: | ---: |
| Baseline 0 | 45 | 810,045 | 888,412 |
| Candidate 0 | 50 | 861,117 | 921,042 |
| Candidate 1 | 49 | 913,038 | 945,801 |
| Baseline 1 | 43 | 903,454 | 936,865 |

These windows have different proof-length distributions, and the candidate
has profiling overhead. Completion-event means are not time-sampled Grafana
gauge averages. This is live protocol validation, not evidence of a precise
steady-state speed increase. For context only, Grafana's 24-hour gauge averages
were approximately 930,971 IPS for MSP primary and 931,881 IPS for ZRH primary;
those should not be directly compared with the table's completion means.

The reset-work investigation exposed a profiling gap: worker CPU totals were
logged only for finished constructions. The instrumentation now emits
`completed=0` for canceled constructions and `completed=1` for finished ones.
Legacy logs explicitly report incomplete cancellation coverage instead of a
zero unfinished-CPU fraction. A separate 180-second mainnet capture contained
42 natural reset jobs and 378 relevant worker records. Of 295.97 seconds of
measured construction-worker CPU, 21.71 seconds (7.34%) belonged to unfinished
constructions. This is a lower bound on discarded work; it excludes tail
squaring and completed but unused checkpoints.

The emulator cancellation regression also exposed a harness shutdown race:
proof bytes already queued on a socket can precede STOP. The harness now drains
and independently verifies them, storing them separately from timed receipts.
The regression verified three timed and two shutdown proofs, emitted ten
canceled-worker records, and verified its emulator restoration proof. Hardware
worker/snapshot regressions passed again. The harness accepts an explicit
`--restore-tool` for emulator runs and preflights its existence before launch.

Checkpoint spacing was re-screened against the new captured trace at 1500 MHz.
Three alternating pairs compared 500K and 250K on jobs 1–6: all 60 complete wire
proofs verified and arrived before reset. Matching receipts saved a median
6.41 ms; their geometric mean IPS ratio improved 1.188%. On the untouched jobs
7–12 slice, all 17 delivered proofs verified. Matching receipts changed only
0.0043%, but 250K delivered four additional proofs before reset across two pairs,
plus one additional late proof. The third pair delivered no extra proofs.
Thus 250K is a candidate for testing deadline coverage, not a universally faster
or established default. The library/client default remains 1M; the existing
M6 baseline setting remains 500K.

A bounded longer mainnet comparison now runs six hours with the frozen original
binary at 500K, then six hours with the frozen optimized binary at 250K. Both
use three engines on board 12305 at 1500 MHz / 1.0 V, native GMP, and the existing
55 C guard. Candidate profiling and endpoint retention are enabled. Results
are pending; no sustained-stability or 1M delivered-IPS claim is made. The
controller handles termination by draining its phase guard and restoring the
original live binary at 1500 MHz. The original 1540 MHz guard source is retained
in the experiment directory. Active-controller metadata is also written beside
the usual live PID files so the experiment can be identified and stopped.

Raw remote artifacts live under `results/round5-{stability,readpath,live,
reset-profile,checkpoints,checkpoint-holdout,soak}` in the prepared M6 checkout.
`m6mini-round5-results.json` retains the completed evidence and pending soak
status; `m6mini-round5-mainnet-trace.json` records the fresh natural-reset inputs.
The retained code and evidence are packaged in draft chiavdf PR #425. The
user-owned optimization-ideas file remains untracked and untouched.
