/*
    Two independent NUDUPL squarings in one thread, with their GCD loops
    interleaved.

    A Lehmer quotient step is bound by its divide-then-multiply-subtract
    chain and leaves most of an Apple core idle. Running two unrelated
    problems through one loop lets the out-of-order core overlap the two
    chains. This only helps independent work (separate recompute jobs,
    separate bucket products), not one sequential squaring chain.

    Both GCDs use the fixed-size Lehmer machinery of xgcd_partial_fixed.h
    with the reference's exact decisions:
    - the partial GCD gives results identical to mpz_xgcd_partial;
    - the full GCD is the same routine with L = 0, which runs Euclid to the
      end. Its cofactor satisfies r_i == -co_i * b (mod a) at every step, so
      the inverse is -co2, and it equals mpz_gcdext's cofactor exactly (see
      mpz_gcdinv_pair).
    So paired NUDUPL and NUCOMP produce exactly the forms qfb_nudupl and
    qfb_nucomp produce.
*/

// Include after verifier.h (it uses form, qfb_nudupl and qfb_nucomp). The
// functions are not inline: include it in one translation unit per program.

#ifndef NUDUPL_PAIR_H
#define NUDUPL_PAIR_H

#include "xgcd_partial_fixed.h"

typedef struct {
   mp_limb_signed_t rr2, rr1, bb, aa2, aa1, bb2, bb1, i;
   int exact;  // words are exact (full GCD tail): skip the Jebelean tests
} xgcd_lehmer_round;

// One step of the reference's Lehmer inner loop. Returns 0, without changing
// the round, when the reference would stop; calling again returns 0 again.
static inline int xgcd_lehmer_step(xgcd_lehmer_round *r)
{
   if (!(r->rr1 != 0 && r->rr1 > r->bb)) return 0;
   mp_limb_signed_t qq = r->rr2 / r->rr1;
   mp_limb_signed_t t1 = r->rr2 - qq * r->rr1;
   mp_limb_signed_t t2 = r->aa2 - qq * r->aa1;
   mp_limb_signed_t t3 = r->bb2 - qq * r->bb1;
   if (r->exact)
   {
   } else if (r->i & 1)
   {
      if (t1 < -t3 || r->rr1 - t1 < t2 - r->aa1) return 0;
   } else
   {
      if (t1 < -t2 || r->rr1 - t1 < t3 - r->bb1) return 0;
   }
   r->rr2 = r->rr1; r->rr1 = t1;
   r->aa2 = r->aa1; r->aa1 = t2;
   r->bb2 = r->bb1; r->bb1 = t3;
   r->i++;
   return 1;
}

typedef struct {
   xgcd_fixed_state st;
   mp_limb_t Ld[XGCD_FIXED_LIMBS];
   int Ln;
   int active;
} xgcd_lane;

static inline void xgcd_lane_init(xgcd_lane *lane, const mpz_t r2, const mpz_t r1, const mpz_t L)
{
   xgcd_fixed_init(&lane->st, r2, r1);
   memset(lane->Ld, 0, sizeof(lane->Ld));
   lane->Ln = (int)mpz_size(L);
   if (lane->Ln) memcpy(lane->Ld, mpz_limbs_read(L), (size_t)lane->Ln * sizeof(mp_limb_t));
   lane->active = 1;
}

// Start this lane's next round, or mark it finished. A finished lane gets a
// round that stops immediately.
static inline void xgcd_lane_begin(xgcd_lane *lane, xgcd_lehmer_round *r)
{
   xgcd_fixed_state *st = &lane->st;
   if (lane->active && !(st->r1->n != 0 && xgcd_fixed_cmp_mag(st->r1, lane->Ld, lane->Ln) > 0))
      lane->active = 0;
   if (!lane->active) { r->rr1 = 0; r->i = 0; r->exact = 0; return; }
   mp_limb_signed_t bits2 = xgcd_fixed_bitlen(st->r2);
   mp_limb_signed_t bits1 = xgcd_fixed_bitlen(st->r1);
   mp_limb_signed_t bits = __GMP_MAX(bits2, bits1) - GMP_LIMB_BITS + 1;
   if (bits < 0) bits = 0;
   r->rr2 = xgcd_fixed_extract(st->r2->d, bits);
   r->rr1 = xgcd_fixed_extract(st->r1->d, bits);
   r->bb = xgcd_fixed_extract(lane->Ld, bits);
   r->aa2 = 0; r->aa1 = 1;
   r->bb2 = 1; r->bb1 = 0;
   r->i = 0;
   // Same rule as mpz_xgcd_partial_fixed.
   r->exact = lane->Ln == 0 && bits == 0;
}

