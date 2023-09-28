#include "vdf_assert.h"
#include "vdf.h"

int main(int argc, char **argv)
{
    char small_primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61};
    int i;
    uint64_t small_primes_bitmap = 0;
    mpz_t n;

    for (i = 0; i < sizeof(small_primes); i++) {
        small_primes_bitmap |= 1UL << small_primes[i];
    }

    mpz_init(n);
    for (i = 0; i < 64; i++) {
        /* Test all numbers from 0 to 63. */
        mpz_set_si(n, i);
        VDF_ASSERT(!!is_prime_bpsw(n) == !!(small_primes_bitmap & 1UL << i));
    }

    /* Test rejection of negative numbers. */
    mpz_set_si(n, -11);
    VDF_ASSERT(!is_prime_bpsw(n));

    /* Known prime. */
    mpz_set_str(n, "0xc8fd905ec4869c162aa4fde5f2630fd51a44784a380aaff309d9df647b2060fc5e63b9bceb19113e8f3f2291397239ed415c9cd4e5e600e574204eec5a39884cfc9fe40f1f6c68b533b5c5721512c8924cb8792668a2e5eab823c27c5dc8509b37455a314514fa25a82584bc764500518cf9e43931151c41d824a2ba1f3e0897", 0);
    VDF_ASSERT(is_prime_bpsw(n));

    /* Make n composite. */
    mpz_mul_ui(n, n, 5);
    VDF_ASSERT(!is_prime_bpsw(n));

    /* Known composite. */
    mpz_set_str(n, "0xc01bcf1c6456e52f8945057d1f7d3d48a7baa6577504b4aa22c45b00cce31640f2d095d0b2b2d55d5e87ea21629d8c873905fc7f19f965f0466537c4898cd2604b82be985545bb745df1fad6fc9fa0ef268f85d8a5dae77e4ba9e4aa2ea2f484b3e469e0572a7df8630f74cdaa1ae271ae1333feaaceeca87916662556c311c8bd229d1628668141f0a5e8cbeacff8b1b4c9736da5567f6ab65961ae833cfc93049f9d16b9d4ab6b302c83ece81e0338baeaebf8cf685a325c2b5c23071c92e6168c124fd3868336c1ea3510f27a7c8390b32cd785518eab0aceff64cb0636e449354ddc8d7cc3d095c22ef1617a7e217fb27d532c6a66e46081d142f50d2ae3", 0);
    VDF_ASSERT(!is_prime_bpsw(n));

    fprintf(stderr, "Test of primes successful!\n");
    mpz_clear(n);
    return 0;
}
