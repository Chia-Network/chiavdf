#![no_main]

use chiavdf::{create_discriminant, verify_n_wesolowski};
use libfuzzer_sys::{fuzz_target, Corpus};

fuzz_target!(|data: ([u8; 10], [u8; 100], Vec<u8>, u64, u64)| -> Corpus {
    let (seed, element, proof, iters, recursion) = data;

    let mut disc = [0; 64];
    if !create_discriminant(&seed, &mut disc) {
        return Corpus::Reject;
    };
    verify_n_wesolowski(&disc, &element, &proof, iters, recursion);
    Corpus::Keep
});
