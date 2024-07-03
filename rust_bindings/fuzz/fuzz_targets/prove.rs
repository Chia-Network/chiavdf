#![no_main]

use chiavdf::prove;
use libfuzzer_sys::{arbitrary::Unstructured, fuzz_target};

fuzz_target!(|data: &[u8]| {
    let mut unstructured = Unstructured::new(data);
    let genesis_challenge: [u8; 32] = unstructured.arbitrary().unwrap();
    let element: [u8; 100] = unstructured.arbitrary().unwrap();
    let iters: u8 = unstructured.arbitrary().unwrap();
    prove(&genesis_challenge, &element, 1024, iters as u64);
});
