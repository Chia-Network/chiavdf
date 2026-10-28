#ifndef PAIR_KERNELS_H
#define PAIR_KERNELS_H

/*
    Paired class-group kernels: two independent NUDUPL or NUCOMP operations
    with their GCD loops interleaved in one thread.

    A Lehmer quotient step is bound by its divide-then-multiply-subtract
    chain and leaves most of a wide out-of-order core idle. Running two
    unrelated problems through one loop lets the core overlap the two chains.
    This helps independent work (separate fills, separate bucket products),
    not one sequential squaring chain.

    Every function here produces exactly what the reference produces:
    - mpz_xgcd_partial_fixed and mpz_xgcd_partial_pair give the same state
      as mpz_xgcd_partial (same Lehmer decisions, xgcd_lehmer_step.h);
    - mpz_gcdinv_pair gives the same gcd and cofactor as
      mpz_gcdext(g, v, NULL, b, a);
    - qfb_nudupl_pair and qfb_nucomp_pair give the same unreduced forms as
      qfb_nudupl and qfb_nucomp (shared phases, nucomp_phases.h).

    Definitions are in pair_kernels.cpp. Its fallback for operands the fixed
    arrays cannot hold is mpz_xgcd_partial, so a program that links it must
    also include nucomp.h in some translation unit, as every user of the
    class-group arithmetic does.
*/

#include "pair_kernels_config.h"

#if defined(CHIAVDF_PAIR_KERNELS)

#include "nucomp_phases.h"

// mpz_xgcd_partial on fixed, zero-padded limb arrays.
void mpz_xgcd_partial_fixed(mpz_ptr co2, mpz_ptr co1, mpz_ptr r2, mpz_ptr r1, mpz_srcptr L);

// Two mpz_xgcd_partial calls with interleaved inner loops.
void mpz_xgcd_partial_pair(mpz_ptr co2a, mpz_ptr co1a, mpz_ptr r2a, mpz_ptr r1a, mpz_srcptr La,
                           mpz_ptr co2b, mpz_ptr co1b, mpz_ptr r2b, mpz_ptr r1b, mpz_srcptr Lb);

// g = gcd(b, a) and v with v*b == g (mod a) for two problems at once, as
// mpz_gcdext(g, v, NULL, b, a) returns them. Requires a > 0 and b >= 0.
void mpz_gcdinv_pair(mpz_ptr ga, mpz_ptr va, mpz_srcptr ba, mpz_srcptr aa,
                     mpz_ptr gb, mpz_ptr vb, mpz_srcptr bb, mpz_srcptr ab);

// The check mpz_gcdinv_pair applies to each result: if v is not the
// cofactor with |v| < a/(2g) that mpz_gcdext returns, recompute g and v with
// mpz_gcdext. Euclid's cofactor always meets that bound; this is a guard.
void mpz_gcdinv_settle(mpz_ptr g, mpz_ptr v, mpz_srcptr b, mpz_srcptr a);

// rx = fx^2 and ry = fy^2, as qfb_nudupl. The partial GCDs run as a pair;
// pair_full_gcd also pairs the full GCDs instead of calling mpz_gcdext.
// Each output may alias its own lane's input but not the other lane's.
void qfb_nudupl_pair(qfb* rx, const qfb* fx, qfb* ry, const qfb* fy,
                     mpz_srcptr D, mpz_srcptr L, bool pair_full_gcd);

// rx = fx * gx and ry = fy * gy, as qfb_nucomp, with both GCDs paired.
// Each output may alias its own lane's inputs but not the other lane's.
void qfb_nucomp_pair(qfb* rx, const qfb* fx, const qfb* gx, qfb* ry, const qfb* fy, const qfb* gy,
                     mpz_srcptr D, mpz_srcptr L);

// Wrappers over the form classes, which share their mpz limbs with the qfb
// views below (as nudupl_form and nucomp_form do).
template <class Form>
inline void qfb_view(qfb* q, const Form& f)
{
    *q->a = *f.a.impl;
    *q->b = *f.b.impl;
    *q->c = *f.c.impl;
}

template <class Form>
inline void qfb_store(Form& f, const qfb* q)
{
    *f.a.impl = *q->a;
    *f.b.impl = *q->b;
    *f.c.impl = *q->c;
}

// x = x^2 and y = y^2 for two independent forms.
template <class Form, class Integer>
inline void nudupl_form_pair(Form& x, Form& y, const Integer& D, const Integer& L, bool pair_full_gcd)
{
    qfb fx, fy;
    qfb_view(&fx, x);
    qfb_view(&fy, y);
    qfb_nudupl_pair(&fx, &fx, &fy, &fy, D.impl, L.impl, pair_full_gcd);
    qfb_store(x, &fx);
    qfb_store(y, &fy);
}

// ax = bx * cx and ay = by * cy for two independent products. ax may alias
// bx or cx, and ay may alias by or cy; the lanes must not alias each other.
template <class Form, class Integer>
inline void nucomp_form_pair(Form& ax, const Form& bx, const Form& cx,
                             Form& ay, const Form& by, const Form& cy,
                             const Integer& D, const Integer& L)
{
    qfb rx, fx, gx, ry, fy, gy;
    qfb_view(&rx, ax);
    qfb_view(&fx, bx);
    qfb_view(&gx, cx);
    qfb_view(&ry, ay);
    qfb_view(&fy, by);
    qfb_view(&gy, cy);
    qfb_nucomp_pair(&rx, &fx, &gx, &ry, &fy, &gy, D.impl, L.impl);
    qfb_store(ax, &rx);
    qfb_store(ay, &ry);
}

#endif // CHIAVDF_PAIR_KERNELS

#endif // PAIR_KERNELS_H
