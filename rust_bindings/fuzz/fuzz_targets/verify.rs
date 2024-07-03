#![no_main]

use chiavdf::{create_discriminant, prove, verify_n_wesolowski};
use libfuzzer_sys::{arbitrary::Unstructured, fuzz_target};

fuzz_target!(|data: &[u8]| {
    let mut unstructured = Unstructured::new(data);
    let genesis_challenge: [u8; 32] = unstructured.arbitrary().unwrap();
    let mut default_el = [0; 100];
    default_el[0] = 0x08;
    let proof = prove(&genesis_challenge, &default_el, 1024, 231).unwrap();
    let disc = create_discriminant(&genesis_challenge, 1024).unwrap();
    let valid = verify_n_wesolowski(&disc, &default_el, &proof, 231, 1024, 0);
    assert!(valid);
});
