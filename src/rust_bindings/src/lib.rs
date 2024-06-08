#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]
#![allow(non_upper_case_globals)]

use std::ffi::{CStr, CString};

extern crate link_cplusplus;

mod bindings {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub fn create_discriminant(seed: &[u8], disc_size_bits: u64) -> CString {
    // SAFETY: The length is guaranteed to match the actual length of the char pointer.
    unsafe {
        let ptr =
            bindings::create_discriminant_wrapper(seed.as_ptr(), seed.len(), disc_size_bits as i32);
        let c_str = CStr::from_ptr(ptr).to_owned();
        bindings::free(ptr as *mut std::ffi::c_void);
        c_str
    }
}

pub fn verify_n_wesolowski(
    discriminant: &CStr,
    x_s: [u8; 100],
    proof: &[u8],
    num_iterations: u64,
    disc_size_bits: u64,
    recursion: u64,
) -> bool {
    // SAFETY: The lengths are guaranteed to match the actual lengths of the char pointers.
    unsafe {
        let value = bindings::verify_n_wesolowski_wrapper(
            dbg!(discriminant.as_ptr() as *const std::ffi::c_char),
            dbg!(x_s.as_ptr() as *const std::ffi::c_char),
            dbg!(x_s.len()),
            dbg!(proof.as_ptr() as *const std::ffi::c_char),
            dbg!(proof.len()),
            dbg!(num_iterations),
            dbg!(disc_size_bits),
            dbg!(recursion),
        );
        value == 1
    }
}

pub fn prove(
    challenge: &[u8],
    x_s: [u8; 100],
    disc_size_bits: i32,
    num_iterations: u64,
) -> Vec<u8> {
    // SAFETY: The lengths are guaranteed to match the actual lengths of the char pointers.
    unsafe {
        let array = bindings::prove_wrapper(
            challenge.as_ptr(),
            challenge.len(),
            x_s.as_ptr(),
            x_s.len(),
            disc_size_bits,
            num_iterations,
        );
        let result = std::slice::from_raw_parts(array.data, array.length).to_vec();
        bindings::delete_byte_array(array);
        result
    }
}

#[cfg(test)]
mod tests {

    use hex_literal::hex;
    use rand::{Rng, SeedableRng};
    use rand_chacha::ChaCha8Rng;

    use super::*;

    #[test]
    fn test_create_discriminant() {
        let mut discriminants = Vec::new();
        let mut seeds = [[0; 10]; 10];

        for (i, seed) in seeds.iter_mut().enumerate() {
            let mut rng = ChaCha8Rng::seed_from_u64(i as u64);

            rng.fill(seed);

            let discriminant = create_discriminant(seed, 512);

            discriminants.push(discriminant);
        }

        let expected = [
            "-0x9a8eaf9c52d9a5f1db648cdf7bcd04b35cb1ac4f421c978fa61fe1344b97d4199dbff700d24e7cfc0b785e4b8b8023dc49f0e90227f74f54234032ac3381879f",
            "-0xb193cdb02f1c2615a257b98933ee0d24157ac5f8c46774d5d635022e6e6bd3f7372898066c2a40fa211d1df8c45cb95c02e36ef878bc67325473d9c0bb34b047",
            "-0xbb5bd19ae50efe98b5ac56c69453a95e92dc16bb4b2824e73b39b9db0a077fa33fc2e775958af14f675a071bf53f1c22f90ccbd456e2291276951830dba9dcaf",
            "-0xa1e93b8f2e9b0fd3b1325fbe40601f55e2afbdc6161409c0aff8737b7213d7d71cab21ffc83a0b6d5bdeee2fdcbbb34fbc8fc0b439915075afa9ffac8bb1b337",
            "-0xf2a10f70148fb30e4a16c4eda44cc0f9917cb9c2d460926d59a408318472e2cfd597193aa58e1fdccc6ae6a4d85bc9b27f77567ebe94fcedbf530a60ff709fd7",
        ];

        for i in 0..5 {
            assert_eq!(
                discriminants[i].to_str().unwrap(),
                expected[i],
                "Discriminant {} does not match (seed is {})",
                i,
                hex::encode(seeds[i])
            );
        }
    }

    #[derive(Debug)]
    struct VdfInfo {
        challenge_hash: [u8; 32],
        number_iters: u64,
        output: [u8; 100],
    }

    impl VdfInfo {
        fn new(challenge_hash: [u8; 32], number_iters: u64, output: [u8; 100]) -> Self {
            Self {
                challenge_hash,
                number_iters,
                output,
            }
        }
    }

    #[derive(Debug)]
    struct VdfProof {
        witness_type: u8,
        witness: Vec<u8>,
        normalized_to_identity: bool,
    }

    impl VdfProof {
        fn new(witness_type: u8, witness: [u8; 100], normalized_to_identity: bool) -> Self {
            Self {
                witness_type,
                witness: witness.to_vec(),
                normalized_to_identity,
            }
        }
    }

    fn get_vdf_info_and_proof(
        vdf_input: [u8; 100],
        challenge_hash: [u8; 32],
        number_iters: u64,
        normalized_to_identity: bool,
    ) -> (VdfInfo, VdfProof) {
        let result = prove(&challenge_hash, vdf_input, 1024, number_iters);
        let output: [u8; 100] = result[0..100].try_into().unwrap();
        let proof_bytes: [u8; 100] = result[100..200].try_into().unwrap();
        (
            VdfInfo::new(challenge_hash, number_iters, output),
            VdfProof::new(0, proof_bytes, normalized_to_identity),
        )
    }

    fn validate_vdf(proof: VdfProof, input_el: [u8; 100], info: VdfInfo) -> bool {
        let genesis_challenge =
            hex!("ccd5bb71183532bff220ba46c268991a3ff07eb358e8255a65c30a2dce0e5fbb");

        if proof.witness_type + 1 > 64 {
            return false;
        }

        let disc = create_discriminant(&genesis_challenge, 1024);

        let mut bytes = Vec::new();
        bytes.extend(info.output);
        bytes.extend(proof.witness);

        verify_n_wesolowski(
            &disc,
            input_el,
            &bytes,
            info.number_iters,
            1024,
            proof.witness_type as u64,
        )
    }

    #[test]
    fn test_verify_n_wesolowski() {
        let genesis_challenge =
            hex!("ccd5bb71183532bff220ba46c268991a3ff07eb358e8255a65c30a2dce0e5fbb");
        let mut default_el = [0; 100];
        default_el[0] = 0x08;
        let (vdf, proof) = dbg!(get_vdf_info_and_proof(
            default_el,
            genesis_challenge,
            231,
            false
        ));
        assert!(validate_vdf(proof, default_el, vdf));
    }
}
