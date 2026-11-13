#include "create_discriminant.h"
#include "picosha2.h"
#include "vdf.h"
#include "verifier.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
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

std::vector<std::string> RunTwoWesoProofs(const std::vector<uint64_t>& counts) {
    init_gmp();
    set_rounding_mode();

    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    integer D = CreateDiscriminant(challenge_hash, 1024);
    integer L = root(-D, 4);
    form f = form::generator(D);

    TwoWesoGlobalStateGuard global_state_guard;
    fast_algorithm = false;
    two_weso = true;

    TwoWesolowskiCallback weso(D, f);
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

TEST(TwoWesoHeadroomTest, ProofsMatchRecordedOutput) {
    // 1000000 crosses the first 64K-block edge at iteration 655360.
    const std::vector<uint64_t> counts = {1000, 15000, 100000, 1000000};
    const std::vector<std::string> hashes = RunTwoWesoProofs(counts);
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
