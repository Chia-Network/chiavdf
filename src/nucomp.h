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

#include "nucomp_phases.h"

// From Antic using Flint (works!)
// MSVC link-time optimization miscompiles this function when it is inlined,
// and the Windows package then divides by zero. Moving this scratch into
// another file does not stop that inlining, and the inlined build still
// divides by zero. The fence below is what makes the Windows package pass.
// Other compilers are unchanged.
#if defined(_MSC_VER)
__declspec(noinline)
#endif
void qfb_nucomp(qfb_t r, const qfb_t f, const qfb_t g, mpz_t const& D, mpz_t const& L)
{
   // Order the inputs once: the smaller a first.
   const qfb* left = f;
   const qfb* right = g;
   if (mpz_cmp(f->a, g->a) > 0)
   {
      left = g;
      right = f;
   }

   // Same approach as qfb_nudupl: keep GMP temporaries for the life of the thread.
   static thread_local qfb_nucomp_ctx ctx;

   /* nucomp calculation (phases in nucomp_phases.h) */
   if (qfb_nucomp_prepare(ctx, left, right))
      mpz_gcdext(ctx.sp, ctx.v1, NULL, ctx.t, ctx.a1);
   const bool partial = qfb_nucomp_reduce_k(ctx, L);
   if (partial)
      mpz_xgcd_partial(ctx.co2, ctx.co1, ctx.r2, ctx.r1, L);
   qfb_nucomp_finish(ctx, r, right, D, partial);
}

// a = b * c
void nucomp_form(form &a, form const& b, form const& c, integer const& D, integer const& L) {
    qfb fr, fr2, fr3;

    // Copies the mpz header only. fr, fr2, and fr3 share limb storage with the
    // source form. Do not clear, free, or reallocate them.
    *fr.a = *a.a.impl;
    *fr.b = *a.b.impl;
    *fr.c = *a.c.impl;
    *fr2.a = *b.a.impl;
    *fr2.b = *b.b.impl;
    *fr2.c = *b.c.impl;
    *fr3.a = *c.a.impl;
    *fr3.b = *c.b.impl;
    *fr3.c = *c.c.impl;

    qfb_nucomp(&fr, &fr2, &fr3, D.impl, L.impl);

    *a.a.impl = *fr.a;
    *a.b.impl = *fr.b;
    *a.c.impl = *fr.c;
}

void qfb_nudupl(qfb_t r, qfb_t f, mpz_t const& D, mpz_t const& L)
{
#if defined(VDF_TEST)
    chiavdf_nudupl_profile_stats* prof = chiavdf_nudupl_profile_sink;
    const bool timing = (prof != nullptr) && chiavdf_nudupl_profile_timing_enabled;
    if (prof != nullptr) {
        ++prof->qfb_nudupl_calls;
        if (mpz_sgn(f->b) < 0) ++prof->b_negative;
    }
    std::chrono::steady_clock::time_point t0;
#endif
    // Performance note:
    // This function is on the hot path for ARM `square_vdf` (NUDUPL). Avoid per-iteration
    // `mpz_init/mpz_clear` churn by reusing a thread-local scratch context.
    static thread_local qfb_nudupl_ctx ctx;

    /* nucomp calculation (phases in nucomp_phases.h) */
    qfb_nudupl_prepare(ctx, f);
#if defined(VDF_TEST)
    if (timing) t0 = std::chrono::steady_clock::now();
#endif
    /* s = gcd(|b|, a); v2 = coefficient for |b| (mod a) */
    mpz_gcdext(ctx.s, ctx.v2, NULL, ctx.b_abs, ctx.a1);
#if defined(VDF_TEST)
    if (timing)
        prof->gcdext_time_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - t0).count());
    if (prof != nullptr) {
        if (mpz_cmp_ui(ctx.s, 1) == 0) ++prof->gcdext_s_eq_1;
        else ++prof->gcdext_s_ne_1;
    }
#endif

    const bool partial = qfb_nudupl_reduce_k(ctx, f, L);
#if defined(VDF_TEST)
    if (prof != nullptr) {
        if (partial) ++prof->branch_a_ge_L;
        else ++prof->branch_a_lt_L;
    }
    std::chrono::steady_clock::time_point t_else0;
    if (timing && partial) t_else0 = std::chrono::steady_clock::now();
#endif
    if (partial) {
        /* Satisfies co2*r1 - co1*r2 == +/- r2_orig */
#if defined(VDF_TEST)
        if (timing) t0 = std::chrono::steady_clock::now();
#endif
        mpz_xgcd_partial(ctx.co2, ctx.co1, ctx.r2, ctx.r1, L);
#if defined(VDF_TEST)
        if (timing)
            prof->xgcd_partial_time_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0).count());
#endif
    }
    qfb_nudupl_finish(ctx, r, f, D, partial);
#if defined(VDF_TEST)
    if (timing && partial)
        prof->else_branch_time_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - t_else0).count());
#endif
}

// a = b * b
void nudupl_form(form &a, form &b, const integer &D, const integer &L)
{
    qfb fr, fr2;

    // Copies the mpz header only. fr and fr2 share limb storage with the
    // source form. Do not clear, free, or reallocate them.
    *fr.a = *a.a.impl;
    *fr.b = *a.b.impl;
    *fr.c = *a.c.impl;
    *fr2.a = *b.a.impl;
    *fr2.b = *b.b.impl;
    *fr2.c = *b.c.impl;

    qfb_nudupl(&fr, &fr2, D.impl, L.impl);

    *a.a.impl = *fr.a;
    *a.b.impl = *fr.b;
    *a.c.impl = *fr.c;
}

#endif // NUCOMP_H
