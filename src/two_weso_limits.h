#ifndef TWO_WESO_LIMITS_H
#define TWO_WESO_LIMITS_H

#include <cstddef>
#include <cstdint>

// 2-weso checkpoint layout. Checkpoints are stored every 10 iterations below
// the switch point and every 100 iterations from the switch point on. The
// squaring worker stops kTwoWesoStopMargin iterations before kMaxItersAllowed.
inline constexpr uint64_t kMaxItersAllowed = 1500000000ULL;
inline constexpr uint64_t kTwoWesoStopMargin = 500000;
// The worker switches to the 100-iteration stride at its first checkpoint at or
// past kSwitchIters (checkpoints are 2^15 iterations apart, plus one batch, then
// rounded up to a multiple of 100), so the actual switch point is below
// kSwitchIters + kSwitchSlackIters.
inline constexpr uint64_t kSwitchIters = 91000000;
inline constexpr uint64_t kSwitchSlackIters = uint64_t(1) << 16;
// Largest iteration count a 2-weso session accepts: the worker reaches every count below it.
inline constexpr uint64_t kTwoWesoItersLimit = kMaxItersAllowed - kTwoWesoStopMargin;

#endif // TWO_WESO_LIMITS_H
