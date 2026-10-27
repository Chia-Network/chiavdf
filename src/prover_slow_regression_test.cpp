#include "verifier.h"
#include "prover_slow.h"
#include "create_discriminant.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>
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

static uint64_t ReferenceCeilDiv(uint64_t n, uint64_t d) {
    // 128-bit division needs the __udivti3 runtime routine, which the
    // clang-cl/lld-link Windows build does not link, so Windows uses a 64-bit
    // reference computed as (n - 1) / d + 1 for n > 0.
#if !defined(_WIN32) && !defined(_MSC_VER) && defined(__SIZEOF_INT128__)
    return static_cast<uint64_t>((static_cast<unsigned __int128>(n) + d - 1) / d);
#else
    return (n == 0) ? 0 : (n - 1) / d + 1;
#endif
}

TEST(ProverSlowRegressionTest, CeilDivDoesNotWrap) {
    EXPECT_EQ(CeilDiv(UINT64_MAX, 360), UINT64_MAX / 360 + 1);
    EXPECT_EQ(CeilDiv(UINT64_MAX - 358, 360), UINT64_MAX / 360);
    EXPECT_EQ(CeilDiv(UINT64_MAX - 359, 360), UINT64_MAX / 360);
    EXPECT_EQ(CeilDiv(UINT64_MAX - 360, 360), UINT64_MAX / 360);

    EXPECT_EQ(CeilDiv(0, 360), 0u);
    EXPECT_EQ(CeilDiv(360, 360), 1u);
    EXPECT_EQ(CeilDiv(720, 360), 2u);
    EXPECT_EQ(CeilDiv(721, 360), 3u);
    EXPECT_EQ(CeilDiv(UINT64_MAX, 1), UINT64_MAX);
    EXPECT_EQ(CeilDiv(0, 1), 0u);
    EXPECT_EQ(CeilDiv(1, 1), 1u);
    EXPECT_EQ(CeilDiv(UINT64_MAX, UINT64_MAX), 1u);
    EXPECT_EQ(CeilDiv(UINT64_MAX - 1, UINT64_MAX), 1u);

    const uint64_t numerators[] = {0, 1, 359, 360, 361,
                                   UINT64_MAX - 360, UINT64_MAX - 359,
                                   UINT64_MAX - 358, UINT64_MAX - 1, UINT64_MAX};
    const uint64_t denominators[] = {1, 2, 3, 359, 360, 361, UINT64_MAX};
    for (uint64_t n : numerators) {
        for (uint64_t d : denominators) {
            uint64_t const expected = ReferenceCeilDiv(n, d);
            EXPECT_EQ(CeilDiv(n, d), expected) << "n=" << n << " d=" << d;
        }
    }
}

TEST(ProverSlowRegressionTest, ProveSlowLargeNumIterationsDoesNotWrapSizeVec) {
#if defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "allocation-size aborts under AddressSanitizer";
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
    GTEST_SKIP() << "allocation-size aborts under AddressSanitizer";
#  endif
#endif

    struct DeathTestStyleRestorer {
        DeathTestStyleRestorer() : saved(GTEST_FLAG_GET(death_test_style)) {}
        ~DeathTestStyleRestorer() { GTEST_FLAG_SET(death_test_style, saved); }
        std::string saved;
    } death_test_style_restorer;

    GTEST_FLAG_SET(death_test_style, "threadsafe");

    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    integer d = CreateDiscriminant(challenge_hash, 1024);
    form x = form::generator(d);

    // Huge values must fail with an exception (allocation failure) rather than
    // wrapping the vector size.
    auto probe = [&](uint64_t num_iterations) {
        EXPECT_EXIT(
            {
                try {
                    ProveSlow(d, x, num_iterations, "");
                } catch (const std::exception&) {
                }
                std::exit(0);
            },
            ::testing::ExitedWithCode(0), "");
    };

    for (uint64_t value : {UINT64_MAX, UINT64_MAX - 358, UINT64_MAX - 359,
                           UINT64_MAX - 360, uint64_t(1) << 63}) {
        probe(value);
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
