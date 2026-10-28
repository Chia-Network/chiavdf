#include <gmpxx.h>
#include <gtest/gtest.h>

#include <cstdint>

#include "primetest.h"

namespace {

const char* const kPrime1024Hex =
    "0xc8fd905ec4869c162aa4fde5f2630fd51a44784a380aaff309d9df647b2060fc5e63b9bceb1"
    "9113e8f3f2291397239ed415c9cd4e5e600e574204eec5a39884cfc9fe40f1f6c68b533b5c5721"
    "512c8924cb8792668a2e5eab823c27c5dc8509b37455a314514fa25a82584bc764500518cf9e43"
    "931151c41d824a2ba1f3e0897";

const char* const kComposite1024Hex =
    "0xc01bcf1c6456e52f8945057d1f7d3d48a7baa6577504b4aa22c45b00cce31640f2d095d0b2"
    "b2d55d5e87ea21629d8c873905fc7f19f965f0466537c4898cd2604b82be985545bb745df1fad6"
    "fc9fa0ef268f85d8a5dae77e4ba9e4aa2ea2f484b3e469e0572a7df8630f74cdaa1ae271ae1333"
    "feaaceeca87916662556c311c8bd229d1628668141f0a5e8cbeacff8b1b4c9736da5567f6ab659"
    "61ae833cfc93049f9d16b9d4ab6b302c83ece81e0338baeaebf8cf685a325c2b5c23071c92e616"
    "8c124fd3868336c1ea3510f27a7c8390b32cd785518eab0aceff64cb0636e449354ddc8d7cc3d0"
    "95c22ef1617a7e217fb27d532c6a66e46081d142f50d2ae3";

void set_hex(mpz_t value, const char* hex) {
    ASSERT_EQ(mpz_set_str(value, hex, 0), 0);
}

}  // namespace

TEST(PrimalitySmallNumbers, MatchesReferenceBelow65536) {
    mpz_class value;
    for (int n = -8; n < 65536; ++n) {
        SCOPED_TRACE(::testing::Message() << "n = " << n);
        mpz_set_si(value.get_mpz_t(), n);
        const bool expected =
            n < 2 ? false : mpz_probab_prime_p(value.get_mpz_t(), 25) != 0;
        EXPECT_EQ(is_prime_bpsw(value.get_mpz_t()) != 0, expected);
    }
}

TEST(PrimalitySmallNumbers, KnownLargeVectors) {
    mpz_class prime;
    set_hex(prime.get_mpz_t(), kPrime1024Hex);
    EXPECT_NE(mpz_probab_prime_p(prime.get_mpz_t(), 25), 0);
    EXPECT_NE(is_prime_bpsw(prime.get_mpz_t()), 0);

    mpz_class composite;
    set_hex(composite.get_mpz_t(), kComposite1024Hex);
    EXPECT_EQ(mpz_probab_prime_p(composite.get_mpz_t(), 25), 0);
    EXPECT_EQ(is_prime_bpsw(composite.get_mpz_t()), 0);

    mpz_class prime_times_five;
    mpz_mul_ui(prime_times_five.get_mpz_t(), prime.get_mpz_t(), 5);
    EXPECT_EQ(is_prime_bpsw(prime_times_five.get_mpz_t()), 0);
}
