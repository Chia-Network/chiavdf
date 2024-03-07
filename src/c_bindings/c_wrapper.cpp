#include "c_wrapper.h"
#include <vector>
#include <gmpxx.h>
#include "../verifier.h"
#include "../prover_slow.h"

extern "C" {
    // C wrapper function
    const char* create_discriminant_wrapper(const uint8_t* seed, size_t seed_size, int length) {
        std::vector<uint8_t> seedVector(seed, seed + seed_size);
        integer result = CreateDiscriminant(seedVector, length);

        // Serialize the 'result' to a string
        std::string resultStr = result.to_string();

        // Allocate a new C-string to hold the serialized result
        char* cResultStr = new char[resultStr.length() + 1];
        std::strcpy(cResultStr, resultStr.c_str());

        return cResultStr; // Return the C-string
    }

    ByteArray prove_wrapper(const uint8_t* challenge_hash, size_t challenge_size, const uint8_t* x_s, size_t x_s_size, int discriminant_size_bits, uint64_t num_iterations) {
        std::vector<uint8_t> challenge_hash_bytes(challenge_hash, challenge_hash + challenge_size);
        integer D = CreateDiscriminant(challenge_hash_bytes, discriminant_size_bits);
        form x = DeserializeForm(D, (const uint8_t*)x_s, x_s_size);
        std::vector<uint8_t> result = ProveSlow(D, x, num_iterations);

        // Allocate memory for the result and copy data
        uint8_t* resultData = new uint8_t[result.size()];
        std::copy(result.begin(), result.end(), resultData);

        // Create and return a ByteArray struct
        ByteArray resultArray = { resultData, result.size() };
        return resultArray;
    }

    int verify_n_wesolowski_wrapper(const char* discriminant_str, size_t discriminant_size, const char* x_s, size_t x_s_size, const char* proof_blob, size_t proof_blob_size, uint64_t num_iterations, uint64_t disc_size_bits, uint64_t recursion) {
        std::vector<uint8_t> x_s_v(x_s, x_s + x_s_size);
        std::vector<uint8_t> proof_blob_v(proof_blob, proof_blob + proof_blob_size);

        bool result = CheckProofOfTimeNWesolowski(integer(discriminant_str), x_s_v.data(), proof_blob_v.data(), proof_blob_v.size(), num_iterations, disc_size_bits, recursion);

        return result ? 1 : 0;
    }
}
