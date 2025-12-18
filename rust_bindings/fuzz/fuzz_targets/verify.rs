#![no_main]

use chiavdf::{create_discriminant, prove, verify_n_wesolowski};
use libfuzzer_sys::fuzz_target;

fuzz_target!(|genesis_challenge: [u8; 32]| {
    let mut default_el = [0; 100];
    default_el[0] = 0x08;
    let proof = prove(&genesis_challenge, &default_el, 1024, 231).unwrap();
    let mut disc = [0; 128];
    assert!(create_discriminant(&genesis_challenge, &mut disc));
    let valid = verify_n_wesolowski(&disc, &default_el, &proof, 231, 0);
    assert!(valid);
});