static inline void xgcd_lane_end(xgcd_lane *lane, const xgcd_lehmer_round *r)
{
   if (!lane->active) return;
   if (r->i == 0)
      xgcd_fixed_division_step(&lane->st);
   else
      xgcd_fixed_apply(&lane->st, r->bb2, r->aa2, r->bb1, r->aa1);
}

// Run two partial GCDs to completion with interleaved inner loops.
static inline void xgcd_lanes_run(xgcd_lane *x, xgcd_lane *y)
{
   xgcd_lehmer_round rx, ry;
   for (;;)
   {
      xgcd_lane_begin(x, &rx);
      xgcd_lane_begin(y, &ry);
      if (!x->active && !y->active) break;
      // Both chains in one loop body; the core overlaps their divides.
      while (xgcd_lehmer_step(&rx) & xgcd_lehmer_step(&ry)) {}
      while (xgcd_lehmer_step(&rx)) {}
      while (xgcd_lehmer_step(&ry)) {}
      xgcd_lane_end(x, &rx);
      xgcd_lane_end(y, &ry);
   }
}

// Two mpz_xgcd_partial calls at once; results identical to the reference.
void mpz_xgcd_partial_pair(mpz_t co2a, mpz_t co1a, mpz_t r2a, mpz_t r1a, const mpz_t La,
                           mpz_t co2b, mpz_t co1b, mpz_t r2b, mpz_t r1b, const mpz_t Lb)
{
   if (!xgcd_fixed_supported(r2a, r1a, La) || !xgcd_fixed_supported(r2b, r1b, Lb)) {
      mpz_xgcd_partial_fixed(co2a, co1a, r2a, r1a, La);
      mpz_xgcd_partial_fixed(co2b, co1b, r2b, r1b, Lb);
      return;
   }
   xgcd_lane x, y;
   xgcd_lane_init(&x, r2a, r1a, La);
   xgcd_lane_init(&y, r2b, r1b, Lb);
   xgcd_lanes_run(&x, &y);
   xgcd_fixed_finish(&x.st, co2a, co1a, r2a, r1a);
   xgcd_fixed_finish(&y.st, co2b, co1b, r2b, r1b);
}

// g = gcd(b, a) and v with v*b == g (mod a), for two problems at once.
// Requires a > 0 and 0 <= b. Same contract as mpz_gcdext(g, v, NULL, b, a)
// up to v's representative modulo a/g.
void mpz_gcdinv_pair(mpz_t ga, mpz_t va, const mpz_t ba, const mpz_t aa,
                     mpz_t gb, mpz_t vb, const mpz_t bb, const mpz_t ab)
{
   struct pair_tls {
      mpz_t r2[2], r1[2], co1[2], zero;
      pair_tls() { for (int k = 0; k < 2; ++k) mpz_inits(r2[k], r1[k], co1[k], NULL); mpz_init(zero); }
      ~pair_tls() { for (int k = 0; k < 2; ++k) mpz_clears(r2[k], r1[k], co1[k], NULL); mpz_clear(zero); }
   };
   static thread_local pair_tls t;
   const mpz_srcptr bs[2] = {ba, bb}, as[2] = {aa, ab};
   for (int k = 0; k < 2; ++k) {
      mpz_set(t.r2[k], as[k]);
      // The Euclid sequence needs r1 < r2; b mod a keeps b's residue.
      if (mpz_cmp(bs[k], as[k]) >= 0) mpz_tdiv_r(t.r1[k], bs[k], as[k]);
      else mpz_set(t.r1[k], bs[k]);
   }
   mpz_xgcd_partial_pair(va, t.co1[0], t.r2[0], t.r1[0], t.zero,
                         vb, t.co1[1], t.r2[1], t.r1[1], t.zero);
   // At the end r1 == 0 and r2 == g == -co2 * b (mod a).
   mpz_swap(ga, t.r2[0]);
   mpz_swap(gb, t.r2[1]);
   mpz_neg(va, va);
   mpz_neg(vb, vb);

   // mpz_gcdext's cofactor is the unique one with |v| < a/(2g). Euclid's
   // cofactor always meets that bound (the last quotient is at least 2 and
   // cofactor signs alternate), so v is exactly mpz_gcdext's. Check the
   // bound cheaply by bit length, exactly if that is inconclusive, and use
   // mpz_gcdext if it ever fails.
   mpz_ptr gs[2] = {ga, gb}, vs[2] = {va, vb};
   for (int k = 0; k < 2; ++k) {
      if (mpz_sgn(vs[k]) == 0) continue;
      if (mpz_sizeinbase(vs[k], 2) + mpz_sizeinbase(gs[k], 2) + 1 < mpz_sizeinbase(as[k], 2)) continue;
      mpz_abs(t.co1[k], vs[k]);
      mpz_mul(t.co1[k], t.co1[k], gs[k]);
      mpz_mul_2exp(t.co1[k], t.co1[k], 1);
      if (mpz_cmp(t.co1[k], as[k]) >= 0)
         mpz_gcdext(gs[k], vs[k], NULL, bs[k], as[k]);
   }
}

