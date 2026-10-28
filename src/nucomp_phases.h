#ifndef NUCOMP_PHASES_H
#define NUCOMP_PHASES_H

/*
    NUDUPL and NUCOMP split around their GCD calls, so that qfb_nudupl and
    qfb_nucomp (nucomp.h) and their paired forms (pair_kernels.cpp) share
    every other statement:

        prepare -> gcdext -> reduce_k -> [partial GCD] -> finish

    Each phase is the reference code, unchanged and in its original order.
    Everything here is inline, so any translation unit may include it.
*/

#include <gmp.h>

typedef struct qfb
{
    mpz_t a;
    mpz_t b;
    mpz_t c;
} qfb;

typedef qfb qfb_t[1];

// GMP temporaries for one NUDUPL, kept for the life of the thread.
struct qfb_nudupl_ctx {
    mpz_t a1, c1, cb, k, s, t, u2, v2;
    mpz_t b_abs;
    mpz_t m2, r1, r2, co1, co2, temp;  // only used in the "a1 >= L" branch

    qfb_nudupl_ctx() {
        mpz_inits(a1, c1, cb, k, s, t, u2, v2, b_abs, m2, r1, r2, co1, co2, temp, nullptr);
    }
    ~qfb_nudupl_ctx() {
        mpz_clears(a1, c1, cb, k, s, t, u2, v2, b_abs, m2, r1, r2, co1, co2, temp, nullptr);
    }
    qfb_nudupl_ctx(const qfb_nudupl_ctx&) = delete;
    qfb_nudupl_ctx& operator=(const qfb_nudupl_ctx&) = delete;
};

// Inputs of the GCD: s = gcd(|b|, a), v2 its coefficient for |b|.
inline void qfb_nudupl_prepare(qfb_nudupl_ctx& x, const qfb* f)
{
    /* a1 = a */
    mpz_set(x.a1, f->a);
    /* c1 = c */
    mpz_set(x.c1, f->c);
    // Use |b| for gcdext/invert and apply sign afterwards; avoids mutating f->b in-place.
    if (mpz_sgn(f->b) < 0)
        mpz_neg(x.b_abs, f->b);
    else
        mpz_set(x.b_abs, f->b);
}

// After mpz_gcdext(s, v2, NULL, b_abs, a1): k = -(c*inv(b)) (mod a1).
// Returns true for the "a1 >= L" branch, with the partial GCD's inputs in
// r2 (a1) and r1 (k).
inline bool qfb_nudupl_reduce_k(qfb_nudupl_ctx& x, const qfb* f, mpz_srcptr L)
{
    if (mpz_sgn(f->b) < 0)
        mpz_neg(x.v2, x.v2); // convert coefficient for |b| into coefficient for b (negative)

    mpz_mul(x.k, x.v2, x.c1);
    mpz_neg(x.k, x.k);

    if (mpz_cmp_ui(x.s, 1) != 0) {
        mpz_fdiv_q(x.a1, x.a1, x.s);
        mpz_mul(x.c1, x.c1, x.s);
    }

    // `mpz_fdiv_r` implements a floor-remainder; for positive modulus `a1`, we can
    // compute the (typically faster) trunc-remainder and fix up negative results.
    mpz_tdiv_r(x.k, x.k, x.a1);
    if (mpz_sgn(x.k) < 0) mpz_add(x.k, x.k, x.a1);

    if (mpz_cmp(x.a1, L) < 0)
        return false;
    mpz_set(x.r2, x.a1);
    /* r1 = k */
    mpz_swap(x.r1, x.k);
    return true;
}

