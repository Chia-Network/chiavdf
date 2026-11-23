//! Regression ports exercising the public Rust API, including the C ABI and GMP build.
//! Keep source-specific modules together in one integration-test binary to share fixtures.

#[path = "regressions/common.rs"]
mod common;
#[path = "regressions/discriminant_bounds_regression_test.rs"]
mod discriminant_bounds_regression_test;
#[path = "regressions/proof_deserialization_regression_test.rs"]
mod proof_deserialization_regression_test;
#[path = "regressions/prover_slow_regression_test.rs"]
mod prover_slow_regression_test;
#[path = "regressions/stress_test.rs"]
mod stress_test;
