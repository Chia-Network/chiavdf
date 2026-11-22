//! Public-API cases from src/discriminant_bounds_regression_test.cpp.

use super::common::{corpus, CHALLENGE};
use chiavdf::{create_discriminant, verify_n_wesolowski};

#[test]
fn rejects_out_of_range_discriminants() {
    let case = &corpus()[0];
    // The magnitude is unchanged, but advertising 1032 bits exceeds the 1024-bit limit.
    let mut oversized = vec![0];
    oversized.extend_from_slice(&case.discriminant);
    for discriminant in [&[][..], &oversized] {
        assert!(!verify_n_wesolowski(
            discriminant,
            &case.x,
            &case.proof,
            case.iterations,
            0,
        ));
    }
    let mut oversized_result = [0; 129];
    assert!(!create_discriminant(&CHALLENGE, &mut oversized_result));
}
