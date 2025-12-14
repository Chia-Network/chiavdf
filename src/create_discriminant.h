#ifndef CREATE_DISCRIMINANT_H
#define CREATE_DISCRIMINANT_H

#include "proof_common.h"
inline integer CreateDiscriminant(const uint8_t* challenge, 
                                  int challenge_length, 
                                  int discriminant_size_bits) {
    // INPUT VALIDATION - Fix for issue #282
    
    // Check 1: Validate discriminant_size_bits is positive
    if (discriminant_size_bits <= 0) {
        throw std::invalid_argument(
            "discriminant_size_bits must be positive (got " + 
            std::to_string(discriminant_size_bits) + ")"
        );
    }
    
    // Check 2: Validate upper bound (optional but recommended)
    const int MAX_DISCRIMINANT_SIZE_BITS = 16384;
    if (discriminant_size_bits > MAX_DISCRIMINANT_SIZE_BITS) {
        throw std::invalid_argument(
            "discriminant_size_bits exceeds maximum allowed value"
        );
    }
    
    // Check 3: Validate challenge pointer and length
    if (challenge == nullptr) {
        throw std::invalid_argument("challenge pointer cannot be null");
    }
    
    if (challenge_length <= 0) {
        throw std::invalid_argument("challenge_length must be positive");
    }
    
    // Check 4: Validate that discriminant_size_bits is a multiple of 8 (required by HashPrime)
    if (discriminant_size_bits % 8 != 0) {
        throw std::invalid_argument(
            "discriminant_size_bits must be a multiple of 8 (got " + 
            std::to_string(discriminant_size_bits) + ")"
        );
    }
    
    // Convert challenge pointer to vector and call HashPrime
    std::vector<uint8_t> challenge_vector(challenge, challenge + challenge_length);
    return HashPrime(challenge_vector, discriminant_size_bits, {0, 1, 2, discriminant_size_bits - 1}) * integer(-1);
}
integer CreateDiscriminant(std::vector<uint8_t>& seed, int length = 1024) {
    // INPUT VALIDATION - Fix for issue #282
    
    // Check 1: Validate discriminant_size_bits is positive
    if (length <= 0) {
        throw std::invalid_argument(
            "discriminant_size_bits must be positive (got " + 
            std::to_string(length) + ")"
        );
    }
    
    // Check 2: Validate upper bound (optional but recommended)
    const int MAX_DISCRIMINANT_SIZE_BITS = 16384;
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
    
    return HashPrime(seed, length, {0, 1, 2, length - 1}) * integer(-1);
}

#endif // CREATE_DISCRIMINANT_H
