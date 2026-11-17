#ifndef CREATE_DISCRIMINANT_H
#define CREATE_DISCRIMINANT_H

#include "proof_common.h"
#include <span>

inline integer CreateDiscriminant(std::span<const uint8_t> seed, int length = 1024) {
    // INPUT VALIDATION - Fix for issue #282
    
    // Check 1: Validate discriminant_size_bits is positive
    if (length <= 0) {
        throw std::invalid_argument(
            "discriminant_size_bits must be positive (got " + 
            std::to_string(length) + ")"
        );
    }
    
    // Check 2: Validate upper bound (optional but recommended)
    const int MAX_DISCRIMINANT_SIZE_BITS = BQFC_MAX_D_BITS;
    if (length > MAX_DISCRIMINANT_SIZE_BITS) {
        throw std::invalid_argument(
            "discriminant_size_bits exceeds maximum allowed value"
        );
    }
    
    // Check 3: Validate that length is a multiple of 8 (required by HashPrime)
    if (length % 8 != 0) {
        throw std::invalid_argument(
            "discriminant_size_bits must be a multiple of 8 (got " + 
            std::to_string(length) + ")"
        );
    }
    
    // Check 4: Validate seed is not empty
    if (seed.empty()) {
        throw std::invalid_argument("seed cannot be empty");
    }
    
    const std::array<int, 4> bitmask{0, 1, 2, length - 1};
    return HashPrime(seed, length, bitmask) * integer(-1);
}

#endif // CREATE_DISCRIMINANT_H
