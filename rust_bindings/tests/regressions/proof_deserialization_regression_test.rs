//! Form decoding cases from src/proof_deserialization_regression_test.cpp,
//! including SerializedFormTest and the shared tests/test_bqfc_deserialize.py cases.
//! Size checks also cover tests/test_x_s_length.py through the Rust API.

use super::common::{corpus, special_form, FORM_SIZE};
use chiavdf::verify_n_wesolowski;
use hex_literal::hex;

#[test]
fn rejects_wrong_proof_and_initial_form_sizes() {
    let case = &corpus()[0];
    for size in [0, 1, FORM_SIZE - 1, FORM_SIZE + 1, 2 * FORM_SIZE] {
        let mut x = case.x.clone();
        x.resize(size, 0);
        assert!(
            !verify_n_wesolowski(&case.discriminant, &x, &case.proof, case.iterations, 0),
            "initial form size {size} accepted",
        );
    }
    for size in [
        0,
        1,
        FORM_SIZE - 1,
        FORM_SIZE,
        2 * FORM_SIZE - 1,
        2 * FORM_SIZE + 1,
    ] {
        let mut proof = case.proof.clone();
        proof.resize(size, 0);
        assert!(
            !verify_n_wesolowski(&case.discriminant, &case.x, &proof, case.iterations, 0),
            "proof size {size} accepted",
        );
    }
}

// At zero iterations, y = x and the proof is the identity. This lets us test
// form decoding through verification, without adding a deserialization API.
fn verifies_unchanged(discriminant: &[u8], form: &[u8]) -> bool {
    let mut proof = form.to_vec();
    proof.extend_from_slice(&special_form(4));
    verify_n_wesolowski(discriminant, form, &proof, 0, 0)
}

#[test]
fn accepts_canonical_serialized_forms() {
    let case = &corpus()[0];
    assert!(verifies_unchanged(
        &case.discriminant,
        &case.proof[..FORM_SIZE]
    ));
    assert!(verifies_unchanged(
        &alternate_discriminant(),
        &alternate_form()
    ));
    for flag in [4, 8] {
        assert!(verifies_unchanged(&[23], &special_form(flag)));
    }
    assert!(verifies_unchanged(&[15], &special_form(8)));
    assert!(verifies_unchanged(&[3], &special_form(4)));
    // bqfc_serialize(a=2, b=-1, D=-23), from SerializedFormTest.
    let mut negative_b = [0; FORM_SIZE];
    negative_b[..6].copy_from_slice(&hex!("010002000101"));
    assert!(verifies_unchanged(&[23], &negative_b));
}

#[test]
fn rejects_unreduced_and_invalid_small_discriminant_forms() {
    // Exact bqfc_serialize outputs for the invalid (D, a, b) tuples in
    // SerializedFormTest: (-23, 3, 1), (-23, 1, -1), and (-15, 2, -1).
    for (discriminant, prefix) in [
        (23, hex!("000003000101")),
        (23, hex!("010001000101")),
        (15, hex!("010002000101")),
    ] {
        let mut form = [0; FORM_SIZE];
        form[..prefix.len()].copy_from_slice(&prefix);
        assert!(!verifies_unchanged(&[discriminant], &form));
    }
    // (-27, 2, 1) serializes as the generator but is not a primitive form.
    assert!(!verifies_unchanged(&[27], &special_form(8)));
}

#[test]
fn rejects_malformed_and_noncanonical_serialized_forms() {
    let case = &corpus()[0];
    let mut malformed = [0; FORM_SIZE];
    malformed[1] = 0xff; // Impossible g_size for a 1024-bit discriminant.
    assert!(!verifies_unchanged(&case.discriminant, &malformed));
    let mut noncanonical = case.proof[..FORM_SIZE].to_vec();
    noncanonical[FORM_SIZE - 1] ^= 1;
    assert!(!verifies_unchanged(&case.discriminant, &noncanonical));

    // The b0 regression fixture shared with tests/test_bqfc_deserialize.py.
    for value in [4, 8] {
        let mut form = alternate_form();
        form[FORM_SIZE - 1] ^= value;
        assert!(!verifies_unchanged(&alternate_discriminant(), &form));
    }
}

#[test]
fn rejects_noncanonical_identity_and_generator_encodings() {
    // Port every padding-byte and flag-bit mutation from SerializedFormTest and
    // tests/test_bqfc_deserialize.py. Both input and output use the mutated form,
    // so rejection must come from decoding rather than a mismatched VDF result.
    for flag in [4, 8] {
        for index in 1..FORM_SIZE {
            let mut form = special_form(flag);
            form[index] = 1;
            assert!(
                !verifies_unchanged(&[23], &form),
                "flag {flag}, nonzero padding byte {index}",
            );
        }
        for bit in 0..8 {
            let form = special_form(flag ^ (1 << bit));
            assert!(
                !verifies_unchanged(&[23], &form),
                "flag {flag}, flipped bit {bit}",
            );
        }
    }
}

fn alternate_discriminant() -> [u8; 128] {
    // Unsigned big-endian magnitude of B0_DISCRIMINANT in test_bqfc_deserialize.py.
    hex!("d036828612e0ccf69971b6b14438171cf08074bc5ea450b3d5ae0526be867a8015671af4648aa59d64ec06091036385cd351dc5bdf01c694bb915316b4f2fee9509ff1dbaebf6407994962d77aaedd403d08436096cce62465eddcd652f6970ba48e3759f8d226cac0a2976f24de3da40c744286cec5ad666d62a51ac32ac1a7")
}

fn alternate_form() -> [u8; FORM_SIZE] {
    hex!(
        "0300d8262c430e78e7c06cf60c9b2049968f604f3b506a85bfe4fff319f8176760
         e06cab8ab45524458bf558101f9b4ce8c23cc1e053263272b808b76c6f26493a11
         3b62ded5707b28d9eedc0503ac2efcd32be670726725be0fa7ea01f0ef3f60250201"
    )
}
