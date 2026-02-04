#include "vdf.h"

#include <cassert>
#include <cstdint>
#include <iostream>

// Required by `parameters.h` (runtime tuning knobs).
int gcd_base_bits = 50;
int gcd_128_max_iter = 3;

int main() {
    // Basic truth table for the helper.
    assert(!chiavdf_diag_is_single_slow_step(0));
    assert(chiavdf_diag_is_single_slow_step(1));
    assert(!chiavdf_diag_is_single_slow_step(2));
    assert(!chiavdf_diag_is_single_slow_step(32));

    std::cout << "OK: chiavdf_diag_is_single_slow_step\n";
    return 0;
}

