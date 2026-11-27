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

TEST(ProofArithmeticRegressionTest, FastPowPreservesFullWidthBaseAndExponent) {
    integer modulus("0x100000000000000000000000000000033");
    for (uint64_t base : {uint64_t(0), uint64_t(1), uint64_t(UINT32_MAX - 1),
                          uint64_t(UINT32_MAX), uint64_t(1) << 32,
                          uint64_t(1) << 63, UINT64_MAX}) {
        EXPECT_EQ(integer::from_uint64(base).to_string(), integer(std::to_string(base)).to_string());
        for (uint64_t exponent : {uint64_t(0), uint64_t(1), uint64_t(1) << 32,
                                  uint64_t(1) << 63, UINT64_MAX}) {
            integer reference_base(std::to_string(base)), reference_exponent(std::to_string(exponent));
            integer expected;
            mpz_powm(expected.impl, reference_base.impl, reference_exponent.impl, modulus.impl);
            EXPECT_EQ(FastPow(base, exponent, modulus).to_string(), expected.to_string());
        }
    }
}

TEST(ProofArithmeticRegressionTest, ShiftedWordExtractionMatchesGmp) {
    integer value("0xfedcba98765432100123456789abcdef0123456789abcdef");
    for (uint32_t shift : {0, 1, 63, 64, 65, 127, 128, 129, 191, 192, 193, 4096}) {
        integer shifted;
        mpz_tdiv_q_2exp(shifted.impl, value.impl, shift);
        EXPECT_EQ(static_cast<mp_limb_t>(chiavdf_mpz_extract_uword_from_shift_nonneg(value.impl, shift)),
                  mpz_getlimbn(shifted.impl, 0));
    }
    EXPECT_EQ(chiavdf_mpz_extract_uword_from_shift_nonneg(value.impl, INT64_MAX), 0);
    integer zero(0);
    EXPECT_EQ(chiavdf_mpz_extract_uword_from_shift_nonneg(zero.impl, 1), 0);
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
    for (uint32_t length : {1, 23, 24, 25, 511, 512, 513}) {
        Segment segment(0, length, values.front(), values[length]);
        integer challenge = GetB(d, segment.x, segment.y), exponent(0);
        chiavdf::checked_mpz_setbit(exponent.impl, length);
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
