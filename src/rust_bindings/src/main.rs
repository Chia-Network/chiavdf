#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]
#![allow(non_upper_case_globals)]

extern crate link_cplusplus;

mod bindings {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

fn main() {
    unsafe {
        let discriminant = bindings::create_discriminant_wrapper([42; 10].as_ptr(), 10, 512);
        println!("discriminant: {:?}", discriminant);

        let mut initial_el = [0i8; 100];
        initial_el[0] = 0x08;

        let bytes = [0; 100];

        let valid = bindings::verify_n_wesolowski_wrapper(
            discriminant,
            512,
            initial_el.as_ptr(),
            initial_el.len(),
            bytes.as_ptr(),
            bytes.len(),
            100000,
            32,
            32,
        );

        println!("valid: {:?}", valid);
    }
}
