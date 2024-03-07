#pragma once
#include <stdint.h>

#ifdef __cplusplus
#include <cstddef> // for size_t
#include <cstdint> // for uint8_t
extern "C" {
#endif

const char* create_discriminant_wrapper(const uint8_t* seed, size_t seed_size, int length);

// Define a struct to hold the byte array and its length
typedef struct {
    uint8_t* data;
    size_t length;
} ByteArray;
ByteArray prove_wrapper(const uint8_t* challenge_hash, size_t challenge_size, const uint8_t* x_s, size_t x_s_size, int discriminant_size_bits, uint64_t num_iterations);

int verify_n_wesolowski_wrapper(const char* discriminant_str, size_t discriminant_size, const char* x_s, size_t x_s_size, const char* proof_blob, size_t proof_blob_size, uint64_t num_iterations, uint64_t disc_size_bits, uint64_t recursion);

#ifdef __cplusplus
}
#endif
