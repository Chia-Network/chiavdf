#![no_main]

use chiavdf::prove;
use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: ([u8; 32], [u8; 100], u16)| {
    let (genesis_challenge, element, iters) = data;
    prove(&genesis_challenge, &element, 1024, iters as u64);
});
