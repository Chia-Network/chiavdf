/*
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
    */

#ifndef _XGCD_PARTIAL
#define _XGCD_PARTIAL

#include <gmp.h>

// Fast helpers (avoid mpz temporaries in tight loops).
static inline mp_limb_signed_t chiavdf_mpz_bitlen_nonneg(const mpz_t x)
{
   // Match mpz_sizeinbase(x, 2) for x >= 0:
   // - returns 1 for x == 0
   // - otherwise returns exact bit length
   const size_t n = mpz_size(x); // number of limbs (abs)
   if (n == 0) return 1;
   const mp_limb_t top = mpz_getlimbn(x, (mp_size_t)(n - 1));
   // top is non-zero when n != 0, but be defensive.
   if (top == 0) return 1;
#if GMP_LIMB_BITS == 64
   const int lead = __builtin_clzll((unsigned long long)top);
#elif GMP_LIMB_BITS == 32
   const int lead = __builtin_clz((unsigned int)top);
#else
   // Fallback (unlikely): conservative loop.
   int lead = 0;
   for (int b = GMP_LIMB_BITS - 1; b >= 0; --b) {
      if ((top >> b) & 1) break;
      ++lead;
   }
#endif
   const mp_limb_signed_t top_bits = (mp_limb_signed_t)(GMP_LIMB_BITS - lead);
   return (mp_limb_signed_t)((n - 1) * (size_t)GMP_LIMB_BITS) + top_bits;
}

static inline mp_limb_signed_t chiavdf_mpz_extract_uword_from_shift_nonneg(const mpz_t x, mp_limb_signed_t shift_bits)
{
   // Return the low word of (x >> shift_bits), assuming x >= 0 and shift_bits >= 0.
   // This is what `mpz_get_ui(tmp)` would yield after `mpz_tdiv_q_2exp(tmp, x, shift_bits)`,
   // but without allocating or touching an mpz temp.
   if (shift_bits <= 0) {
      // limb 0 is enough for our use here.
      return (mp_limb_signed_t)mpz_getlimbn(x, 0);
   }
   const mp_limb_signed_t limb_bits = (mp_limb_signed_t)GMP_LIMB_BITS;
   const mp_limb_signed_t limb_idx = shift_bits / limb_bits;
   const mp_limb_signed_t off = shift_bits - limb_idx * limb_bits;
   mp_limb_t lo = mpz_getlimbn(x, (mp_size_t)limb_idx);
   if (off == 0) return (mp_limb_signed_t)lo;
   mp_limb_t hi = mpz_getlimbn(x, (mp_size_t)(limb_idx + 1));
   lo >>= (unsigned)off;
   hi <<= (unsigned)(limb_bits - off);
   return (mp_limb_signed_t)(lo | hi);
}

void mpz_xgcd_partial(mpz_t co2, mpz_t co1,
                                    mpz_t r2, mpz_t r1, const mpz_t L)
{
   // Hot-path note:
   // This function can run in the inner loop of NUDUPL; avoid per-call
   // `mpz_init/mpz_clear` by using thread-local temporaries.
   static thread_local int _chiavdf_xgcd_partial_inited = 0;
   static thread_local mpz_t q;
   static thread_local mpz_t r;
   mp_limb_signed_t aa2, aa1, bb2, bb1, rr1, rr2, qq, bb, t1, t2, t3, i;
   mp_limb_signed_t bits, bits1, bits2;

   if (!_chiavdf_xgcd_partial_inited) {
      mpz_init(q);
      mpz_init(r);
      _chiavdf_xgcd_partial_inited = 1;
   }

   mpz_set_ui(co2, 0);
   mpz_set_si(co1, -1);

   while (mpz_cmp_ui(r1, 0) && mpz_cmp(r1, L) > 0)
   {
      // r2/r1 are expected to be nonnegative here (algorithm maintains sign after each step).
      bits2 = chiavdf_mpz_bitlen_nonneg(r2);
      bits1 = chiavdf_mpz_bitlen_nonneg(r1);
      bits = __GMP_MAX(bits2, bits1) - GMP_LIMB_BITS + 1;
      if (bits < 0) bits = 0;

      rr2 = chiavdf_mpz_extract_uword_from_shift_nonneg(r2, bits);
      rr1 = chiavdf_mpz_extract_uword_from_shift_nonneg(r1, bits);
      bb  = chiavdf_mpz_extract_uword_from_shift_nonneg(L,  bits);

      aa2 = 0; aa1 = 1;
      bb2 = 1; bb1 = 0;

      for (i = 0; rr1 != 0 && rr1 > bb; i++)
      {
         qq = rr2 / rr1;

         t1 = rr2 - qq*rr1;
         t2 = aa2 - qq*aa1;
         t3 = bb2 - qq*bb1;

         if (i & 1)
         {
            if (t1 < -t3 || rr1 - t1 < t2 - aa1) break;
         } else
         {
            if (t1 < -t2 || rr1 - t1 < t3 - bb1) break;
         }

         rr2 = rr1; rr1 = t1;
         aa2 = aa1; aa1 = t2;
         bb2 = bb1; bb1 = t3;
      }

      if (i == 0)
      {
         // r2,r1 are nonnegative here; trunc and floor division are equivalent, and
         // `mpz_tdiv_qr` avoids extra sign-handling overhead.
         mpz_tdiv_qr(q, r2, r2, r1);
         mpz_swap(r2, r1);

         mpz_submul(co2, co1, q);
         mpz_swap(co2, co1);
      } else
      {
         mpz_mul_si(r, r2, bb2);
         if (aa2 >= 0)
            mpz_addmul_ui(r, r1, aa2);
         else
            mpz_submul_ui(r, r1, -aa2);
         mpz_mul_si(r1, r1, aa1);
         if (bb1 >= 0)
            mpz_addmul_ui(r1, r2, bb1);
         else
            mpz_submul_ui(r1, r2, -bb1);
         mpz_set(r2, r);

         mpz_mul_si(r, co2, bb2);
         if (aa2 >= 0)
            mpz_addmul_ui(r, co1, aa2);
         else
            mpz_submul_ui(r, co1, -aa2);
         mpz_mul_si(co1, co1, aa1);
         if (bb1 >= 0)
            mpz_addmul_ui(co1, co2, bb1);
         else
            mpz_submul_ui(co1, co2, -bb1);
         mpz_set(co2, r);

         if (mpz_sgn(r1) < 0) { mpz_neg(co1, co1); mpz_neg(r1, r1); }
         if (mpz_sgn(r2) < 0) { mpz_neg(co2, co2); mpz_neg(r2, r2); }
      }
   }

   if (mpz_sgn(r2) < 0)
   {
      mpz_neg(co2, co2); mpz_neg(co1, co1);
      mpz_neg(r2, r2);
   }

   // q/r are thread-local; no clears here.
}
#endif /* _XGCD_PARTIAL */
