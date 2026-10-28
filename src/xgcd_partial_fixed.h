/*
    Fixed-size variants of mpz_xgcd_partial (xgcd_partial.c). Remainders and
    cofactors live in small zero-padded limb arrays, and each round's 2x2
    matrix is applied with one fused pass per output instead of eight
    separate mpz calls.

    mpz_xgcd_partial_fixed makes exactly the reference's Lehmer decisions
    (single-limb approximations, same inner loop and stopping test), so its
    results are identical to mpz_xgcd_partial. The one exception is a full
    GCD (L == 0) once both values fit in a word: it then finishes with plain
    exact Euclid, which cannot change the (unique) end state but avoids
    several short rounds and mpz division steps.

    Inputs the arrays cannot hold (negative values, or more than
    XGCD_FIXED_MAX_IN limbs) use the reference.
*/

#ifndef XGCD_PARTIAL_FIXED_H
#define XGCD_PARTIAL_FIXED_H

#include <gmp.h>
#include <stdint.h>
#include <string.h>

#include "xgcd_partial.c"

#define XGCD_FIXED_MAX_IN 10
#define XGCD_FIXED_LIMBS (XGCD_FIXED_MAX_IN + 2)

typedef unsigned __int128 xgcd_u128;
typedef __int128 xgcd_s128;

// Sign-magnitude integer; n == 0 means zero and then neg == 0. Every limb at
// index n and above is zero, so loops can read past n without bounds checks.
typedef struct {
   mp_limb_t d[XGCD_FIXED_LIMBS];
   int n;
   int neg;
} xgcd_fixed_int;

static inline void xgcd_fixed_normalize(xgcd_fixed_int *x)
{
   while (x->n > 0 && x->d[x->n - 1] == 0) x->n--;
   if (x->n == 0) x->neg = 0;
}

static inline void xgcd_fixed_from_mpz(xgcd_fixed_int *x, const mpz_t z)
{
   int old = x->n;
   x->n = (int)mpz_size(z);
   x->neg = mpz_sgn(z) < 0;
   if (x->n) memcpy(x->d, mpz_limbs_read(z), (size_t)x->n * sizeof(mp_limb_t));
   for (int i = x->n; i < old; ++i) x->d[i] = 0;
}

static inline void xgcd_fixed_to_mpz(mpz_t z, const xgcd_fixed_int *x)
{
   if (x->n == 0) { mpz_set_ui(z, 0); return; }
   mp_limb_t *p = mpz_limbs_write(z, x->n);
   memcpy(p, x->d, (size_t)x->n * sizeof(mp_limb_t));
   mpz_limbs_finish(z, x->neg ? -x->n : x->n);
}

// Same contract as chiavdf_mpz_bitlen_nonneg: 1 for zero.
static inline mp_limb_signed_t xgcd_fixed_bitlen(const xgcd_fixed_int *x)
{
   if (x->n == 0) return 1;
   return (mp_limb_signed_t)(x->n - 1) * GMP_LIMB_BITS
          + (GMP_LIMB_BITS - __builtin_clzll(x->d[x->n - 1]));
}

// Same contract as chiavdf_mpz_extract_uword_from_shift_nonneg, reading a
// zero-padded array.
static inline mp_limb_signed_t xgcd_fixed_extract(const mp_limb_t *d, mp_limb_signed_t shift)
{
   if (shift <= 0) return (mp_limb_signed_t)d[0];
   mp_limb_signed_t idx = shift / GMP_LIMB_BITS;
   mp_limb_signed_t off = shift - idx * GMP_LIMB_BITS;
   mp_limb_t lo = d[idx];
   if (off == 0) return (mp_limb_signed_t)lo;
   return (mp_limb_signed_t)((lo >> off) | (d[idx + 1] << (GMP_LIMB_BITS - off)));
}

// Bits [shift, shift + 128) of a zero-padded array.
static inline xgcd_u128 xgcd_fixed_extract128(const mp_limb_t *d, mp_limb_signed_t shift)
{
   mp_limb_signed_t idx = shift / GMP_LIMB_BITS;
   unsigned off = (unsigned)(shift - idx * GMP_LIMB_BITS);
   xgcd_u128 lo = (xgcd_u128)d[idx + 1] << 64 | d[idx];
   if (off == 0) return lo;
   return (lo >> off) | ((xgcd_u128)d[idx + 2] << (128 - off));
}

static inline int xgcd_fixed_cmp_mag(const xgcd_fixed_int *a, const mp_limb_t *b, int bn)
{
   if (a->n != bn) return a->n < bn ? -1 : 1;
   for (int i = bn - 1; i >= 0; --i)
      if (a->d[i] != b[i]) return a->d[i] < b[i] ? -1 : 1;
   return 0;
}

