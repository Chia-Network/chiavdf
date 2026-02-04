#![no_main]

use chiavdf::{create_discriminant, verify_n_wesolowski};
use libfuzzer_sys::{fuzz_target, Corpus};

fuzz_target!(|data: ([u8; 10], [u8; 100], Vec<u8>, u64, u64)| -> Corpus {
    let (seed, element, proof, iters, recursion) = data;

    let mut disc = [0; 64];
    if !create_discriminant(&seed, &mut disc) {
        return Corpus::Reject;
    };
    // Resource-bound the fuzz input so the target doesn't burn CI CPU/RAM.
    const MAX_ITERS: u64 = 1024;
    const MAX_RECURSION: u64 = 4;
    verify_n_wesolowski(
        &disc,
        &element,
        &proof,
        iters % MAX_ITERS,
        recursion % MAX_RECURSION,
    );
    Corpus::Keep
});
