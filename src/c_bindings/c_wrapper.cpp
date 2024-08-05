#include "c_wrapper.h"
#include <vector>
#include <gmpxx.h>
#include "../verifier.h"
#include "../prover_slow.h"

extern "C" {
    // C wrapper function
    bool create_discriminant_wrapper(const uint8_t* seed, size_t seed_size, size_t size_bits, uint8_t* result) {
        try {
            std::vector<uint8_t> seed_vector(seed, seed + seed_size);
            integer discriminant = CreateDiscriminant(seed_vector, size_bits);
            mpz_export(result, NULL, 1, 1, 0, 0, discriminant.impl);
            return true;
        } catch (const runtime_error& e) {
            return false;
        }
    }

    ByteArray prove_wrapper(const uint8_t* challenge_hash, size_t challenge_size, const uint8_t* x_s, size_t x_s_size, size_t discriminant_size_bits, uint64_t num_iterations) {
        try {
            std::vector<uint8_t> challenge_hash_bytes(challenge_hash, challenge_hash + challenge_size);
            integer discriminant = CreateDiscriminant(challenge_hash_bytes, discriminant_size_bits);
            form x = DeserializeForm(discriminant, x_s, x_s_size);
            std::vector<uint8_t> result = ProveSlow(discriminant, x, num_iterations);

            // Allocate memory for the result and copy data
            uint8_t* resultData = new uint8_t[result.size()];
            std::copy(result.begin(), result.end(), resultData);

            // Create and return a ByteArray struct
            ByteArray resultArray = { resultData, result.size() };
            return resultArray;
        } catch (const runtime_error& e) {
            ByteArray resultArray = { nullptr, 0 };
            return resultArray;
        }
    }

    bool verify_n_wesolowski_wrapper(const uint8_t* discriminant_bytes, size_t discriminant_size_bits, const uint8_t* x_s, size_t x_s_size, const uint8_t* proof_blob, size_t proof_blob_size, uint64_t num_iterations, uint64_t recursion) {
        try {
            integer discriminant;
            mpz_import(discriminant.impl, discriminant_size_bits / 8, 1, 1, 0, 0, discriminant_bytes);
            
            std::vector<uint8_t> x_s_v(x_s, x_s + x_s_size);
            std::vector<uint8_t> proof_blob_v(proof_blob, proof_blob + proof_blob_size);
            
            return CheckProofOfTimeNWesolowski(
                discriminant,
                x_s_v.data(),
                proof_blob_v.data(),
                proof_blob_v.size(),
                num_iterations,
                discriminant_size_bits,
                recursion
            );
        } catch (const runtime_error& e) {
            return false;
        }
    }

    void delete_byte_array(ByteArray array) {
        delete[] array.data;
    }
}
