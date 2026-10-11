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

`balanced` requests 1, 2, 4, 8, 12, 16 and 20 million iterations simultaneously
on all three chains. `irregular` includes short requests and iterations not
aligned to ASIC value spacing. `staggered` starts challenges at different times.
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
