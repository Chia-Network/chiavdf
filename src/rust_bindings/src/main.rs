#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]
#![allow(non_upper_case_globals)]

extern crate link_cplusplus;

mod bindings {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub fn create_discriminant<const N: usize>(seed: &[u8], output: &mut [u8; N]) {
    unsafe {
        let ptr = bindings::create_discriminant_wrapper(seed.as_ptr(), seed.len(), N as i32);
        output.copy_from_slice(std::slice::from_raw_parts(ptr as *const u8, N));
        bindings::free(ptr as *mut std::ffi::c_void);
    }
}

pub fn verify_n_wesolowski(
    discriminant: &[u8],
    x_s: &[u8],
    proof: &[u8],
    num_iterations: u64,
    disc_size_bits: u64,
    recursion: u64,
) -> bool {
    unsafe {
        let value = bindings::verify_n_wesolowski_wrapper(
            discriminant.as_ptr() as *const std::ffi::c_char,
            discriminant.len(),
            x_s.as_ptr() as *const std::ffi::c_char,
            x_s.len(),
            proof.as_ptr() as *const std::ffi::c_char,
            proof.len(),
            num_iterations,
            disc_size_bits,
            recursion,
        );
        value == 1
    }
}

fn main() {
    let mut discriminant = [0; 512];
    create_discriminant(&[42; 10], &mut discriminant);
    println!("Discriminant: {:?}", discriminant);

    let mut initial_el = [0; 100];
    initial_el[0] = 0x08;

    let bytes = [0; 100];

    let valid = verify_n_wesolowski(&discriminant, &initial_el, &bytes, 100000, 32, 32);

    println!("Is valid: {}", valid);
}
