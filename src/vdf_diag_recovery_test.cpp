#include "vdf.h"
#include "create_discriminant.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

// Required by `parameters.h` (runtime tuning knobs).
int gcd_base_bits = 50;
int gcd_128_max_iter = 3;

int main() try {
    assert(is_vdf_test);  // compiled with VDF_MODE=1

    init_gmp();
    set_rounding_mode();
    init_gcd_params_for_cpu();

    // Keep this deterministic and fast.
    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    integer D = CreateDiscriminant(challenge_hash, 1024);

    // Inflate L (but keep it within the fast-path representation) so `a > L` is false
    // for many iterations, forcing multi-iteration slow recovery.
    integer L = root(-D, 4);
    L <<= 512;  // ~768 bits total, should fit the fast path's padded representation.

    form f = form::generator(D);

    // Use a cheap callback; we don't need proofs here, just to exercise recovery attribution.
    OneWesolowskiCallback weso(D, f, /*wanted_iter=*/0);
    FastStorage* fast_storage = nullptr;

    // Force the fast path on (it will immediately bail due to `a <= L`).
    debug_mode = false;
    fast_algorithm = false;
    two_weso = false;

    std::atomic<bool> stopped{false};

    chiavdf_vdf_test_diag_stats stats;
    chiavdf_vdf_test_diag_stats_sink = &stats;

    // Run a small number of iterations; the recovery loop should do >1 slow steps.
    repeated_square(/*iterations=*/64, f, D, L, &weso, fast_storage, stopped);

    chiavdf_vdf_test_diag_stats_sink = nullptr;

    // Sanity: we must have exercised the "a <= L" bailout + recovery.
    assert(stats.a_not_high_enough_fails > 0);
    assert(stats.a_not_high_enough_recovery_calls > 0);
    assert(stats.a_not_high_enough_recovery_iters > stats.a_not_high_enough_recovery_calls);

    // Regression check:
    // When recovery performs a burst (>1 slow iters), the next fast attempt must NOT be
    // attributed as "entered after single slow".
    assert(stats.a_not_high_enough_fails_after_single_slow == 0);

    std::cout << "OK: recovery attribution (after-single-slow) is correct\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
    return 1;
}

#if defined(ARCH_ARM)
#include "asm_arm_fallback_impl.inc"
#endif

