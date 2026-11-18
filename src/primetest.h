#ifndef PRIMETEST_H
#define PRIMETEST_H

#include "checked_cast.h"
#include "pprods.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

struct BpswProfile {
    bool enabled = std::getenv("CHIAVDF_BPSW_PROFILE") != nullptr;
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> small_prime_gcds{0};
    std::atomic<uint64_t> small_prime_rejections{0};
    std::atomic<uint64_t> small_prime_nanoseconds{0};
    std::atomic<uint64_t> miller_rabin_calls{0};
    std::atomic<uint64_t> miller_rabin_passes{0};
    std::atomic<uint64_t> miller_rabin_nanoseconds{0};
    std::atomic<uint64_t> lucas_v_calls{0};
    std::atomic<uint64_t> lucas_v_passes{0};
    std::atomic<uint64_t> lucas_v_nanoseconds{0};

    ~BpswProfile() {
        if (enabled) {
            std::fprintf(stderr,
                         "CHIAVDF_BPSW_PROFILE "
                         "{\"calls\":%llu,\"small_prime_gcds\":%llu,"
                         "\"small_prime_rejections\":%llu,\"small_prime_ns\":%llu,"
                         "\"miller_rabin_calls\":%llu,\"miller_rabin_passes\":%llu,"
                         "\"miller_rabin_ns\":%llu,\"lucas_v_calls\":%llu,"
                         "\"lucas_v_passes\":%llu,\"lucas_v_ns\":%llu}\n",
                         static_cast<unsigned long long>(calls.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(small_prime_gcds.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(small_prime_rejections.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(small_prime_nanoseconds.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(miller_rabin_calls.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(miller_rabin_passes.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(miller_rabin_nanoseconds.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(lucas_v_calls.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(lucas_v_passes.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(lucas_v_nanoseconds.load(std::memory_order_relaxed)));
        }
    }
};

inline BpswProfile& GetBpswProfile() {
    static BpswProfile profile;
    return profile;
}

inline uint64_t BpswElapsedNanoseconds(std::chrono::steady_clock::time_point start) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count());
}

static int miller_rabin(const mpz_t n, mpz_t b, mpz_t d)
{
    int r = 0;
    mp_bitcnt_t s_bits = mpz_scan1(n, 1);
    int s = checked_cast<int>(s_bits);

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
    int i;
    mp_bitcnt_t l_bits = mpz_sizeinbase(e, 2);
    int l = checked_cast<int>(l_bits);
    int minus_2q = -2 * q;

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
    BpswProfile& profile = GetBpswProfile();
    const bool profiling = profile.enabled;

    if (profiling)
        profile.calls.fetch_add(1, std::memory_order_relaxed);

    /* Zero, one and negative numbers are not prime. */
    if (mpz_cmp_ui(n, 2) < 0)
        return 0;

    /* Answer directly below 8. Primes are 2, 3, 5 and 7 (bitmask 0xac). */
    if (mpz_cmp_ui(n, 8) < 0)
        return ((0xac >> mpz_get_ui(n)) & 1) ? 2 : 0;

    /* Discard even numbers. */
    if (!mpz_tstbit(n, 0))
        return 0;

    /* Adjust the number of GCDs to compute with products of small primes
     * if bit length of n is less than 1024. As the computational cost of
     * Miller-Rabin test is cubic in bit length of n, choose the number of
     * GCD tests proportionally to the cube of n's bit length. */
    if (n->_mp_size < 16) {
        int size_cube = n->_mp_size * n->_mp_size * n->_mp_size;
        n_pprods = min_pprods + (n_pprods - min_pprods) * size_cube / (16*16*16);
    }

    /* Numbers up to the largest prime in the products may be one of those
     * primes, so leave them to the exact Miller-Rabin and Lucas tests. */
    const auto small_prime_start = profiling ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};
    uint64_t gcd_count = 0;
    if (mpz_cmp_ui(n, pprods_max_prime) > 0) {
        for (i = 0; i < n_pprods; i++) {
            if (profiling)
                ++gcd_count;
            if (mpn_gcd_1(n->_mp_d, n->_mp_size, pprods[i]) != 1) {
                if (profiling) {
                    profile.small_prime_gcds.fetch_add(gcd_count, std::memory_order_relaxed);
                    profile.small_prime_rejections.fetch_add(1, std::memory_order_relaxed);
                    profile.small_prime_nanoseconds.fetch_add(
                        BpswElapsedNanoseconds(small_prime_start), std::memory_order_relaxed);
                }
                return 0;
            }
        }
    }
    if (profiling) {
        profile.small_prime_gcds.fetch_add(gcd_count, std::memory_order_relaxed);
        profile.small_prime_nanoseconds.fetch_add(
            BpswElapsedNanoseconds(small_prime_start), std::memory_order_relaxed);
    }

    mpz_init2(b, n->_mp_size * sizeof(n->_mp_d[0]) * 8);
    mpz_init2(d, n->_mp_size * sizeof(n->_mp_d[0]) * 8);
    mpz_set_ui(b, 2);
    const auto miller_rabin_start = profiling ? std::chrono::steady_clock::now()
                                              : std::chrono::steady_clock::time_point{};
    const int passes_miller_rabin = miller_rabin(n, b, d);
    if (profiling) {
        profile.miller_rabin_calls.fetch_add(1, std::memory_order_relaxed);
        profile.miller_rabin_passes.fetch_add(passes_miller_rabin != 0, std::memory_order_relaxed);
        profile.miller_rabin_nanoseconds.fetch_add(
            BpswElapsedNanoseconds(miller_rabin_start), std::memory_order_relaxed);
    }

    int passes_lucas_v = 0;
    if (passes_miller_rabin) {
        const auto lucas_v_start = profiling ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};
        passes_lucas_v = is_vprp(n, b, d);
        if (profiling) {
            profile.lucas_v_calls.fetch_add(1, std::memory_order_relaxed);
            profile.lucas_v_passes.fetch_add(passes_lucas_v != 0, std::memory_order_relaxed);
            profile.lucas_v_nanoseconds.fetch_add(
                BpswElapsedNanoseconds(lucas_v_start), std::memory_order_relaxed);
        }
    }
    ret = passes_miller_rabin && passes_lucas_v;

    mpz_clears(b, d, NULL);
    return ret ? 2 : 0;
}

#endif // PRIMETEST_H
