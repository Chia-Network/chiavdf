#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]
#![allow(non_upper_case_globals)]

extern crate link_cplusplus;

mod bindings {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub fn create_discriminant<const N: usize>(seed: &[u8], output: &mut [u8; N]) {
    // SAFETY: The length is guaranteed to match the actual length of the char pointer.
    unsafe {
        let ptr = bindings::create_discriminant_wrapper(seed.as_ptr(), seed.len(), N as i32 * 8);
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
    // SAFETY: The lengths are guaranteed to match the actual lengths of the char pointers.
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

            let mut discriminant = [0; 64];
            create_discriminant(seed, &mut discriminant);

            discriminants.push(hex::encode(discriminant));
        }

        let expected = [
            hex!("2d307839613865616639633532643961356631646236343863646637626364303462333563623161633466343231633937386661363166653133343462393764"),
            hex!("2d307862313933636462303266316332363135613235376239383933336565306432343135376163356638633436373734643564363335303232653665366264"),
            hex!("2d307862623562643139616535306566653938623561633536633639343533613935653932646331366262346232383234653733623339623964623061303737"),
            hex!("2d307861316539336238663265396230666433623133323566626534303630316635356532616662646336313631343039633061666638373337623732313364"),
            hex!("2d307866326131306637303134386662333065346131366334656461343463633066393931376362396332643436303932366435396134303833313834373265"),
            hex!("2d307838346637343633633264373333373663316431393831616630623239666430366431663463663332396164376336303836633063653439303762343765"),
            hex!("2d307863646531363563643435366263316165313334626262373336636232343839303036356634366264303362323766633536316237323764386166323266"),
            hex!("2d307861633264303833303232306230636165346662333037633166653062303530393732303663633738633039343439376634366333323763363630633238"),
            hex!("2d307864663535363234646162313335373263656139306437653366376661643937616235623066316233353064353162623065653036306531353432646135"),
            hex!("2d307866623463356664663533393035353264656438636639303634353065356164386265623963353862343539386562336365346665623438646464343564"),
        ];

        for i in 0..10 {
            assert_eq!(
                discriminants[i],
                hex::encode(expected[i]),
                "Discriminant {} does not match (seed is {})",
                i,
                hex::encode(seeds[i])
            );
        }
    }
}
