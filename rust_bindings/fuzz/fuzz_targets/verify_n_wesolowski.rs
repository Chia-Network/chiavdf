#![no_main]

use chiavdf::{create_discriminant, verify_n_wesolowski};
use libfuzzer_sys::{arbitrary::Unstructured, fuzz_target};

fuzz_target!(|data: &[u8]| {
    let mut unstructured = Unstructured::new(data);
    let seed: [u8; 10] = unstructured.arbitrary().unwrap();
    let mut disc = [0; 64];
    if !create_discriminant(&seed, &mut disc) {
        return;
    };
    let element: [u8; 100] = unstructured.arbitrary().unwrap();
    let proof: Vec<u8> = unstructured.arbitrary().unwrap();
    let iters: u8 = unstructured.arbitrary().unwrap();
    verify_n_wesolowski(&disc, &element, &proof, iters as u64, 0);
});
