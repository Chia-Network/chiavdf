#include "vdf.h"
#include "create_discriminant.h"
#include "prover.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace {

form SquareN(form value, const integer& D, const integer& L, uint64_t steps) {
    PulmarkReducer reducer;
    for (uint64_t i = 0; i < steps; i++) {
        nudupl_form(value, value, D, L);
        reducer.reduce(value);
    }
    return value;
}

class SerialWitness : public Prover {
public:
    SerialWitness(Segment segm, integer D, const std::vector<form>& forms, uint32_t kk, uint32_t ll)
        : Prover(segm, D), forms(forms) {
        k = kk;
        l = ll;
    }
    form GetForm(uint64_t iteration) override { return forms.at(iteration); }
    void start() override { GenerateProof(); }
    void stop() override {}
    bool PerformExtraStep() override { return true; }
    void OnFinish() override { is_finished = true; }
private:
    const std::vector<form>& forms;
};

class ParallelWitness : public ParallelProver {
public:
    ParallelWitness(Segment segm, integer D, const std::vector<form>& forms, uint32_t kk, uint32_t ll,
                    uint8_t segment_threads = PARALLEL_PROVER_N_THREADS)
        : ParallelProver(segm, D, segment_threads), forms(forms) {
        k = kk;
        l = ll;
    }
    form GetForm(uint64_t iteration) override {
        std::lock_guard<std::mutex> lock(mu);
        calls.push_back(iteration);
        threads.insert(std::this_thread::get_id());
        return forms.at(iteration);
    }
    void start() override { GenerateProof(); }
    void stop() override {}
    bool PerformExtraStep() override { return true; }
    void OnFinish() override { is_finished = true; }

    std::mutex mu;
    std::vector<uint64_t> calls;
    std::set<std::thread::id> threads;
private:
    const std::vector<form>& forms;
};

std::vector<uint64_t> NonZeroCalls(uint64_t iterations, uint32_t k, uint32_t l,
                                   const integer& B, uint64_t* eligible) {
    ProofBlockCache cache(iterations, k, l, B);
    uint64_t rows = iterations / (k * l) + (iterations % (k * l) != 0);
    std::vector<uint64_t> calls;
    *eligible = 0;
    for (uint64_t cursor = l; cursor > 0; --cursor) {
        uint64_t j = cursor - 1;
        for (uint64_t i = 0; i < rows; i++) {
            if (iterations < k * (i * l + j + 1)) continue;
            ++*eligible;
            if (cache.Get(i, j)) calls.push_back(i);
        }
    }
    std::sort(calls.begin(), calls.end());
    return calls;
}

uint32_t NonEmptySegmentCount(uint64_t iterations, uint32_t k, uint32_t l,
                              uint32_t n, const integer& B) {
    ProofBlockCache cache(iterations, k, l, B);
    uint64_t rows = iterations / (k * l) + (iterations % (k * l) != 0);
    uint32_t base = l / n;
    uint32_t rem = l % n;
    uint32_t offset = 0;
    uint32_t count = 0;
    for (uint32_t segment = 0; segment < n; segment++) {
        uint32_t len = base + (segment >= n - rem ? 1 : 0);
        uint32_t start = l - offset;
        bool used = false;
        for (uint32_t cursor = start; cursor > start - len && !used; cursor--) {
            uint64_t j = cursor - 1;
            for (uint64_t i = 0; i < rows; i++) {
                if (iterations < k * (i * l + j + 1)) continue;
                if (cache.Get(i, j)) {
                    used = true;
                    break;
                }
            }
        }
        if (used) count++;
        offset += len;
    }
    return count;
}

}  // namespace

TEST(ParallelProverWitness, SkipsZeroBucketsAndMatchesSerialProof) {
    set_rounding_mode();
    integer D(-23);
    integer L = root(-D, 4);
    form x = form::generator(D);
    bool saw_zero = false;

    for (uint32_t k : {4u, 5u}) {
        for (uint32_t l : {2u, 4u}) {
            for (uint64_t iterations : {uint64_t(k * l), uint64_t(k * l + 3), uint64_t(2 * k * l + 1)}) {
                form y = SquareN(x, D, L, iterations);
                Segment seg(0, iterations, x, y);
                integer B = GetB(D, x, y);
                uint64_t eligible = 0;
                std::vector<uint64_t> expected = NonZeroCalls(iterations, k, l, B, &eligible);
                if (expected.size() >= eligible) continue;
                saw_zero = true;

                uint64_t rows = iterations / (k * l) + (iterations % (k * l) != 0);
                std::vector<form> forms(rows);
                forms[0] = x;
                for (uint64_t i = 1; i < rows; i++)
                    forms[i] = SquareN(forms[i - 1], D, L, uint64_t(k) * l);

                SerialWitness serial(seg, D, forms, k, l);
                serial.start();
                ParallelWitness parallel(seg, D, forms, k, l);
                parallel.start();
                ASSERT_TRUE(serial.IsFinished());
                ASSERT_TRUE(parallel.IsFinished());

                std::vector<uint64_t> calls = parallel.calls;
                std::sort(calls.begin(), calls.end());
                EXPECT_EQ(calls, expected);
                EXPECT_LT(calls.size(), eligible);

                form serial_proof = serial.GetProof();
                form parallel_proof = parallel.GetProof();
                EXPECT_TRUE(serial_proof == parallel_proof);
                bool valid = false;
                VerifyWesolowskiProof(D, x, y, parallel_proof, iterations, valid);
                EXPECT_TRUE(valid);
                ASSERT_TRUE(saw_zero);
                return;
            }
        }
    }
    FAIL() << "No parameter set produced a zero bucket";
}

