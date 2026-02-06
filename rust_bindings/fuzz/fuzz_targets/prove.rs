#![no_main]

use chiavdf::prove;
use libfuzzer_sys::{fuzz_target, Corpus};

// Fuzzing `prove()` with unbounded `iters` can explode memory usage and runtime.
// The cost of the underlying VDF prover is at least linear in `iters`, and in
// practice can become superlinear due to internal allocation patterns. We have
// observed OOM (exit 137) in CI when `iters` is allowed to reach the full `u16`
// range, so we cap it to keep fuzzing stable and high-throughput.
//
// Why 4096:
// - Large enough to exercise multiple loop iterations and proof paths beyond
//   "toy" counts, preserving meaningful coverage.
// - Small enough to keep inputs fast and avoid pathological allocations across
//   typical CI memory limits.
// - Selected empirically as a conservative upper bound given prior OOMs; it can
//   be raised later if measurements show steady memory and acceptable exec/s.
//
// If you want deeper iteration coverage, consider a separate stress target or
// a time/iteration-budgeted harness rather than unbounded fuzz inputs.
const MAX_ITERS: u64 = 4096;

fuzz_target!(|data: ([u8; 32], [u8; 100], u16)| -> Corpus {
    let (genesis_challenge, element, iters) = data;
    let iters = iters as u64;
    if iters > MAX_ITERS {
        return Corpus::Reject;
    }
    prove(&genesis_challenge, &element, 1024, iters);
    Corpus::Keep
});
