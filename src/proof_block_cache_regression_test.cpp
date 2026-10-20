#include "vdf.h"
#include "prover.hpp"
#include "proof_block_cache.hpp"
#include <gtest/gtest.h>

TEST(ProofBlockCacheTest, MatchesStatelessPowersAcrossPartialRowsAndWorkerSplits) {
    for (const char* value : {"3", "257", "0x800000000000000000000000000000000000000000000000000000000000000075"}) {
        integer modulus(value);
        for (uint64_t k : {1, 2, 4, 5, 8, 10}) {
            for (uint64_t l : std::vector<uint64_t>{1, 3, 4000 / k}) {
                for (uint64_t iterations : {k - 1, k, k + 1, k * l - 1, k * l, k * l + 1, 2 * k * l + 7}) {
                    for (bool split : {false, true}) {
                        uint64_t boundary = split ? l / 2 : 0;
                        for (auto range : {std::pair<uint64_t, uint64_t>{l, boundary}, {boundary, 0}}) {
                            ProofBlockCache cache(iterations, k, l, modulus);
                            uint64_t rows = iterations / (k * l) + (iterations % (k * l) != 0);
                            for (uint64_t cursor = range.first; cursor > range.second; --cursor) {
                                uint64_t j = cursor - 1;
                                for (uint64_t i = 0; i < rows; ++i) {
                                    uint64_t offset = k * (i * l + j + 1);
                                    if (offset > iterations) continue;
                                    integer expected = FastPow(2, iterations - offset, modulus);
                                    mpz_mul_2exp(expected.impl, expected.impl, k);
                                    mpz_tdiv_q(expected.impl, expected.impl, modulus.impl);
                                    auto words = expected.to_vector();
                                    ASSERT_EQ(cache.Get(i, j), words.empty() ? 0 : words[0])
                                        << "T=" << iterations << " k=" << k << " l=" << l << " i=" << i << " j=" << j;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
