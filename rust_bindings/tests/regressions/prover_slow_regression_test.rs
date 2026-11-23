//! Tiny-iteration and coordinate-overflow cases from src/prover_slow_regression_test.cpp,
//! combined with proof chaining from tests/test_verifier.py.

use super::common::{special_form, CHALLENGE, FORM_SIZE};
use chiavdf::{create_discriminant, prove, verify_n_wesolowski};

#[test]
fn generated_proofs_verify_and_chain() {
    // ProverSlow's tiny-iteration and coordinate-overflow regression cases.
    // Also port test_verifier.py's second proof starting from the first output.
    for bits in [512, 1024] {
        let mut discriminant = vec![0; bits / 8];
        assert!(create_discriminant(&CHALLENGE, &mut discriminant));
        for iterations in [1, 2, 50, 55, 66, 231] {
            let x = special_form(8);
            let proof = prove(&CHALLENGE, &x, bits, iterations).unwrap();
            assert_eq!(proof.len(), 2 * FORM_SIZE);
            assert!(verify_n_wesolowski(
                &discriminant,
                &x,
                &proof,
                iterations,
                0
            ));
            assert!(!verify_n_wesolowski(
                &discriminant,
                &x,
                &proof,
                iterations + 1,
                0
            ));
            let y = &proof[..FORM_SIZE];
            let next = prove(&CHALLENGE, y, bits, 19).unwrap();
            assert!(verify_n_wesolowski(&discriminant, y, &next, 19, 0));
        }
    }
}