// Thread-local scratch for one lane of qfb_nudupl_pair, as in qfb_nudupl.
struct qfb_nudupl_pair_ctx {
   mpz_t a1, c1, cb, k, s, v2, b_abs, m2, r1, r2, co1, co2, temp;
   qfb_nudupl_pair_ctx() { mpz_inits(a1, c1, cb, k, s, v2, b_abs, m2, r1, r2, co1, co2, temp, nullptr); }
   ~qfb_nudupl_pair_ctx() { mpz_clears(a1, c1, cb, k, s, v2, b_abs, m2, r1, r2, co1, co2, temp, nullptr); }
   qfb_nudupl_pair_ctx(const qfb_nudupl_pair_ctx&) = delete;
   qfb_nudupl_pair_ctx& operator=(const qfb_nudupl_pair_ctx&) = delete;
};

// rx = fx^2 and ry = fy^2 (NUDUPL, unreduced, like qfb_nudupl). The partial
// GCDs always run as a pair; pair_full_gcd also pairs the full GCDs instead
// of calling mpz_gcdext. Falls back to two qfb_nudupl calls in the rare
// a < L case.
void qfb_nudupl_pair(qfb_t rx, qfb_t fx, qfb_t ry, qfb_t fy, mpz_t const& D, mpz_t const& L,
                     bool pair_full_gcd)
{
   if (mpz_cmp(fx->a, L) < 0 || mpz_cmp(fy->a, L) < 0 || mpz_sgn(fx->a) <= 0 || mpz_sgn(fy->a) <= 0) {
      qfb_nudupl(rx, fx, D, L);
      qfb_nudupl(ry, fy, D, L);
      return;
   }
   static thread_local qfb_nudupl_pair_ctx cx, cy;
   qfb_nudupl_pair_ctx* c[2] = {&cx, &cy};
   qfb* f[2] = {fx, fy};
   qfb* r[2] = {rx, ry};

   for (int j = 0; j < 2; ++j) {
      mpz_set(c[j]->a1, f[j]->a);
      mpz_set(c[j]->c1, f[j]->c);
      mpz_abs(c[j]->b_abs, f[j]->b);
   }
   /* s = gcd(|b|, a); v2 = coefficient for |b| (mod a) */
   if (pair_full_gcd) {
      mpz_gcdinv_pair(cx.s, cx.v2, cx.b_abs, cx.a1, cy.s, cy.v2, cy.b_abs, cy.a1);
   } else {
      mpz_gcdext(cx.s, cx.v2, NULL, cx.b_abs, cx.a1);
      mpz_gcdext(cy.s, cy.v2, NULL, cy.b_abs, cy.a1);
   }

   bool partial[2];
   for (int j = 0; j < 2; ++j) {
      qfb_nudupl_pair_ctx& x = *c[j];
      if (mpz_sgn(f[j]->b) < 0) mpz_neg(x.v2, x.v2);
      mpz_mul(x.k, x.v2, x.c1);
      mpz_neg(x.k, x.k);
      if (mpz_cmp_ui(x.s, 1) != 0) {
         mpz_fdiv_q(x.a1, x.a1, x.s);
         mpz_mul(x.c1, x.c1, x.s);
      }
      mpz_tdiv_r(x.k, x.k, x.a1);
      if (mpz_sgn(x.k) < 0) mpz_add(x.k, x.k, x.a1);
      // After dividing by s, a1 can drop below L; that lane uses the short
      // branch and skips the partial GCD.
      partial[j] = mpz_cmp(x.a1, L) >= 0;
      if (partial[j]) {
         mpz_set(x.r2, x.a1);
         mpz_swap(x.r1, x.k);
      }
   }

   if (partial[0] && partial[1]) {
      mpz_xgcd_partial_pair(cx.co2, cx.co1, cx.r2, cx.r1, L, cy.co2, cy.co1, cy.r2, cy.r1, L);
   } else {
      for (int j = 0; j < 2; ++j)
         if (partial[j]) mpz_xgcd_partial_fixed(c[j]->co2, c[j]->co1, c[j]->r2, c[j]->r1, L);
   }

   for (int j = 0; j < 2; ++j) {
      qfb_nudupl_pair_ctx& x = *c[j];
      qfb* out = r[j];
      qfb* in = f[j];
      if (!partial[j]) {
         mpz_mul(x.temp, x.a1, x.k);             // t = a1*k
         mpz_mul(out->a, x.a1, x.a1);
         mpz_mul_2exp(x.cb, x.temp, 1);
         mpz_add(x.cb, x.cb, in->b);
         mpz_add(out->c, in->b, x.temp);
         mpz_mul(out->c, out->c, x.k);
         mpz_add(out->c, out->c, x.c1);
         mpz_fdiv_q(out->c, out->c, x.a1);
      } else {
         mpz_mul(x.m2, in->b, x.r1);
         mpz_submul(x.m2, x.c1, x.co1);
         mpz_divexact(x.m2, x.m2, x.a1);

         mpz_mul(out->a, x.r1, x.r1);
         mpz_submul(out->a, x.co1, x.m2);
         if (mpz_sgn(x.co1) >= 0)
            mpz_neg(out->a, out->a);

         mpz_mul(x.cb, out->a, x.co2);
         mpz_submul(x.cb, x.a1, x.r1);
         mpz_neg(x.cb, x.cb);
         mpz_mul_2exp(x.cb, x.cb, 1);
         mpz_divexact(x.cb, x.cb, x.co1);
         mpz_sub(x.cb, x.cb, in->b);
         mpz_mul_2exp(x.temp, out->a, 1);
         mpz_fdiv_r(x.cb, x.cb, x.temp);

         mpz_mul(out->c, x.cb, x.cb);
         mpz_sub(out->c, out->c, D);
         mpz_divexact(out->c, out->c, out->a);
         mpz_tdiv_q_2exp(out->c, out->c, 2);

         if (mpz_sgn(out->a) < 0) {
            mpz_neg(out->a, out->a);
            mpz_neg(out->c, out->c);
         }
      }
      mpz_set(out->b, x.cb);
   }
}

