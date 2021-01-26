#ifndef PRIMETEST_H
#define PRIMETEST_H

#include "pprods.h"

static int miller_rabin(const mpz_t n, mpz_t b, mpz_t d)
{
    int r = 0, s = mpz_scan1(n, 1);

    mpz_tdiv_q_2exp(d, n, s);
    mpz_powm(b, b, d, n);
    if (!mpz_cmp_ui(b, 1))
        return 1;

    while (1) {
        mpz_add_ui(b, b, 1);
        if (!mpz_cmp(b, n))
            return 1;
        r++;
        if (r == s)
            return 0;
        mpz_sub_ui(b, b, 1);
        mpz_powm_ui(b, b, 2, n);
    }
}

#define VPRP_MAX_D 1000

static int find_pq(int *out_p, int *out_q, const mpz_t n, mpz_t tmp)
{
    int d = 5;

    for (; d < VPRP_MAX_D; d = abs(d) + 2) {
        d = d % 4 == 1 ? d : -d;
        mpz_set_si(tmp, d);
        if (mpz_jacobi(tmp, n) == -1) {
            if (d == 5) {
                *out_p = 5;
                *out_q = 5;
            } else {
                *out_p = 1;
                *out_q = (1 - d) / 4;
            }
            return 0;
        }
    }

    /* Failed to find suitable d < VPRP_MAX_D. */
    return 1;
}

static void addmul_si(mpz_t p, mpz_t a, int b)
{
    if (b < 0)
        mpz_submul_ui(p, a, -b);
    else
        mpz_addmul_ui(p, a, b);
}

static void find_lucas_v(mpz_t u1, mpz_t e, const mpz_t m, int p, int q,
        mpz_t u2, mpz_t tmp2)
{
    int i, l = mpz_sizeinbase(e, 2), minus_2q = -2 * q;

    mpz_set_ui(u1, 1); /* U1 */
    mpz_set_ui(u2, p); /* U2 */

    for (i = l - 2; i >= 0; i--) {
        mpz_mul(tmp2, u2, u1); /* U1 * U2 */
        mpz_mul(u2, u2, u2);  /* U2**2 */
        mpz_mul(u1, u1, u1);  /* U1**2 */
        if (mpz_tstbit(e, i)) {
            mpz_mul_si(u1, u1, q);
            mpz_sub(u1, u2, u1); /* U1 = U2**2 - Q * U1**2 */

            if (p != 1) {
                mpz_mul_ui(u2, u2, p);
            }
            addmul_si(u2, tmp2, minus_2q);

        } else {
            addmul_si(u2, u1, -q); /* U2 = U2**2 - Q * U1**2 */
            mpz_mul_2exp(tmp2, tmp2, 1);
            if (p != 1) {
                mpz_submul_ui(tmp2, u1, p);
                mpz_swap(tmp2, u1);
            } else {
                mpz_sub(u1, tmp2, u1);
            }
        }
        mpz_tdiv_r(u1, u1, m);
        mpz_tdiv_r(u2, u2, m);
    }
    /* V1 = 2 * U2 - P * U1 */
    mpz_mul_2exp(u2, u2, 1);
    mpz_submul_ui(u2, u1, p);
    mpz_swap(u1, u2); /* Place V1 into U1 */
    /*mpz_tdiv_r(u1, u1, m);*/
}

static int is_vprp(const mpz_t n, mpz_t tmp1, mpz_t tmp2)
{
    int p, q, res = 0;
    mpz_t e, v;

    mpz_inits(v, e, NULL);

    if (find_pq(&p, &q, n, tmp1))
        goto out;

    mpz_add_ui(e, n, 1);

    find_lucas_v(v, e, n, p, q, tmp1, tmp2);
    if (q < 0)
        mpz_cdiv_r(v, v, n);
    else
        mpz_fdiv_r(v, v, n);
    res = mpz_cmp_si(v, 2 * q) == 0;

out:
    mpz_clears(v, e, NULL);
    return res;
}

/*
 * This is a "strengthened" Baillie-PSW primality test based on "Strengthening
 * the Baillie-PSW primality test" paper (https://arxiv.org/abs/2006.14425).
 *
 * The primality test consists of 3 steps:
 * 1. Compute GCDs of n (the number being tested) and products of small primes.
 * Declare n to be composite if any of those GCDs is not equal to 1. For a
 * randomly generated odd n of 1024 bits in length, the test ends here in 88%
 * of cases, and it's substantially faster than running a single round of
 * Miller-Rabin test.
 * 2. Do a single round of Miller-Rabin test with base 2.
 * 3. Do the Lucas-V Probable Prime test (vprp).
 */
static int is_prime_bpsw(const mpz_t n)
{
    int i, ret = 0;
    int min_pprods = 5, n_pprods = sizeof(pprods) / sizeof(*pprods);
    mpz_t b, d;

    /* Discard even numbers. */
    if (!mpz_tstbit(n, 0) && mpz_cmp_ui(n, 2))
        return 0;

    /* Adjust the number of GCDs to compute with products of small primes
     * if bit length of n is less than 1024. As the computational cost of
     * Miller-Rabin test is cubic in bit length of n, choose the number of
     * GCD tests proportionally to the cube of n's bit length. */
    if (n->_mp_size < 16) {
        int size_cube = n->_mp_size * n->_mp_size * n->_mp_size;
        n_pprods = min_pprods + (n_pprods - min_pprods) * size_cube / (16*16*16);
    }

    for (i = 0; i < n_pprods; i++) {
        if (mpn_gcd_1(n->_mp_d, n->_mp_size, pprods[i]) != 1)
            return 0;
    }

    mpz_init2(b, n->_mp_size * sizeof(n->_mp_d[0]) * 8);
    mpz_init2(d, n->_mp_size * sizeof(n->_mp_d[0]) * 8);
    mpz_set_ui(b, 2);
    ret = miller_rabin(n, b, d) && is_vprp(n, b, d);

    mpz_clears(b, d, NULL);
    return ret ? 2 : 0;
}

#endif // PRIMETEST_H
