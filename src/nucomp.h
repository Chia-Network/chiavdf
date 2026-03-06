#ifndef NUCOMP_H
#define NUCOMP_H

/**
    Copyright (C) 2012 William Hart

    Permission is hereby granted, free of charge, to any person obtaining a copy of this
    software and associated documentation files (the "Software"), to deal in the Software
    without restriction, including without limitation the rights to use, copy, modify, merge,
    publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
    to whom the Software is furnished to do so, subject to the following conditions:
    The above copyright notice and this permission notice shall be included in all copies or
    substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
    INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
    PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
    FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
    OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

    MIT licensing permission obtained January 13, 2020 by Chia Network Inc. from William Hart

Copyright 2020 Chia Network Inc

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
**/

#include "xgcd_partial.c"

#include "chiavdf_profile.h"

#if defined(VDF_TEST)
#include <chrono>
#endif

#include <cstdlib>
#include <cstring>

#define LOG2(X) (63 - __builtin_clzll((X)))
//using namespace std;

typedef struct qfb
{
    mpz_t a;
    mpz_t b;
    mpz_t c;
} qfb;

typedef qfb qfb_t[1];

// From Antic using Flint (works!)
void qfb_nucomp(qfb_t r, const qfb_t f, const qfb_t g, mpz_t const& D, mpz_t const& L)
{
   mpz_t a1, a2, c2, ca, cb, cc, k, s, sp, ss, m, t, u2, v1, v2;

   if (mpz_cmp(f->a, g->a) > 0)
   {
      qfb_nucomp(r, g, f, D, L);
      return;
   }

   mpz_init(a1); mpz_init(a2); mpz_init(c2);
   mpz_init(ca); mpz_init(cb); mpz_init(cc);
   mpz_init(k); mpz_init(m);
   mpz_init(s); mpz_init(sp); mpz_init(ss);
   mpz_init(t); mpz_init(u2); mpz_init(v1); mpz_init(v2);

   /* nucomp calculation */

   mpz_set(a1, f->a);
   mpz_set(a2, g->a);
   mpz_set(c2, g->c);

   mpz_add(ss, f->b, g->b);
   mpz_fdiv_q_2exp(ss, ss, 1);

   mpz_sub(m, f->b, g->b);
   mpz_fdiv_q_2exp(m, m, 1);

   mpz_fdiv_r(t, a2, a1);
   if (!mpz_cmp_ui(t, 0))
   {
      mpz_set_ui(v1, 0);
      mpz_set(sp, a1);
   } else
      mpz_gcdext(sp, v1, NULL, t, a1);

   mpz_mul(k, m, v1);
   mpz_fdiv_r(k, k, a1);

   if (mpz_cmp_ui(sp, 1))
   {
      mpz_gcdext(s, v2, u2, ss, sp);

      /* k = k*u2 - v2*c2 */
      mpz_mul(k, k, u2);
      mpz_mul(t, v2, c2);
      mpz_sub(k, k, t);

      if (mpz_cmp_ui(s, 1))
      {
         mpz_divexact(a1, a1, s);
         mpz_divexact(a2, a2, s);
         mpz_mul(c2, c2, s);
      }

      mpz_fdiv_r(k, k, a1);
   }

   if (mpz_cmp(a1, L) < 0)
   {
      mpz_mul(t, a2, k);

      mpz_mul(ca, a2, a1);

      mpz_mul_2exp(cb, t, 1);
      mpz_add(cb, cb, g->b);

      mpz_add(cc, g->b, t);
      mpz_mul(cc, cc, k);
      mpz_add(cc, cc, c2);

      mpz_divexact(cc, cc, a1);
   } else
   {
      mpz_t m1, m2, r1, r2, co1, co2, temp;

      mpz_init(m1); mpz_init(m2); mpz_init(r1); mpz_init(r2);
      mpz_init(co1); mpz_init(co2); mpz_init(temp);

      mpz_set(r2, a1);
      mpz_set(r1, k);

      mpz_xgcd_partial(co2, co1, r2, r1, L);

      /* m1 = (m*co1 + a2*r1) / a1 */
      mpz_mul(t, a2, r1);
      mpz_mul(m1, m, co1);
      mpz_add(m1, m1, t);
      mpz_divexact(m1, m1, a1);

      /* m2 = (ss*r1 - c2*co1) / a1 */
      mpz_mul(m2, ss, r1);
      mpz_mul(temp, c2, co1);
      mpz_sub(m2, m2, temp);
      mpz_divexact(m2, m2, a1);

      /* ca = -sgn(co1) * (r1*m1 - co1*m2) */
      mpz_mul(ca, r1, m1);
      mpz_mul(temp, co1, m2);
      if (mpz_sgn(co1) < 0)
         mpz_sub(ca, ca, temp);
      else
         mpz_sub(ca, temp, ca);

      /* cb = (2 * (t - ca*co2) / co1 - g->b) % (2*ca) */
      mpz_mul(cb, ca, co2);
      mpz_sub(cb, t, cb);
      mpz_mul_2exp(cb, cb, 1);
      mpz_divexact(cb, cb, co1);
      mpz_sub(cb, cb, g->b);
      mpz_mul_2exp(temp, ca, 1);
      mpz_fdiv_r(cb, cb, temp);

      /* cc = (cb*cb - D) / (4*ca) */
      mpz_mul(cc, cb, cb);
      mpz_sub(cc, cc, D);
      mpz_divexact(cc, cc, ca);
      mpz_fdiv_q_2exp(cc, cc, 2);

      if (mpz_sgn(ca) < 0)
      {
         mpz_neg(ca, ca);
         mpz_neg(cc, cc);
      }

      mpz_clear(m1); mpz_clear(m2); mpz_clear(r1); mpz_clear(r2);
      mpz_clear(co1); mpz_clear(co2); mpz_clear(temp);
   }

   mpz_set(r->a, ca);
   mpz_set(r->b, cb);
   mpz_set(r->c, cc);

   mpz_clear(ca); mpz_clear(cb); mpz_clear(cc);
   mpz_clear(k); mpz_clear(m);
   mpz_clear(s); mpz_clear(sp); mpz_clear(ss);
   mpz_clear(t); mpz_clear(u2); mpz_clear(v1); mpz_clear(v2);
   mpz_clear(a1); mpz_clear(a2); mpz_clear(c2);
}

