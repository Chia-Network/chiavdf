#ifndef PROOF_BLOCK_CACHE_H
#define PROOF_BLOCK_CACHE_H

#include "integer_common.h"

#include <cstdint>
#include <vector>

// Each worker traverses j in descending order. For a fixed row i, the
// exponent increases by k on each step. The remainder from division for
// this bucket is therefore the modular-power input for the next bucket.
// Call Get once per eligible j in descending, consecutive order for each row.
// Separate worker ranges must each own a cache.
class ProofBlockCache {
    uint64_t iterations, k, l;
    const integer& modulus;
    std::vector<integer> remainders;
    std::vector<bool> initialized;
    integer quotient;
public:
    ProofBlockCache(uint64_t iterations, uint64_t k, uint64_t l, const integer& modulus)
        : iterations(iterations), k(k), l(l), modulus(modulus),
          remainders(iterations / (k * l) + (iterations % (k * l) != 0)),
          initialized(remainders.size(), false) {}

    uint64_t Get(uint64_t i, uint64_t j) {
        integer& remainder = remainders[i];
        if (!initialized[i]) {
            integer two(2);
            mpz_powm_ui(remainder.impl, two.impl, iterations - k * (i * l + j + 1), modulus.impl);
            initialized[i] = true;
        }
        mpz_mul_2exp(remainder.impl, remainder.impl, k);
        // GMP permits an output to alias an input, but quotient and remainder
        // must be distinct. This also retains allocated limb storage.
        mpz_tdiv_qr(quotient.impl, remainder.impl, remainder.impl, modulus.impl);
        auto words = quotient.to_vector();
        return words.empty() ? 0 : words[0];
    }
};
#endif
