#include "checked_cast.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

TEST(CheckedCastTest, UnsignedToSignedWithinRangeSucceeds) {
    const size_t value = static_cast<size_t>(std::numeric_limits<int32_t>::max());
    EXPECT_EQ(checked_cast<int32_t>(value), std::numeric_limits<int32_t>::max());
}

TEST(CheckedCastTest, UnsignedToSignedOutOfRangeThrows) {
    const size_t value = static_cast<size_t>(std::numeric_limits<int32_t>::max()) + 1;
    EXPECT_THROW((void)checked_cast<int32_t>(value), std::overflow_error);
}

TEST(CheckedCastTest, SignedNarrowerToWiderWithinRangeSucceeds) {
    const int32_t value = 42;
    EXPECT_EQ(checked_cast<int64_t>(value), 42);
}

TEST(CheckedCastTest, SignedNarrowerToWiderNegativeWithinRangeSucceeds) {
    const int32_t value = -7;
    EXPECT_EQ(checked_cast<int64_t>(value), -7);
}