TEST(ParallelProverSegmentThreads, MatchesSerialProofForEveryThreadCount) {
    set_rounding_mode();
    std::vector<uint8_t> seed({0, 0, 1, 2, 3, 3, 4, 4});
    const integer discriminants[] = {
        CreateDiscriminant(seed, 512),
        integer(-23),
    };
    const uint32_t requested_counts[] = {1, 2, 3, 8};
    struct Case {
        uint32_t k;
        uint32_t l;
        uint64_t iterations;
    };
    const Case cases[] = {
        {2, 7, 700},
        {2, 11, 700},
        {3, 2, 700},
        {2, 8, 700},
    };

    for (integer D : discriminants) {
        integer L = root(-D, 4);
        for (const Case& c : cases) {
            form x = form::generator(D);
            form y = SquareN(x, D, L, c.iterations);
            Segment seg(0, c.iterations, x, y);
            integer B = GetB(D, x, y);
            uint64_t rows = c.iterations / (c.k * c.l)
                + (c.iterations % (c.k * c.l) != 0);
            std::vector<form> forms(rows);
            forms[0] = x;
            for (uint64_t i = 1; i < rows; i++)
                forms[i] = SquareN(forms[i - 1], D, L, uint64_t(c.k) * c.l);

            SerialWitness serial(seg, D, forms, c.k, c.l);
            serial.start();
            ASSERT_TRUE(serial.IsFinished());

            ParallelWitness reference(seg, D, forms, c.k, c.l);
            reference.start();
            ASSERT_TRUE(reference.IsFinished());
            EXPECT_TRUE(reference.GetProof() == serial.GetProof());

            ParallelWitness explicit_two(seg, D, forms, c.k, c.l, 2);
            explicit_two.start();
            ASSERT_TRUE(explicit_two.IsFinished());
            EXPECT_TRUE(explicit_two.GetProof() == reference.GetProof());

            std::vector<uint64_t> reference_calls = reference.calls;
            std::sort(reference_calls.begin(), reference_calls.end());
            EXPECT_FALSE(reference_calls.empty());

            for (uint32_t requested : requested_counts) {
                ParallelWitness parallel(seg, D, forms, c.k, c.l,
                                         static_cast<uint8_t>(requested));
                const std::thread::id caller = std::this_thread::get_id();
                parallel.start();
                ASSERT_TRUE(parallel.IsFinished());

                EXPECT_TRUE(parallel.GetProof() == serial.GetProof())
                    << "D=" << D.to_string() << " k=" << c.k << " l=" << c.l
                    << " iterations=" << c.iterations << " n=" << requested;
                bool valid = false;
                VerifyWesolowskiProof(D, x, y, parallel.GetProof(), c.iterations, valid);
                EXPECT_TRUE(valid);
                EXPECT_TRUE(parallel.GetProof() == reference.GetProof());
                EXPECT_TRUE(parallel.GetProof() == explicit_two.GetProof());

                std::vector<uint64_t> calls = parallel.calls;
                std::sort(calls.begin(), calls.end());
                EXPECT_EQ(calls, reference_calls);

                uint32_t n = requested;
                if (n > c.l) n = c.l;
                if (n < 1) n = 1;
                uint32_t nonempty = NonEmptySegmentCount(c.iterations, c.k, c.l, n, B);
                EXPECT_EQ(parallel.threads.size(), nonempty);
                EXPECT_EQ(nonempty, n);
                EXPECT_EQ(parallel.threads.size(), std::min(n, c.l));
                EXPECT_EQ(parallel.threads.count(caller), 1u);
                if (n == 1) {
                    EXPECT_EQ(parallel.threads.size(), 1u);
                    EXPECT_EQ(parallel.threads.count(caller), 1u);
                }
            }
        }
    }
}