// r = f^2 from k, or after the partial GCD (co2*r1 - co1*r2 == +/- r2_orig).
// r may alias f.
inline void qfb_nudupl_finish(qfb_nudupl_ctx& x, qfb* r, const qfb* f, mpz_srcptr D, bool partial)
{
    if (!partial) {
        mpz_mul(x.t, x.a1, x.k);

        mpz_mul(r->a, x.a1, x.a1);

        mpz_mul_2exp(x.cb, x.t, 1);
        mpz_add(x.cb, x.cb, f->b);

        mpz_add(r->c, f->b, x.t);
        mpz_mul(r->c, r->c, x.k);
        mpz_add(r->c, r->c, x.c1);

        mpz_fdiv_q(r->c, r->c, x.a1);
    } else {
        /* m2 = b * r1 */
        mpz_mul(x.m2, f->b, x.r1);
        mpz_submul(x.m2, x.c1, x.co1);
        /* m2 = (b*r1 - c1*co1) / a1 */
        mpz_divexact(x.m2, x.m2, x.a1);

        /* new_a = r1^2 */
        mpz_mul(r->a, x.r1, x.r1);
        /* new_a = new_a - co1 * m2 */
        mpz_submul(r->a, x.co1, x.m2);
        if (mpz_sgn(x.co1) >= 0)
            mpz_neg(r->a, r->a);

        mpz_mul(x.cb, r->a, x.co2);
        mpz_submul(x.cb, x.a1, x.r1);
        /* cb = a1*r1 - new_a*co2 */
        mpz_neg(x.cb, x.cb);
        /* cb = 2 * (a1*r1 - new_a*co2) */
        mpz_mul_2exp(x.cb, x.cb, 1);
        mpz_divexact(x.cb, x.cb, x.co1);
        mpz_sub(x.cb, x.cb, f->b);
        mpz_mul_2exp(x.temp, r->a, 1);
        mpz_fdiv_r(x.cb, x.cb, x.temp);

        mpz_mul(r->c, x.cb, x.cb);
        mpz_sub(r->c, r->c, D);
        mpz_divexact(r->c, r->c, r->a);
        mpz_tdiv_q_2exp(r->c, r->c, 2);

        if (mpz_sgn(r->a) < 0) {
            mpz_neg(r->a, r->a);
            mpz_neg(r->c, r->c);
        }
    }

    mpz_set(r->b, x.cb);
}

// GMP temporaries for one NUCOMP, kept for the life of the thread.
struct qfb_nucomp_ctx {
    mpz_t a1, a2, c2, ca, cb, cc, k, s, sp, ss, m, t, u2, v1, v2;
    mpz_t m1, m2, r1, r2, co1, co2, temp;

    qfb_nucomp_ctx() {
        mpz_inits(a1, a2, c2, ca, cb, cc, k, s, sp, ss, m, t, u2, v1, v2,
            m1, m2, r1, r2, co1, co2, temp, nullptr);
    }
    ~qfb_nucomp_ctx() {
        mpz_clears(a1, a2, c2, ca, cb, cc, k, s, sp, ss, m, t, u2, v1, v2,
            m1, m2, r1, r2, co1, co2, temp, nullptr);
    }
    qfb_nucomp_ctx(const qfb_nucomp_ctx&) = delete;
    qfb_nucomp_ctx& operator=(const qfb_nucomp_ctx&) = delete;
};

// left is the operand with the smaller a. Returns true when
// mpz_gcdext(sp, v1, NULL, t, a1) is needed (t = a2 mod a1 is nonzero).
inline bool qfb_nucomp_prepare(qfb_nucomp_ctx& x, const qfb* left, const qfb* right)
{
    mpz_set(x.a1, left->a);
    mpz_set(x.a2, right->a);
    mpz_set(x.c2, right->c);

    mpz_add(x.ss, left->b, right->b);
    mpz_fdiv_q_2exp(x.ss, x.ss, 1);

    mpz_sub(x.m, left->b, right->b);
    mpz_fdiv_q_2exp(x.m, x.m, 1);

    mpz_fdiv_r(x.t, x.a2, x.a1);
    if (!mpz_cmp_ui(x.t, 0))
    {
        mpz_set_ui(x.v1, 0);
        mpz_set(x.sp, x.a1);
        return false;
    }
    return true;
}