// x = x^2 and y = y^2 for two independent forms.
void nudupl_form_pair(form &x, form &y, const integer &D, const integer &L, bool pair_full_gcd)
{
   qfb fx, fy;
   *fx.a = *x.a.impl; *fx.b = *x.b.impl; *fx.c = *x.c.impl;
   *fy.a = *y.a.impl; *fy.b = *y.b.impl; *fy.c = *y.c.impl;
   qfb_nudupl_pair(&fx, &fx, &fy, &fy, D.impl, L.impl, pair_full_gcd);
   *x.a.impl = *fx.a; *x.b.impl = *fx.b; *x.c.impl = *fx.c;
   *y.a.impl = *fy.a; *y.b.impl = *fy.b; *y.c.impl = *fy.c;
}

// Thread-local scratch for one lane of qfb_nucomp_pair, as in qfb_nucomp.
struct qfb_nucomp_pair_ctx {
   mpz_t a1, a2, c2, ca, cb, cc, k, s, sp, ss, m, t, u2, v1, v2, m1, m2, r1, r2, co1, co2, temp;
   qfb_nucomp_pair_ctx() {
      mpz_inits(a1, a2, c2, ca, cb, cc, k, s, sp, ss, m, t, u2, v1, v2, m1, m2, r1, r2, co1, co2, temp, nullptr);
   }
   ~qfb_nucomp_pair_ctx() {
      mpz_clears(a1, a2, c2, ca, cb, cc, k, s, sp, ss, m, t, u2, v1, v2, m1, m2, r1, r2, co1, co2, temp, nullptr);
   }
   qfb_nucomp_pair_ctx(const qfb_nucomp_pair_ctx&) = delete;
   qfb_nucomp_pair_ctx& operator=(const qfb_nucomp_pair_ctx&) = delete;
};

