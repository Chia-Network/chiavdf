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

TEST(TwoWesolowskiCallbackRegressionTest, RejectsOutOfBoundsFormLookupAtCapacityLimit) {
    integer d = make_fixture_discriminant();
    form f = form::generator(d);
    TwoWesolowskiCallback callback(d, f);
    callback.IncreaseConstants(kSwitchIters);

    EXPECT_NO_THROW((void)callback.GetFormCopy(kMaxItersAllowed - 1000));
    EXPECT_NO_THROW((void)callback.GetFormCopy(kMaxItersAllowed));
    EXPECT_THROW((void)callback.GetFormCopy(kMaxItersAllowed + 1000), std::runtime_error);
}

TEST(TwoWesolowskiCallbackRegressionTest, UsesWideStrideIndexingPastWideStrideIters) {
    integer d = make_fixture_discriminant();
    form f = form::generator(d);
    TwoWesolowskiCallback callback(d, f);
    callback.IncreaseConstants(kSwitchIters);

    const size_t switch_index = static_cast<size_t>(kSwitchIters / 10);
    const size_t wide_index =
        switch_index + static_cast<size_t>((kWideStrideIters - kSwitchIters) / 100);

    EXPECT_EQ(callback.GetPosition(kWideStrideIters - 100), wide_index - 1);
    EXPECT_EQ(callback.GetPosition(kWideStrideIters), wide_index);
    EXPECT_EQ(callback.GetPosition(kWideStrideIters + 999), wide_index);
    EXPECT_EQ(callback.GetPosition(kWideStrideIters + 1000), wide_index + 1);
    EXPECT_EQ(
        callback.GetPosition(kMaxItersAllowed),
        wide_index + static_cast<size_t>((kMaxItersAllowed - kWideStrideIters) / 1000)
    );
}

TEST(TwoWesolowskiCallbackRegressionTest, KeepsDenseIndexingBelowOldLimit) {
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

TEST(TwoWesolowskiCallbackRegressionTest, RejectsInvalidLimits) {
    integer d = make_fixture_discriminant();
    form f = form::generator(d);

    auto expect_invalid = [&](const TwoWesoLimits& limits) {
        EXPECT_THROW(TwoWesolowskiCallback(d, f, limits), std::invalid_argument);
    };

    TwoWesoLimits zero_switch;
    zero_switch.switch_iters = 0;
    expect_invalid(zero_switch);

    TwoWesoLimits misaligned_switch;
    misaligned_switch.switch_iters = 12345;
    expect_invalid(misaligned_switch);

    TwoWesoLimits misaligned_wide;
    misaligned_wide.switch_iters = 9100;
    misaligned_wide.wide_stride_iters = 80500;
    expect_invalid(misaligned_wide);

    TwoWesoLimits switch_not_before_wide;
    switch_not_before_wide.switch_iters = switch_not_before_wide.wide_stride_iters;
    expect_invalid(switch_not_before_wide);

    TwoWesoLimits max_too_close;
    max_too_close.max_iters = max_too_close.wide_stride_iters + 500000;
    expect_invalid(max_too_close);
}

TEST(TwoWesolowskiCallbackRegressionTest, RejectsSwitchPastWideStrideIters) {
    integer d = make_fixture_discriminant();
    form f = form::generator(d);
    TwoWesolowskiCallback callback(d, f);

    EXPECT_THROW(callback.IncreaseConstants(kWideStrideIters), std::logic_error);
}

TEST(TwoWesolowskiCallbackRegressionTest, UnwrittenCheckpointReadsAsDefaultForm) {
    integer d = make_fixture_discriminant();
    form f = form::generator(d);
    TwoWesolowskiCallback callback(d, f);
    callback.IncreaseConstants(kSwitchIters);

    form stored;
    ASSERT_NO_THROW(stored = callback.GetFormCopy(kWideStrideIters));

    const form empty;
    EXPECT_TRUE(stored.a == empty.a);
    EXPECT_TRUE(stored.b == empty.b);
    EXPECT_TRUE(stored.c == empty.c);
}