// After the first GCD. Returns true for the "a1 >= L" branch, with the
// partial GCD's inputs in r2 (a1) and r1 (k).
inline bool qfb_nucomp_reduce_k(qfb_nucomp_ctx& x, mpz_srcptr L)
{
    mpz_mul(x.k, x.m, x.v1);
    mpz_fdiv_r(x.k, x.k, x.a1);

    if (mpz_cmp_ui(x.sp, 1))
    {
        mpz_gcdext(x.s, x.v2, x.u2, x.ss, x.sp);

        /* k = k*u2 - v2*c2 */
        mpz_mul(x.k, x.k, x.u2);
        mpz_mul(x.t, x.v2, x.c2);
        mpz_sub(x.k, x.k, x.t);

        if (mpz_cmp_ui(x.s, 1))
        {
            mpz_divexact(x.a1, x.a1, x.s);
            mpz_divexact(x.a2, x.a2, x.s);
            mpz_mul(x.c2, x.c2, x.s);
        }

        mpz_fdiv_r(x.k, x.k, x.a1);
    }

    if (mpz_cmp(x.a1, L) < 0)
        return false;
    mpz_set(x.r2, x.a1);
    mpz_set(x.r1, x.k);
    return true;
}

// r = left * right from k, or after the partial GCD. r may alias either input.
inline void qfb_nucomp_finish(qfb_nucomp_ctx& x, qfb* r, const qfb* right, mpz_srcptr D, bool partial)
{
    if (!partial)
    {
        mpz_mul(x.t, x.a2, x.k);

        mpz_mul(x.ca, x.a2, x.a1);

        mpz_mul_2exp(x.cb, x.t, 1);
        mpz_add(x.cb, x.cb, right->b);

        mpz_add(x.cc, right->b, x.t);
        mpz_mul(x.cc, x.cc, x.k);
        mpz_add(x.cc, x.cc, x.c2);

        mpz_divexact(x.cc, x.cc, x.a1);
    } else
    {
        /* m1 = (m*co1 + a2*r1) / a1 */
        mpz_mul(x.t, x.a2, x.r1);
        mpz_mul(x.m1, x.m, x.co1);
        mpz_add(x.m1, x.m1, x.t);
        mpz_divexact(x.m1, x.m1, x.a1);

        /* m2 = (ss*r1 - c2*co1) / a1 */
        mpz_mul(x.m2, x.ss, x.r1);
        mpz_mul(x.temp, x.c2, x.co1);
        mpz_sub(x.m2, x.m2, x.temp);
        mpz_divexact(x.m2, x.m2, x.a1);

        /* ca = -sgn(co1) * (r1*m1 - co1*m2) */
        mpz_mul(x.ca, x.r1, x.m1);
        mpz_mul(x.temp, x.co1, x.m2);
        if (mpz_sgn(x.co1) < 0)
            mpz_sub(x.ca, x.ca, x.temp);
        else
            mpz_sub(x.ca, x.temp, x.ca);

        /* cb = (2 * (t - ca*co2) / co1 - right->b) % (2*ca) */
        mpz_mul(x.cb, x.ca, x.co2);
        mpz_sub(x.cb, x.t, x.cb);
        mpz_mul_2exp(x.cb, x.cb, 1);
        mpz_divexact(x.cb, x.cb, x.co1);
        mpz_sub(x.cb, x.cb, right->b);
        mpz_mul_2exp(x.temp, x.ca, 1);
        mpz_fdiv_r(x.cb, x.cb, x.temp);

        /* cc = (cb*cb - D) / (4*ca) */
        mpz_mul(x.cc, x.cb, x.cb);
        mpz_sub(x.cc, x.cc, D);
        mpz_divexact(x.cc, x.cc, x.ca);
        mpz_fdiv_q_2exp(x.cc, x.cc, 2);

        if (mpz_sgn(x.ca) < 0)
        {
            mpz_neg(x.ca, x.ca);
            mpz_neg(x.cc, x.cc);
        }
    }

    mpz_set(r->a, x.ca);
    mpz_set(r->b, x.cb);
    mpz_set(r->c, x.cc);
}

#endif // NUCOMP_PHASES_H