// rx = fx * gx and ry = fy * gy (NUCOMP, unreduced), identical to two
// qfb_nucomp calls. Each output may alias its own lane's inputs but not the
// other lane's. Both GCDs run as pairs when both lanes need them.
void qfb_nucomp_pair(qfb_t rx, const qfb_t fx, const qfb_t gx,
                     qfb_t ry, const qfb_t fy, const qfb_t gy,
                     mpz_t const& D, mpz_t const& L)
{
   static thread_local qfb_nucomp_pair_ctx cx, cy;
   qfb_nucomp_pair_ctx* c[2] = {&cx, &cy};
   qfb* r[2] = {rx, ry};
   const qfb* fo[2] = {fx, fy};
   const qfb* go[2] = {gx, gy};
   const qfb* f[2];
   const qfb* g[2];
   bool gcd[2], partial[2];

   for (int j = 0; j < 2; ++j) {
      // Same operand order as qfb_nucomp: the smaller a first.
      bool swap = mpz_cmp(fo[j]->a, go[j]->a) > 0;
      f[j] = swap ? go[j] : fo[j];
      g[j] = swap ? fo[j] : go[j];
      qfb_nucomp_pair_ctx& x = *c[j];
      mpz_set(x.a1, f[j]->a);
      mpz_set(x.a2, g[j]->a);
      mpz_set(x.c2, g[j]->c);
      mpz_add(x.ss, f[j]->b, g[j]->b);
      mpz_fdiv_q_2exp(x.ss, x.ss, 1);
      mpz_sub(x.m, f[j]->b, g[j]->b);
      mpz_fdiv_q_2exp(x.m, x.m, 1);
      mpz_fdiv_r(x.t, x.a2, x.a1);
      gcd[j] = mpz_cmp_ui(x.t, 0) != 0;
      if (!gcd[j]) {
         mpz_set_ui(x.v1, 0);
         mpz_set(x.sp, x.a1);
      }
   }

   const bool paired_gcd = gcd[0] && gcd[1];
   if (paired_gcd) {
      mpz_gcdinv_pair(cx.sp, cx.v1, cx.t, cx.a1, cy.sp, cy.v1, cy.t, cy.a1);
   } else {
      for (int j = 0; j < 2; ++j)
         if (gcd[j]) mpz_gcdext(c[j]->sp, c[j]->v1, NULL, c[j]->t, c[j]->a1);
   }

   for (int j = 0; j < 2; ++j) {
      qfb_nucomp_pair_ctx& x = *c[j];
      // v1 is exactly mpz_gcdext's cofactor (see mpz_gcdinv_pair), so this
      // stays identical to qfb_nucomp even when sp != 1.
      mpz_mul(x.k, x.m, x.v1);
      mpz_fdiv_r(x.k, x.k, x.a1);
      if (mpz_cmp_ui(x.sp, 1)) {
         mpz_gcdext(x.s, x.v2, x.u2, x.ss, x.sp);
         mpz_mul(x.k, x.k, x.u2);
         mpz_mul(x.t, x.v2, x.c2);
         mpz_sub(x.k, x.k, x.t);
         if (mpz_cmp_ui(x.s, 1)) {
            mpz_divexact(x.a1, x.a1, x.s);
            mpz_divexact(x.a2, x.a2, x.s);
            mpz_mul(x.c2, x.c2, x.s);
         }
         mpz_fdiv_r(x.k, x.k, x.a1);
      }
      partial[j] = mpz_cmp(x.a1, L) >= 0;
      if (partial[j]) {
         mpz_set(x.r2, x.a1);
         mpz_set(x.r1, x.k);
      }
   }

   if (partial[0] && partial[1]) {
      mpz_xgcd_partial_pair(cx.co2, cx.co1, cx.r2, cx.r1, L, cy.co2, cy.co1, cy.r2, cy.r1, L);
   } else {
      for (int j = 0; j < 2; ++j)
         if (partial[j]) mpz_xgcd_partial_fixed(c[j]->co2, c[j]->co1, c[j]->r2, c[j]->r1, L);
   }

   for (int j = 0; j < 2; ++j) {
      qfb_nucomp_pair_ctx& x = *c[j];
      if (!partial[j]) {
         mpz_mul(x.t, x.a2, x.k);
         mpz_mul(x.ca, x.a2, x.a1);
         mpz_mul_2exp(x.cb, x.t, 1);
         mpz_add(x.cb, x.cb, g[j]->b);
         mpz_add(x.cc, g[j]->b, x.t);
         mpz_mul(x.cc, x.cc, x.k);
         mpz_add(x.cc, x.cc, x.c2);
         mpz_divexact(x.cc, x.cc, x.a1);
      } else {
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

         /* cb = (2 * (t - ca*co2) / co1 - g->b) % (2*ca) */
         mpz_mul(x.cb, x.ca, x.co2);
         mpz_sub(x.cb, x.t, x.cb);
         mpz_mul_2exp(x.cb, x.cb, 1);
         mpz_divexact(x.cb, x.cb, x.co1);
         mpz_sub(x.cb, x.cb, g[j]->b);
         mpz_mul_2exp(x.temp, x.ca, 1);
         mpz_fdiv_r(x.cb, x.cb, x.temp);

         /* cc = (cb*cb - D) / (4*ca) */
         mpz_mul(x.cc, x.cb, x.cb);
         mpz_sub(x.cc, x.cc, D);
         mpz_divexact(x.cc, x.cc, x.ca);
         mpz_fdiv_q_2exp(x.cc, x.cc, 2);

         if (mpz_sgn(x.ca) < 0) {
            mpz_neg(x.ca, x.ca);
            mpz_neg(x.cc, x.cc);
         }
      }
      mpz_set(r[j]->a, x.ca);
      mpz_set(r[j]->b, x.cb);
      mpz_set(r[j]->c, x.cc);
   }
}