// out = u*x + v*y, exact, with |u|, |v| < 2^63. out must not alias x or y.
static inline void xgcd_fixed_lincomb(xgcd_fixed_int *out, mp_limb_signed_t u, const xgcd_fixed_int *x,
                                      mp_limb_signed_t v, const xgcd_fixed_int *y)
{
   const mp_limb_t mu = u < 0 ? (mp_limb_t)(-u) : (mp_limb_t)u;
   const mp_limb_t mv = v < 0 ? (mp_limb_t)(-v) : (mp_limb_t)v;
   const int sx = (u < 0) ^ x->neg;
   const int sy = (v < 0) ^ y->neg;
   const int n = x->n > y->n ? x->n : y->n;
   const int old = out->n;
   mp_limb_t cp = 0, cq = 0;

   if (sx == sy) {
      mp_limb_t c = 0;
      for (int i = 0; i < n; ++i) {
         xgcd_u128 p = (xgcd_u128)mu * x->d[i] + cp;
         xgcd_u128 q = (xgcd_u128)mv * y->d[i] + cq;
         xgcd_u128 s = (xgcd_u128)(mp_limb_t)p + (mp_limb_t)q + c;
         out->d[i] = (mp_limb_t)s;
         cp = (mp_limb_t)(p >> 64);
         cq = (mp_limb_t)(q >> 64);
         c = (mp_limb_t)(s >> 64);
      }
      // cp, cq < 2^63 because mu, mv < 2^63, so this cannot overflow.
      out->d[n] = cp + cq + c;
      out->neg = sx;
   } else {
      // |u x| - |v y| in two's complement over n + 1 limbs.
      mp_limb_t borrow = 0;
      for (int i = 0; i < n; ++i) {
         xgcd_u128 p = (xgcd_u128)mu * x->d[i] + cp;
         xgcd_u128 q = (xgcd_u128)mv * y->d[i] + cq;
         mp_limb_t pl = (mp_limb_t)p, ql = (mp_limb_t)q;
         mp_limb_t d = pl - ql - borrow;
         borrow = (pl < ql) | ((pl - ql) < borrow);
         out->d[i] = d;
         cp = (mp_limb_t)(p >> 64);
         cq = (mp_limb_t)(q >> 64);
      }
      mp_limb_t top = cp - cq - borrow;
      int negative = (cp < cq) || (cp - cq < borrow);
      out->d[n] = top;
      if (negative) {
         mp_limb_t carry = 1;
         for (int i = 0; i <= n; ++i) {
            mp_limb_t t = ~out->d[i] + carry;
            carry = carry && t == 0;
            out->d[i] = t;
         }
         out->neg = sy;
      } else {
         out->neg = sx;
      }
   }
   out->n = n + 1;
   for (int i = n + 1; i < old; ++i) out->d[i] = 0;
   xgcd_fixed_normalize(out);
}

typedef struct {
   xgcd_fixed_int rb[4], cb[4];
   xgcd_fixed_int *r2, *r1, *rt, *rs;
   xgcd_fixed_int *co2, *co1, *ct, *cs;
} xgcd_fixed_state;

static inline void xgcd_fixed_init(xgcd_fixed_state *st, const mpz_t r2, const mpz_t r1)
{
   memset(st->rb, 0, sizeof(st->rb));
   memset(st->cb, 0, sizeof(st->cb));
   st->r2 = &st->rb[0]; st->r1 = &st->rb[1]; st->rt = &st->rb[2]; st->rs = &st->rb[3];
   st->co2 = &st->cb[0]; st->co1 = &st->cb[1]; st->ct = &st->cb[2]; st->cs = &st->cb[3];
   xgcd_fixed_from_mpz(st->r2, r2);
   xgcd_fixed_from_mpz(st->r1, r1);
   st->co1->n = 1; st->co1->neg = 1; st->co1->d[0] = 1;   // co2 = 0, co1 = -1
}

// Apply the round matrix: new r2 = bb2*r2 + aa2*r1, new r1 = aa1*r1 + bb1*r2
// (old values), the same on the cofactors, then the reference's sign fixes.
static inline void xgcd_fixed_apply(xgcd_fixed_state *st, mp_limb_signed_t bb2, mp_limb_signed_t aa2,
                                    mp_limb_signed_t bb1, mp_limb_signed_t aa1)
{
   xgcd_fixed_lincomb(st->rt, bb2, st->r2, aa2, st->r1);
   xgcd_fixed_lincomb(st->rs, aa1, st->r1, bb1, st->r2);
   xgcd_fixed_lincomb(st->ct, bb2, st->co2, aa2, st->co1);
   xgcd_fixed_lincomb(st->cs, aa1, st->co1, bb1, st->co2);
   xgcd_fixed_int *t;
   t = st->r2; st->r2 = st->rt; st->rt = t;
   t = st->r1; st->r1 = st->rs; st->rs = t;
   t = st->co2; st->co2 = st->ct; st->ct = t;
   t = st->co1; st->co1 = st->cs; st->cs = t;
   if (st->r1->neg) { st->co1->neg = st->co1->n ? !st->co1->neg : 0; st->r1->neg = 0; }
   if (st->r2->neg) { st->co2->neg = st->co2->n ? !st->co2->neg : 0; st->r2->neg = 0; }
}

