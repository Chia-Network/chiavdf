//! Corpus verification from src/stress_test.cpp, extended with corrupted inputs.
//! Mainnet samples cover iteration-count bit widths 1 through 30 and depths up to 61.
//! High-bit mutations exercise 64-bit counts beyond the recorded proofs' range.

use super::common::{corpus, FORM_SIZE};
use chiavdf::verify_n_wesolowski;

const SEGMENT_SIZE: usize = 8 + 33 + FORM_SIZE;

#[test]
fn accepts_recorded_vdf_proofs() {
    let mut saw_recursive_proof = false;
    for (index, case) in corpus().iter().enumerate() {
        saw_recursive_proof |= case.recursion > 0;
        assert!(
            verify_n_wesolowski(
                &case.discriminant,
                &case.x,
                &case.proof,
                case.iterations,
                case.recursion,
            ),
            "corpus record {index}, recursion {}",
            case.recursion,
        );
    }
    assert!(saw_recursive_proof, "corpus must exercise recursive proofs");
}

#[test]
fn rejects_recorded_proofs_with_wrong_iterations_or_recursion() {
    for (index, case) in corpus().iter().enumerate() {
        // Changing only a high bit must also invalidate a proof: dropping that
        // bit at a GMP scalar boundary would otherwise preserve verification.
        for iterations in [
            case.iterations + 1,
            case.iterations ^ (1_u64 << 32),
            case.iterations ^ (1_u64 << 63),
        ] {
            assert!(
                !verify_n_wesolowski(
                    &case.discriminant,
                    &case.x,
                    &case.proof,
                    iterations,
                    case.recursion,
                ),
                "wrong iteration count {iterations} accepted for corpus record {index}",
            );
        }
        for recursion in [case.recursion + 1, u64::MAX] {
            assert!(
                !verify_n_wesolowski(
                    &case.discriminant,
                    &case.x,
                    &case.proof,
                    case.iterations,
                    recursion,
                ),
                "wrong recursion {recursion} accepted for corpus record {index}",
            );
        }
    }
}

#[test]
fn rejects_corrupted_recorded_proofs() {
    for (index, case) in corpus().iter().enumerate() {
        for offset in [0, FORM_SIZE] {
            let mut proof = case.proof.clone();
            proof[offset] = 0;
            proof[offset + 1] = 0xff;
            assert!(
                !verify_n_wesolowski(
                    &case.discriminant,
                    &case.x,
                    &proof,
                    case.iterations,
                    case.recursion,
                ),
                "malformed form at {offset} accepted for corpus record {index}",
            );
        }
        let other = corpus()
            .iter()
            .find(|other| other.discriminant != case.discriminant)
            .expect("corpus must contain distinct discriminants");
        assert!(
            !verify_n_wesolowski(
                &other.discriminant,
                &case.x,
                &case.proof,
                case.iterations,
                case.recursion,
            ),
            "wrong discriminant accepted for corpus record {index}",
        );
    }
}

#[test]
fn rejects_corrupted_recursive_segments() {
    for (index, case) in corpus().iter().enumerate().filter(|(_, c)| c.recursion > 0) {
        // The last segment is checked first and contains iterations, B, and a form.
        let offset = case.proof.len() - SEGMENT_SIZE;
        let mut wrong_iterations = case.proof.clone();
        wrong_iterations[offset..offset + 8].copy_from_slice(&u64::MAX.to_be_bytes());
        let mut zero_b = case.proof.clone();
        zero_b[offset + 8..offset + 8 + 33].fill(0);
        let mut malformed_form = case.proof.clone();
        malformed_form[offset + 8 + 33] = 0;
        malformed_form[offset + 8 + 33 + 1] = 0xff;
        for proof in [&wrong_iterations, &zero_b, &malformed_form] {
            assert!(
                !verify_n_wesolowski(
                    &case.discriminant,
                    &case.x,
                    proof,
                    case.iterations,
                    case.recursion,
                ),
                "corrupted segment accepted for corpus record {index}",
            );
        }

        // Keep the final segment's remaining count unchanged, so this checks
        // high-bit handling in the recursive segment's exponent itself.
        let segment_iterations =
            u64::from_be_bytes(case.proof[offset..offset + 8].try_into().unwrap());
        let mut wide_segment = case.proof.clone();
        wide_segment[offset..offset + 8]
            .copy_from_slice(&(segment_iterations + (1_u64 << 32)).to_be_bytes());
        assert!(
            !verify_n_wesolowski(
                &case.discriminant,
                &case.x,
                &wide_segment,
                case.iterations + (1_u64 << 32),
                case.recursion,
            ),
            "wide corrupted segment accepted for corpus record {index}",
        );
    }
}
