#include "verifier.h"
#include "prover_slow.h"
#include "create_discriminant.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

TEST(ProverSlowRegressionTest, ProveSlowHandlesSingleIterationWithoutEmptyProof) {
    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    integer d = CreateDiscriminant(challenge_hash, 1024);
    form x = form::generator(d);

    std::vector<uint8_t> proof_blob = ProveSlow(d, x, 1, "");
    EXPECT_EQ(proof_blob.size(), static_cast<size_t>(2 * BQFC_FORM_SIZE));

    EXPECT_NO_THROW((void)DeserializeForm(d, proof_blob.data(), BQFC_FORM_SIZE));
    EXPECT_NO_THROW((void)DeserializeForm(d, proof_blob.data() + BQFC_FORM_SIZE, BQFC_FORM_SIZE));
}

TEST(ProverSlowRegressionTest, ProveSlowHandlesTinyIterationsThatTriggerKZeroEstimate) {
    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    integer d = CreateDiscriminant(challenge_hash, 1024);
    form x = form::generator(d);

    for (uint64_t iterations : {1ULL, 2ULL}) {
        std::vector<uint8_t> proof_blob = ProveSlow(d, x, iterations, "");
        EXPECT_EQ(proof_blob.size(), static_cast<size_t>(2 * BQFC_FORM_SIZE));
        EXPECT_NO_THROW((void)DeserializeForm(d, proof_blob.data(), BQFC_FORM_SIZE));
        EXPECT_NO_THROW((void)DeserializeForm(d, proof_blob.data() + BQFC_FORM_SIZE, BQFC_FORM_SIZE));
    }
}

TEST(ProverSlowRegressionTest, ProveSlowSucceedsPastFiftyIterationsWithoutCoordinateOverflow) {
    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    integer d = CreateDiscriminant(challenge_hash, 1024);
    form x = form::generator(d);

    for (uint64_t iters : {50ULL, 55ULL, 66ULL}) {
        std::vector<uint8_t> proof_blob = ProveSlow(d, x, iters, "");
        ASSERT_EQ(proof_blob.size(), static_cast<size_t>(2 * BQFC_FORM_SIZE));

        form y = DeserializeForm(d, proof_blob.data(), BQFC_FORM_SIZE);
        form proof = DeserializeForm(d, proof_blob.data() + BQFC_FORM_SIZE, BQFC_FORM_SIZE);

        bool is_valid = false;
        VerifyWesolowskiProof(d, x, y, proof, iters, is_valid);
        EXPECT_TRUE(is_valid);
    }
}

// ApproximateParameters only picks l > 1 above about 2^23.25 iterations, too
// slow for CI, so k and l are given here to cover the multi-round path of
// ProveSlowLowMemory (a bucket set per round, the last-first fold).
TEST(ProverSlowRegressionTest, ProveSlowLowMemoryMatchesProveSlowWithSeveralRounds) {
    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    integer d = CreateDiscriminant(challenge_hash, 1024);
    form x = form::generator(d);

    struct Params { uint64_t k, l; };
    for (Params p : {Params{2, 3}, Params{3, 2}, Params{1, 4}, Params{4, 10}}) {
        uint64_t const kl = p.k * p.l;
        for (uint64_t iters : std::vector<uint64_t>{kl, kl + 1, 50 * kl, 50 * kl + 1, 50 * kl + kl - 1, 397}) {
            std::vector<uint8_t> expected = ProveSlowWithParameters(d, x, iters, "", p.k, p.l);
            ASSERT_EQ(expected.size(), static_cast<size_t>(2 * BQFC_FORM_SIZE));

            form y = DeserializeForm(d, expected.data(), BQFC_FORM_SIZE);
            form proof = DeserializeForm(d, expected.data() + BQFC_FORM_SIZE, BQFC_FORM_SIZE);
            bool is_valid = false;
            VerifyWesolowskiProof(d, x, y, proof, iters, is_valid);
            EXPECT_TRUE(is_valid) << "k=" << p.k << " l=" << p.l << " iters=" << iters;

            for (uint64_t interval : std::vector<uint64_t>{0, 1, kl, 7 * kl + 1, 1000, ~uint64_t(0)}) {
                EXPECT_EQ(ProveSlowLowMemoryWithParameters(d, x, iters, "", interval, p.k, p.l), expected)
                    << "k=" << p.k << " l=" << p.l << " iters=" << iters << " interval=" << interval;
            }
        }
    }
}