// ax = bx * cx and ay = by * cy for two independent products.
void nucomp_form_pair(form &ax, const form &bx, const form &cx,
                      form &ay, const form &by, const form &cy,
                      const integer &D, const integer &L)
{
   qfb rx, fx, gx, ry, fy, gy;
   *rx.a = *ax.a.impl; *rx.b = *ax.b.impl; *rx.c = *ax.c.impl;
   *fx.a = *bx.a.impl; *fx.b = *bx.b.impl; *fx.c = *bx.c.impl;
   *gx.a = *cx.a.impl; *gx.b = *cx.b.impl; *gx.c = *cx.c.impl;
   *ry.a = *ay.a.impl; *ry.b = *ay.b.impl; *ry.c = *ay.c.impl;
   *fy.a = *by.a.impl; *fy.b = *by.b.impl; *fy.c = *by.c.impl;
   *gy.a = *cy.a.impl; *gy.b = *cy.b.impl; *gy.c = *cy.c.impl;
   qfb_nucomp_pair(&rx, &fx, &gx, &ry, &fy, &gy, D.impl, L.impl);
   *ax.a.impl = *rx.a; *ax.b.impl = *rx.b; *ax.c.impl = *rx.c;
   *ay.a.impl = *ry.a; *ay.b.impl = *ry.b; *ay.c.impl = *ry.c;
}

#endif // NUDUPL_PAIR_H
