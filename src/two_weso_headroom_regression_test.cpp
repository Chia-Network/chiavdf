#include "create_discriminant.h"
#include "picosha2.h"
#include "vdf.h"
#include "verifier.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

class TwoWesoGlobalStateGuard {
  public:
    TwoWesoGlobalStateGuard()
        : fast_algorithm_(fast_algorithm), two_weso_(two_weso) {}

    ~TwoWesoGlobalStateGuard() {
        fast_algorithm = fast_algorithm_;
        two_weso = two_weso_;
    }

  private:
    bool fast_algorithm_;
    bool two_weso_;
};

class TwoWesoWorkerGuard {
  public:
    TwoWesoWorkerGuard(std::thread& worker, std::atomic<bool>& stopped)
        : worker_(worker), stopped_(stopped) {}

    ~TwoWesoWorkerGuard() { Stop(); }

    void Stop() {
        stopped_ = true;
        if (worker_.joinable()) {
            worker_.join();
        }
    }

  private:
    std::thread& worker_;
    std::atomic<bool>& stopped_;
};

TwoWesoLimits ScaledLimits(uint32_t block_shift) {
    TwoWesoLimits limits;
    limits.switch_threshold_iters = 9100;
    limits.wide_stride_iters = 80000;
    limits.max_iters = 1500000;
    limits.block_shift = block_shift;
    return limits;
}

TwoWesoProverTuning ScaledTuning() {
    TwoWesoProverTuning tuning;
    tuning.small_segment_iters = 1000;
    return tuning;
}

std::vector<std::string> ScaledExpectedHashes() {
    // These were recorded with the same scaled limits before the storage rework (the checkpoint layout and proofs do not depend on the block size).
    return {
        "be254f04a5f24d96a311054d4862473669895deaa4c942fa638d228dce413fc5",
        "dd5623891108eae2c41f2637ccbdab4db57887cce1f640d103491e417e461134",
        "729a41ff293ac5f3bce59b142203e50af15521627104d03dd0e5a8424302e832",
        "aea8af5d23f676399fc4d5793e4630aa304737625a4fb47d2553f67ff86decff",
        "1e830d000ebe452f16b2b7b262b9536f3e75054c4d11d0a3a8013781002c6592",
        "62808bc919996853e7c6a49800d4967e22bf35c0afc97d490983be6eedccb1af",
        "6067aa9c63707ce59b6b9cf8c1489296aa41154a7364e3a9cb709d8ca4318e67",
        "0ec2c422c91c97b6f76ba8f65528330c3ea8b99a4053621c64259f534150853f",
    };
}

std::vector<std::string> RunTwoWesoProofs(
    const TwoWesoLimits& limits,
    const TwoWesoProverTuning& tuning,
    const std::vector<uint64_t>& counts) {
    init_gmp();
    set_rounding_mode();

    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    integer D = CreateDiscriminant(challenge_hash, 1024);
    integer L = root(-D, 4);
    form f = form::generator(D);

    TwoWesoGlobalStateGuard global_state_guard;
    fast_algorithm = false;
    two_weso = true;

    TwoWesolowskiCallback weso(D, f, limits, tuning);
    std::atomic<bool> stopped{false};
    std::thread worker(
        repeated_square,
        0,
        f,
        std::ref(D),
        std::ref(L),
        &weso,
        static_cast<FastStorage*>(nullptr),
        std::ref(stopped));
    TwoWesoWorkerGuard worker_guard(worker, stopped);

    std::vector<std::string> hashes;
    hashes.reserve(counts.size());
    for (uint64_t count : counts) {
        Proof proof = ProveTwoWeso(D, f, count, 0, &weso, 0, stopped);

        std::vector<uint8_t> bytes(proof.y.begin(), proof.y.end());
        bytes.insert(bytes.end(), proof.proof.begin(), proof.proof.end());

        EXPECT_TRUE(CheckProofOfTimeNWesolowski(
            D,
            DEFAULT_ELEMENT,
            bytes.data(),
            bytes.size(),
            count,
            1024,
            proof.witness_type));
        EXPECT_EQ(proof.witness_type, 2);
        hashes.push_back(picosha2::hash256_hex_string(bytes));
    }

    worker_guard.Stop();
    return hashes;
}

}  // namespace

TEST(TwoWesoHeadroomTest, ProofsMatchRecordedOutputBelowOldLimit) {
    const std::vector<uint64_t> counts = {1000, 15000, 100000, 1000000};
    const std::vector<std::string> hashes =
        RunTwoWesoProofs(TwoWesoLimits(), TwoWesoProverTuning(), counts);
    const std::vector<std::string> expected = {
        "f102f062449d2da1d285fe31a24ecf148c0c1611822adac1f93d3b587e48e2b6",
        "367f43655fa2a44845b039e8699918839987179bf6fabd509ab24a7580f79f5d",
        "4bc8650bc32ce357682f4a6009a03cfb1995c41c57a12f9afc5245e91abe70ff",
        "b9051c6725788bebc775e77c86e4a6b91aed8844cac33dda6111af3b96bc3ca4",
    };

    ASSERT_EQ(hashes.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(hashes[i], expected[i]) << "count=" << counts[i];
    }
}

TEST(TwoWesoHeadroomTest, ProofsMatchRecordedOutputWithScaledLimits) {
    const std::vector<uint64_t> counts = {
        50000, 79900, 79999, 80000, 80001, 120000, 450000, 999000,
    };
    const std::vector<std::string> hashes =
        RunTwoWesoProofs(ScaledLimits(8), ScaledTuning(), counts);
    const std::vector<std::string> expected = ScaledExpectedHashes();

    ASSERT_EQ(hashes.size(), counts.size());
    ASSERT_EQ(expected.size(), counts.size());
    for (size_t i = 0; i < counts.size(); i++) {
        EXPECT_EQ(hashes[i], expected[i]) << "count=" << counts[i];
    }
}

TEST(TwoWesoHeadroomTest, ScaledLayoutMatchesWithDefaultBlockSize) {
    const std::vector<uint64_t> counts = {79999, 80001, 450000};
    const std::vector<size_t> expected_indices = {2, 4, 6};
    const std::vector<std::string> hashes =
        RunTwoWesoProofs(ScaledLimits(16), ScaledTuning(), counts);
    const std::vector<std::string> expected = ScaledExpectedHashes();

    ASSERT_EQ(hashes.size(), counts.size());
    ASSERT_EQ(expected_indices.size(), counts.size());
    for (size_t i = 0; i < counts.size(); i++) {
        EXPECT_EQ(hashes[i], expected[expected_indices[i]]) << "count=" << counts[i];
    }
}

TEST(TwoWesoHeadroomTest, ShortSegmentsPastSwitchUseStoredStride) {
    // A small segment setting above switch_threshold_iters / 9 leaves segments
    // that end past the switch point shorter than the segment size, so they must
    // rely on the stored wide stride instead of stepping every 10 iterations.
    TwoWesoProverTuning tuning;
    tuning.small_segment_iters = 5000;

    const std::vector<uint64_t> counts = {40000, 45000, 60000};
    const std::vector<std::string> hashes =
        RunTwoWesoProofs(ScaledLimits(8), tuning, counts);

    EXPECT_EQ(hashes.size(), counts.size());
}