// a = b * c
void nucomp_form(form &a, form const& b, form const& c, integer const& D, integer const& L) {
    qfb fr, fr2, fr3;

    mpz_init_set(fr.a,a.a.impl);
    mpz_init_set(fr.b,a.b.impl);
    mpz_init_set(fr.c,a.c.impl);
    
    mpz_init_set(fr2.a,b.a.impl);
    mpz_init_set(fr2.b,b.b.impl);
    mpz_init_set(fr2.c,b.c.impl);

    mpz_init_set(fr3.a,c.a.impl);
    mpz_init_set(fr3.b,c.b.impl);
    mpz_init_set(fr3.c,c.c.impl);

    qfb_nucomp(&fr, &fr2, &fr3, D.impl, L.impl);

    mpz_set(a.a.impl,fr.a);
    mpz_set(a.b.impl,fr.b);
    mpz_set(a.c.impl,fr.c);

    mpz_clear(fr.a);
    mpz_clear(fr.b);
    mpz_clear(fr.c);
    mpz_clear(fr2.a);
    mpz_clear(fr2.b);
    mpz_clear(fr2.c);
    mpz_clear(fr3.a);
    mpz_clear(fr3.b);
    mpz_clear(fr3.c);
}

void qfb_nudupl(qfb_t r, qfb_t f, mpz_t const& D, mpz_t const& L)
{
#if defined(VDF_TEST)
    chiavdf_nudupl_profile_stats* prof = chiavdf_nudupl_profile_sink;
    const bool timing = (prof != nullptr) && chiavdf_nudupl_profile_timing_enabled;
    if (prof != nullptr) {
        ++prof->qfb_nudupl_calls;
    }
#else
#endif
    // Performance note:
    // This function is on the hot path for ARM `square_vdf` (NUDUPL). Avoid per-iteration
    // `mpz_init/mpz_clear` churn by reusing a thread-local scratch context.
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
    static thread_local qfb_nudupl_ctx ctx;

    mpz_t& a1 = ctx.a1;
    mpz_t& c1 = ctx.c1;
    mpz_t& cb = ctx.cb;
    mpz_t& k  = ctx.k;
    mpz_t& s  = ctx.s;
    mpz_t& t  = ctx.t;
    mpz_t& v2 = ctx.v2;
    mpz_t& b_abs = ctx.b_abs;

    /* nucomp calculation */

    /* a1 = a */
    mpz_set(a1, f->a);
    /* c1 = c */
    mpz_set(c1, f->c);

    const int b_sgn = mpz_sgn(f->b);

    if (b_sgn < 0) {
#if defined(VDF_TEST)
        if (prof != nullptr) ++prof->b_negative;
        std::chrono::steady_clock::time_point t_g0;
        if (timing) t_g0 = std::chrono::steady_clock::now();
#endif
        // Use |b| for gcdext/invert and apply sign afterwards; avoids mutating f->b in-place.
        mpz_neg(b_abs, f->b);
        /* s = gcd(|b|, a); v2 = coefficient for |b| (mod a) */
        mpz_gcdext(s, v2, NULL, b_abs, a1);
        mpz_neg(v2, v2); // convert coefficient for |b| into coefficient for b (negative)
#if defined(VDF_TEST)
        if (timing) {
            const auto t_g1 = std::chrono::steady_clock::now();
            prof->gcdext_time_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t_g1 - t_g0).count());
        }
#endif
    } else {
#if defined(VDF_TEST)
        std::chrono::steady_clock::time_point t_g0;
        if (timing) t_g0 = std::chrono::steady_clock::now();
#endif
        mpz_set(b_abs, f->b);
        mpz_gcdext(s, v2, NULL, b_abs, a1);
#if defined(VDF_TEST)
        if (timing) {
            const auto t_g1 = std::chrono::steady_clock::now();
            prof->gcdext_time_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t_g1 - t_g0).count());
        }
