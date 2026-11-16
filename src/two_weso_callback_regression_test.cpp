#include "create_discriminant.h"
#include "vdf.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

// Needed by headers pulled in via vdf.h (declared extern in parameters.h).
int gcd_base_bits = 50;
int gcd_128_max_iter = 3;

namespace {

integer make_fixture_discriminant() {
    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    return CreateDiscriminant(challenge_hash, 1024);
}

}  // namespace

TEST(TwoWesolowskiCallbackRegressionTest, UsesDenseIndexingBeforeTransition) {
    integer d = make_fixture_discriminant();
    form f = form::generator(d);
    TwoWesolowskiCallback callback(d, f);

    EXPECT_FALSE(callback.LargeConstants());
    EXPECT_EQ(callback.GetPosition(0), static_cast<size_t>(0));
    EXPECT_EQ(callback.GetPosition(10), static_cast<size_t>(1));
    EXPECT_EQ(
        callback.GetPosition(static_cast<uint64_t>(kSwitchIters - 10)),
        static_cast<size_t>((kSwitchIters - 10) / 10)
    );
}

TEST(TwoWesolowskiCallbackRegressionTest, SwitchesToSparseIndexingAtTransitionBoundary) {
    integer d = make_fixture_discriminant();
    form f = form::generator(d);
    TwoWesolowskiCallback callback(d, f);
    callback.IncreaseConstants(kSwitchIters);

    const size_t switch_index = static_cast<size_t>(kSwitchIters / 10);
    EXPECT_TRUE(callback.LargeConstants());
    EXPECT_EQ(callback.GetPosition(static_cast<uint64_t>(kSwitchIters - 10)), switch_index - 1);
    EXPECT_EQ(callback.GetPosition(kSwitchIters), switch_index);
    EXPECT_EQ(callback.GetPosition(static_cast<uint64_t>(kSwitchIters + 99)), switch_index);
    EXPECT_EQ(callback.GetPosition(static_cast<uint64_t>(kSwitchIters + 100)), switch_index + 1);
}

TEST(TwoWesolowskiCallbackRegressionTest, TransitionMathSupportsNonRoundedSwitchInput) {
    integer d = make_fixture_discriminant();
    form f = form::generator(d);
    TwoWesolowskiCallback callback(d, f);

    const uint64_t custom_switch_iters = 12345;
    callback.IncreaseConstants(custom_switch_iters);

    const size_t switch_index = static_cast<size_t>(custom_switch_iters / 10);
    EXPECT_EQ(callback.GetPosition(custom_switch_iters), switch_index);
    EXPECT_EQ(callback.GetPosition(custom_switch_iters + 100), switch_index + 1);
}

TEST(TwoWesolowskiCallbackRegressionTest, RejectsFormLookupPastCapacity) {
    integer d = make_fixture_discriminant();
    form f = form::generator(d);
    TwoWesolowskiCallback callback(d, f);
    callback.IncreaseConstants(kSwitchIters);

    EXPECT_THROW(
        (void)callback.GetFormCopy(kMaxItersAllowed + 100000000),
        std::runtime_error
    );
}

TEST(TwoWesolowskiCallbackRegressionTest, IndexingFollowsTwoStrideLayout) {
    integer d = make_fixture_discriminant();
    form f = form::generator(d);
    TwoWesolowskiCallback callback(d, f);
    callback.IncreaseConstants(kSwitchIters);

    const uint64_t switch_iters = static_cast<uint64_t>(kSwitchIters);
    auto expected_position = [switch_iters](uint64_t power) -> size_t {
        if (power < switch_iters) {
            return static_cast<size_t>(power / 10);
        }
        return static_cast<size_t>(switch_iters / 10 + (power - switch_iters) / 100);
    };

    const std::vector<uint64_t> powers = {
        0,
        10,
        switch_iters - 10,
        switch_iters,
        switch_iters + 100,
        799999900,
    };
    for (uint64_t power : powers) {
        EXPECT_EQ(callback.GetPosition(power), expected_position(power)) << "power=" << power;
    }
}

TEST(TwoWesolowskiCallbackRegressionTest, ThrowsOnCheckpointNotComputedYet) {
    integer d = make_fixture_discriminant();
    form f = form::generator(d);
    TwoWesolowskiCallback callback(d, f);
    callback.IncreaseConstants(kSwitchIters);

    EXPECT_NO_THROW((void)callback.GetFormCopy(0));
    EXPECT_THROW((void)callback.GetFormCopy(kSwitchIters), std::logic_error);
}
