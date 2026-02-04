#![no_main]

use chiavdf::prove;
use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: ([u8; 32], [u8; 100], u16)| {
    let (genesis_challenge, element, iters) = data;
    // Keep fuzzing fast + memory-bounded.
    //
    // Without this, `iters` can be up to 65535 which causes large transient
    // allocations in the prover. Under sanitizer/libFuzzer this leads to RSS
    // growth (allocator quarantine) and CI OOMs at the default 2GB limit.
    const MAX_ITERS: u64 = 1024;
    let iters = (iters as u64) % MAX_ITERS;
    prove(&genesis_challenge, &element, 1024, iters);
});
