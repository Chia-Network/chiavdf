#include "vdf.h"
#include "prover.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <mutex>
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
    ParallelWitness(Segment segm, integer D, const std::vector<form>& forms, uint32_t kk, uint32_t ll)
        : ParallelProver(segm, D), forms(forms) {
        k = kk;
        l = ll;
    }
    form GetForm(uint64_t iteration) override {
        std::lock_guard<std::mutex> lock(mu);
        calls.push_back(iteration);
        return forms.at(iteration);
    }
    void start() override { GenerateProof(); }
    void stop() override {}
    bool PerformExtraStep() override { return true; }
    void OnFinish() override { is_finished = true; }

    std::mutex mu;
    std::vector<uint64_t> calls;
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
