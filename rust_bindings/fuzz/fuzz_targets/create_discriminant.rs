#![no_main]

use chiavdf::create_discriminant;
use libfuzzer_sys::fuzz_target;

fuzz_target!(|seed: [u8; 10]| {
    let mut disc = [0; 64];
    assert!(create_discriminant(&seed, &mut disc));
});
