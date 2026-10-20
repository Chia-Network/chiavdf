#include "vdf.h"
#include "prover.hpp"
#include "verifier.h"
#include "create_discriminant.h"
#include <gtest/gtest.h>

namespace {
template<class Base>
class ArithmeticFixtureProver : public Base {
    const std::vector<form>& values;
public:
    ArithmeticFixtureProver(Segment segment, integer d, const std::vector<form>& values, uint32_t k)
        : Base(segment, d), values(values) {
        this->k = k;
        this->l = 24 / k;
    }
    form GetForm(uint64_t row) override { return values.at(row * 24); }
    void start() override { this->GenerateProof(); }
    void stop() override {}
    bool PerformExtraStep() override { return true; }
    void OnFinish() override { this->is_finished = true; }
};

void euclidean_partial(integer& co2, integer& co1, integer& r2, integer& r1, const integer& limit) {
    co2 = integer(0);
    co1 = integer(-1);
    while (r1 != integer(0) && r1 > limit) {
        integer quotient = r2 / r1;
        integer remainder = r2 - quotient * r1;
        integer cofactor = co2 - quotient * co1;
        r2 = r1;
        r1 = remainder;
        co2 = co1;
        co1 = cofactor;
    }
}
}

TEST(ProofArithmeticRegressionTest, SerialAndParallelMatchDirectWesolowskiExponent) {
    set_rounding_mode();
    std::vector<uint8_t> seed = {0, 0, 1, 2, 3, 3, 4, 4};
    integer d = CreateDiscriminant(seed, 1024), limit = root(-d, 4);
    PulmarkReducer reducer;
    std::vector<form> values{form::generator(d)};
    for (size_t i = 1; i <= 513; ++i) {
        form value = values.back();
        nudupl_form(value, value, d, limit);
        reducer.reduce(value);
        values.push_back(value);
    }
    for (uint64_t length : {1, 23, 24, 25, 511, 512, 513}) {
        Segment segment(0, length, values.front(), values[length]);
        integer challenge = GetB(d, segment.x, segment.y), exponent(0);
        mpz_setbit(exponent.impl, length);
        exponent = exponent / challenge;
        form expected = FastPowFormNucomp(segment.x, d, exponent, limit, reducer);
        reducer.reduce(expected);
        for (uint32_t k : {1, 2, 3, 4, 6, 8}) {
            ArithmeticFixtureProver<Prover> serial(segment, d, values, k);
            ArithmeticFixtureProver<ParallelProver> parallel(segment, d, values, k);
            serial.start();
            parallel.start();
            for (auto* prover : std::vector<Prover*>{&serial, &parallel}) {
                form actual = prover->GetProof();
                bool valid = false;
                VerifyWesolowskiProof(d, segment.x, segment.y, actual, length, valid);
                ASSERT_TRUE(valid) << "length=" << length << " k=" << k;
                EXPECT_EQ(SerializeForm(actual, d.num_bits()), SerializeForm(expected, d.num_bits()))
                    << "length=" << length << " k=" << k;
            }
        }
    }
}

TEST(ProofArithmeticRegressionTest, PartialGcdMatchesEuclideanRemaindersAndCofactors) {
    gmp_randstate_t state;
    gmp_randinit_mt(state);
    gmp_randseed_ui(state, 20261014);
    for (unsigned bits : {1, 32, 63, 64, 65, 128, 255, 256, 512, 1024, 2048}) {
        for (unsigned sample = 0; sample < 32; ++sample) {
            integer a, b, limit, co2, co1;
            mpz_urandomb(a.impl, state, bits);
            mpz_urandomb(b.impl, state, bits);
            if (a < b) mpz_swap(a.impl, b.impl);
            limit = b >> (sample % 4);
            if (sample % 7 == 0) limit = integer(0);
            integer expected_a = a, expected_b = b, expected_co2, expected_co1;
            euclidean_partial(expected_co2, expected_co1, expected_a, expected_b, limit);
            mpz_xgcd_partial(co2.impl, co1.impl, a.impl, b.impl, limit.impl);
            EXPECT_EQ(a.to_string(), expected_a.to_string());
            EXPECT_EQ(b.to_string(), expected_b.to_string());
            EXPECT_EQ(co2.to_string(), expected_co2.to_string());
            EXPECT_EQ(co1.to_string(), expected_co1.to_string());
        }
    }
    gmp_randclear(state);
}