// One exact Euclid step (the reference's i == 0 branch). Rare, so it goes
// through mpz.
static void xgcd_fixed_division_step(xgcd_fixed_state *st)
{
   mpz_t q, a, b, c2, c1;
   mpz_inits(q, a, b, c2, c1, NULL);
   xgcd_fixed_to_mpz(a, st->r2);
   xgcd_fixed_to_mpz(b, st->r1);
   xgcd_fixed_to_mpz(c2, st->co2);
   xgcd_fixed_to_mpz(c1, st->co1);
   mpz_tdiv_qr(q, a, a, b);
   mpz_submul(c2, c1, q);
   // r2 <- old r1, r1 <- remainder; co2 <- old co1, co1 <- updated co2.
   xgcd_fixed_from_mpz(st->r2, a);
   xgcd_fixed_from_mpz(st->co2, c2);
   xgcd_fixed_int *t = st->r2; st->r2 = st->r1; st->r1 = t;
   t = st->co2; st->co2 = st->co1; st->co1 = t;
   mpz_clears(q, a, b, c2, c1, NULL);
}

static inline void xgcd_fixed_finish(xgcd_fixed_state *st, mpz_t co2_out, mpz_t co1_out, mpz_t r2_io, mpz_t r1_io)
{
   if (st->r2->neg)
   {
      st->co2->neg = st->co2->n ? !st->co2->neg : 0;
      st->co1->neg = st->co1->n ? !st->co1->neg : 0;
      st->r2->neg = 0;
   }
   xgcd_fixed_to_mpz(co2_out, st->co2);
   xgcd_fixed_to_mpz(co1_out, st->co1);
   xgcd_fixed_to_mpz(r2_io, st->r2);
   xgcd_fixed_to_mpz(r1_io, st->r1);
}

static inline int xgcd_fixed_supported(const mpz_t r2, const mpz_t r1, const mpz_t L)
{
   return mpz_sgn(r2) >= 0 && mpz_sgn(r1) >= 0 && mpz_sgn(L) >= 0 &&
          mpz_size(r2) <= XGCD_FIXED_MAX_IN && mpz_size(r1) <= XGCD_FIXED_MAX_IN &&
          mpz_size(L) <= XGCD_FIXED_MAX_IN;
}

void mpz_xgcd_partial_fixed(mpz_t co2_out, mpz_t co1_out, mpz_t r2_io, mpz_t r1_io, const mpz_t L)
{
   if (!xgcd_fixed_supported(r2_io, r1_io, L)) {
      mpz_xgcd_partial(co2_out, co1_out, r2_io, r1_io, L);
      return;
   }
   xgcd_fixed_state st;
   xgcd_fixed_init(&st, r2_io, r1_io);

   mp_limb_t Ld[XGCD_FIXED_LIMBS] = {0};
   const int Ln = (int)mpz_size(L);
   if (Ln) memcpy(Ld, mpz_limbs_read(L), (size_t)Ln * sizeof(mp_limb_t));
   mp_limb_signed_t aa2, aa1, bb2, bb1, rr1, rr2, qq, bb, t1, t2, t3, i;
   mp_limb_signed_t bits, bits1, bits2;

   while (st.r1->n != 0 && xgcd_fixed_cmp_mag(st.r1, Ld, Ln) > 0)
   {
      bits2 = xgcd_fixed_bitlen(st.r2);
      bits1 = xgcd_fixed_bitlen(st.r1);
      bits = __GMP_MAX(bits2, bits1) - GMP_LIMB_BITS + 1;
      if (bits < 0) bits = 0;

      rr2 = xgcd_fixed_extract(st.r2->d, bits);
      rr1 = xgcd_fixed_extract(st.r1->d, bits);
      bb  = xgcd_fixed_extract(Ld, bits);

      aa2 = 0; aa1 = 1;
      bb2 = 1; bb1 = 0;

      // With L == 0 (a full GCD) the end state is unique, so once both
      // values fit in a word the words are exact and plain Euclid can
      // finish without the Jebelean tests (entries stay below r2 < 2^63).
      // For L > 0 keep the reference's decisions exactly.
      const int exact = Ln == 0 && bits == 0;

      for (i = 0; rr1 != 0 && rr1 > bb; i++)
      {
         qq = rr2 / rr1;

         t1 = rr2 - qq*rr1;
         t2 = aa2 - qq*aa1;
         t3 = bb2 - qq*bb1;

         if (exact)
         {
         } else if (i & 1)
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
         xgcd_fixed_division_step(&st);
      else
         xgcd_fixed_apply(&st, bb2, aa2, bb1, aa1);
   }

   xgcd_fixed_finish(&st, co2_out, co1_out, r2_io, r1_io);
}

#endif // XGCD_PARTIAL_FIXED_H