#endif
    }

    mpz_mul(k, v2, c1);
    mpz_neg(k, k);

    const bool s_is_1 = (mpz_cmp_ui(s, 1) == 0);
#if defined(VDF_TEST)
    if (prof != nullptr) {
        if (s_is_1) ++prof->gcdext_s_eq_1;
        else ++prof->gcdext_s_ne_1;
    }
#endif

    if (!s_is_1) {
        mpz_fdiv_q(a1, a1, s);
        mpz_mul(c1, c1, s);
    }

    /* k = -(c*inv(b)) (mod a) */
    // `mpz_fdiv_r` implements a floor-remainder; for positive modulus `a1`, we can
    // compute the (typically faster) trunc-remainder and fix up negative results.
    mpz_tdiv_r(k, k, a1);
    if (mpz_sgn(k) < 0) mpz_add(k, k, a1);

    if (mpz_cmp(a1, L) < 0) {
#if defined(VDF_TEST)
        if (prof != nullptr) ++prof->branch_a_lt_L;
#endif
        mpz_mul(t, a1, k);

        mpz_mul(r->a, a1, a1);

        mpz_mul_2exp(cb, t, 1);
        mpz_add(cb, cb, f->b);

        mpz_add(r->c, f->b, t);
        mpz_mul(r->c, r->c, k);
        mpz_add(r->c, r->c, c1);

        mpz_fdiv_q(r->c, r->c, a1);
    } else {
#if defined(VDF_TEST)
        if (prof != nullptr) ++prof->branch_a_ge_L;
        std::chrono::steady_clock::time_point t_else0;
        if (timing) t_else0 = std::chrono::steady_clock::now();
#endif
        mpz_t& m2 = ctx.m2;
        mpz_t& r1 = ctx.r1;
        mpz_t& r2 = ctx.r2;
        mpz_t& co1 = ctx.co1;
        mpz_t& co2 = ctx.co2;
        mpz_t& temp = ctx.temp;

        mpz_set(r2, a1);
        /* r1 = k */
        mpz_swap(r1, k);

        /* Satisfies co2*r1 - co1*r2 == +/- r2_orig */
#if defined(VDF_TEST)
        std::chrono::steady_clock::time_point t_x0;
        if (timing) t_x0 = std::chrono::steady_clock::now();
#endif
        mpz_xgcd_partial(co2, co1, r2, r1, L);
#if defined(VDF_TEST)
        if (timing) {
            const auto t_x1 = std::chrono::steady_clock::now();
            prof->xgcd_partial_time_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t_x1 - t_x0).count());
        }
#endif

        /* m2 = b * r1 */
        mpz_mul(m2, f->b, r1);
        mpz_submul(m2, c1, co1);
        /* m2 = (b*r1 - c1*co1) / a1 */
        mpz_divexact(m2, m2, a1);

        /* new_a = r1^2 */
        mpz_mul(r->a, r1, r1);
        /* new_a = new_a - co1 * m2 */
        mpz_submul(r->a, co1, m2);
        if (mpz_sgn(co1) >= 0)
            mpz_neg(r->a, r->a);

        mpz_mul(cb, r->a, co2);
        mpz_submul(cb, a1, r1);
        /* cb = a1*r1 - new_a*co2 */
        mpz_neg(cb, cb);
        /* cb = 2 * (a1*r1 - new_a*co2) */
        mpz_mul_2exp(cb, cb, 1);
        mpz_divexact(cb, cb, co1);
        mpz_sub(cb, cb, f->b);
        mpz_mul_2exp(temp, r->a, 1);
        mpz_fdiv_r(cb, cb, temp);

        mpz_mul(r->c, cb, cb);
        mpz_sub(r->c, r->c, D);
        mpz_divexact(r->c, r->c, r->a);
        mpz_tdiv_q_2exp(r->c, r->c, 2);

        if (mpz_sgn(r->a) < 0) {
            mpz_neg(r->a, r->a);
            mpz_neg(r->c, r->c);
        }
#if defined(VDF_TEST)
        if (timing) {
            const auto t_else1 = std::chrono::steady_clock::now();
            prof->else_branch_time_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t_else1 - t_else0).count());
        }
#endif
    }

    mpz_set(r->b, cb);
}

// a = b * b
void nudupl_form(form &a, form &b, const integer &D, const integer &L)
{
    qfb fr, fr2;

    mpz_init_set(fr.a,a.a.impl);
    mpz_init_set(fr.b,a.b.impl);
    mpz_init_set(fr.c,a.c.impl);

    mpz_init_set(fr2.a,b.a.impl);
    mpz_init_set(fr2.b,b.b.impl);
    mpz_init_set(fr2.c,b.c.impl);

    qfb_nudupl(&fr, &fr2, D.impl, L.impl);

    mpz_set(a.a.impl,fr.a);
    mpz_set(a.b.impl,fr.b);
    mpz_set(a.c.impl,fr.c);

    mpz_clear(fr.a);
    mpz_clear(fr.b);
    mpz_clear(fr.c);
    mpz_clear(fr2.a);
    mpz_clear(fr2.b);
    mpz_clear(fr2.c);
}

#endif // NUCOMP_H
