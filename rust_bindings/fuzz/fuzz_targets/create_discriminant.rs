#![no_main]

use chiavdf::create_discriminant;
use libfuzzer_sys::{arbitrary::Unstructured, fuzz_target};

fuzz_target!(|data: &[u8]| {
    let mut unstructured = Unstructured::new(data);
    let seed: [u8; 10] = unstructured.arbitrary().unwrap();
    let mut disc = [0; 64];
    assert!(create_discriminant(&seed, &mut disc));
});
