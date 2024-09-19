#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
#include <cstddef> // for size_t
#include <cstdint> // for uint8_t
extern "C" {
#endif

bool create_discriminant_wrapper(const uint8_t* seed, size_t seed_size, size_t size_bits, uint8_t* result);

// Define a struct to hold the byte array and its length
typedef struct {
    uint8_t* data;
    size_t length;
} ByteArray;
ByteArray prove_wrapper(const uint8_t* challenge_hash, size_t challenge_size, const uint8_t* x_s, size_t x_s_size, size_t discriminant_size_bits, uint64_t num_iterations);

bool verify_n_wesolowski_wrapper(const uint8_t* discriminant_bytes, size_t discriminant_size, const uint8_t* x_s, const uint8_t* proof_blob, size_t proof_blob_size, uint64_t num_iterations, uint64_t recursion);
void delete_byte_array(ByteArray array);

#ifdef __cplusplus
}
#endif
