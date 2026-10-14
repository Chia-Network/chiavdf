# M6 USB and delivery candidates — 2026-10-13

No candidate produced a useful, repeatable delivered-IPS improvement across
both request workloads. The live service was restored to three ASIC boards
(USB locations 12305, 8241 and 8257), one physical engine per board, at
1620 MHz and 1.0 V requested. The ASIC clock and the SPI transport clock are
different settings; the normal SPI clock remains 10 MHz.

| Candidate | Balanced median paired IPS change | Irregular holdout | Decision |
| --- | ---: | ---: | --- |
| Nonblocking current ADC telemetry | +0.006% | −1.497% | Remove experimental implementation |
| SPI transport: 10 → 20 MHz | +0.191% | −0.310% | Keep 10 MHz |
| FT4222 1.4.4.221 and bundled D2XX 1.4.30 | −0.159% | Not advanced | Keep existing library stack |
| SPI transport: 40 MHz | Initialization failed; no proofs produced | Not advanced | Reject this setting |

Each percentage is the median of three pair-level medians over matched
chain/target receipts. Trial order alternated and discriminants rotated across
boards. Balanced targets were 1M, 2M, 4M, 8M, 12M, 16M and 20M iterations.
The independent irregular workload included 100003, 250007, 1000123, 2000333,
4000777, 8000111 and 20009999 iterations. These are synthetic
challenge-to-complete-proof receipt measurements, not mainnet Grafana averages.
Independent chains' IPS must not be added together.

All **690 delivered hardware proofs** from accepted trials verified, including
tampered iteration-count negative controls. Accepted trials had no recoveries
or invalid delivered proofs; every board restoration succeeded. The highest
observed temperature was **44.1°C**, below the 55°C experimental stop.
Voltage stayed at 1.0 V requested. Four auxiliary threads and three proof
threads per engine, a 250K checkpoint threshold, retained ASIC values,
native GMP DIV2=1, profiling and current telemetry were used throughout.

## Complete-chain readiness

The useful retained change is diagnostic: `--proof-profile` now distinguishes
final-segment publication, full-chain readiness, retrieval and completion of
application write calls. Full-chain readiness is the latest publication of the
final segment and every required checkpoint. All marker-order checks passed.

In the balanced baseline, final-segment publication to full-chain readiness
took **47.416 ms median** and **151.766 ms p90**. Full-chain readiness to
retrieval took **32 µs median**, **213 µs p90** and 12.707 ms maximum.
Retrieval to completed write calls took 75 µs median. The previous long
publication-to-send tail was predominantly checkpoint construction. These
measurements do not justify a separate sender thread.

State preparation took 74 µs median; the subsequent ASIC job-start call took
410 µs median. The newer stack reduced the latter to 381 µs, while 20 MHz SPI
took 421 µs. This startup cost is already small compared with construction.

Nonblocking telemetry removes the explicit 10 ms ADC sleep while preserving
conversion time and calibration. Real polling continued during conversion;
no early ADC reads were recorded. Its balanced retrieval maximum was 0.859 ms,
but the irregular holdout still reached 17.116 ms. Removing one deliberate
sleep did not remove scheduler, polling or other delivery delays and did not
produce a consistent throughput gain.

Application write completion does not measure wire arrival. The synthetic
server times receipt of the entire serialized proof independently.

## Reproduction and evidence

[Results](m6mini-usb-candidates-results.json) retain individual trial summaries,
paired measurements, stage distributions, loader/binary hashes and the final
live configuration. Complete proof bytes and raw logs remain on `m6mini` under
`/Users/cameron/Code/chiavdf-m6-round2/results/round7-usb-candidates`,
`round7b-usb-candidates` and `round7c-usb-holdout`.

The experimental driver changes are preserved in
[m6mini-usb-candidates.patch](m6mini-usb-candidates.patch), rather than in the
normal build. Apply it to a disposable checkout and force a rebuild with
`make -B`: the hardware Makefile does not track all header dependencies.
Do not run a benchmark while live clients own the same boards.

[m6mini_multi_board_client.py](m6mini_multi_board_client.py) accepts a JSON
configuration containing `binary`, `boards`, `gmp_lib_dir`, `restore_tool` and
optional `environment`. An executable wrapper calls its `main(config, argv)`
and is passed to `m6mini_proof_delivery.py --client` with three engines.
The wrapper launches one client per board, serializes FTDI discovery, prefixes
logs with board identity, and restores every board sequentially after all
clients stop. This avoids the observed macOS concurrent-enumeration race.
[m6mini_usb_summary.py](m6mini_usb_summary.py) keeps board/job identities
separate when matching timing markers.

Use `CHIAVDF_HW_ASYNC_TELEMETRY=1` or `CHIAVDF_HW_SPI_MHZ=20` only in the
experimental configuration. Normal trials use neither. The newer FTDI trial
loaded private copies of both vendor libraries from the `ft4222` 1.13.0 source
distribution; this was a paired-stack comparison. No vendor binaries or
installed system drivers were changed.

Benchmark pauses, restoration gaps and build activity are excluded from the
mainnet regional-comparison capture. The collector continued recording raw
events throughout; its held gauge during pauses is not a valid performance
sample for a running hardware phase.
