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
