#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]
#![allow(non_upper_case_globals)]

mod bindings {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

fn main() {
    // I have
    // bindings::create_discriminant_wrapper(seed, seed_size, length)
    // and
    // bindings::verify_n_wesolowski_wrapper(discriminant_str, discriminant_size, x_s, x_s_size, proof_blob, proof_blob_size, num_iterations, disc_size_bits, recursion)
    // in my bindings.rs
    // generate some example code for using these in a test
    // and print it out here

    let seed = [0u8; 32];
    let seed_size = 32;
    let length = 1000;
    let discriminant_size = 32;
    let x_s = [0i8; 32];
    let x_s_size = 32;
    let proof_blob = [0i8; 32];
    let proof_blob_size = 32;
    let num_iterations = 10;
    let disc_size_bits = 32;
    let recursion = 0;

    unsafe {
        let discriminant = bindings::create_discriminant_wrapper(seed.as_ptr(), seed_size, length);
        let result = bindings::verify_n_wesolowski_wrapper(
            discriminant,
            discriminant_size,
            x_s.as_ptr(),
            x_s_size,
            proof_blob.as_ptr(),
            proof_blob_size,
            num_iterations,
            disc_size_bits,
            recursion,
        );
        println!("result: {}", result);
    }
}
