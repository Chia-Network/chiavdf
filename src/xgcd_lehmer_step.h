#ifndef XGCD_LEHMER_STEP_H
#define XGCD_LEHMER_STEP_H

#include <gmp.h>

/*
    The single-limb Lehmer inner loop of mpz_xgcd_partial (xgcd_partial.c),
    shared with the fixed-size and paired variants (pair_kernels.cpp).

    A round runs Euclid on word approximations rr2, rr1 of the remainders and
    accumulates the cofactor matrix, so that rr2 = bb2*A + aa2*B and
    rr1 = bb1*A + aa1*B for the approximated inputs A, B. It stops at the
    Jebelean condition (or once rr1 <= bb), after i steps. With exact set,
    the words are the exact values (a full GCD once both fit in a word), so
    the Jebelean test is skipped.
*/
typedef struct {
    mp_limb_signed_t rr2, rr1, bb, aa2, aa1, bb2, bb1, i;
    int exact;
} xgcd_lehmer_round;

static inline void xgcd_lehmer_begin(xgcd_lehmer_round *r, mp_limb_signed_t rr2, mp_limb_signed_t rr1,
                                     mp_limb_signed_t bb, int exact)
{
    r->rr2 = rr2;
    r->rr1 = rr1;
    r->bb = bb;
    r->aa2 = 0;
    r->aa1 = 1;
    r->bb2 = 1;
    r->bb1 = 0;
    r->i = 0;
    r->exact = exact;
}

// One step. Returns 0, leaving the round unchanged, where the loop stops;
// calling again returns 0 again.
static inline int xgcd_lehmer_step(xgcd_lehmer_round *r)
{
    mp_limb_signed_t qq, t1, t2, t3;

    if (!(r->rr1 != 0 && r->rr1 > r->bb))
        return 0;
    qq = r->rr2 / r->rr1;
    t1 = r->rr2 - qq * r->rr1;
    t2 = r->aa2 - qq * r->aa1;
    t3 = r->bb2 - qq * r->bb1;
    if (!r->exact) {
        if (r->i & 1) {
            if (t1 < -t3 || r->rr1 - t1 < t2 - r->aa1)
                return 0;
        } else {
            if (t1 < -t2 || r->rr1 - t1 < t3 - r->bb1)
                return 0;
        }
    }
    r->rr2 = r->rr1;
    r->rr1 = t1;
    r->aa2 = r->aa1;
    r->aa1 = t2;
    r->bb2 = r->bb1;
    r->bb1 = t3;
    r->i++;
    return 1;
}

#endif // XGCD_LEHMER_STEP_H
