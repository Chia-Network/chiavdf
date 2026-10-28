#include "pair_kernels.h"

#if defined(CHIAVDF_PAIR_KERNELS)

#include "xgcd_lehmer_step.h"
#include "xgcd_partial.h"

#include <algorithm>
#include <string.h>

static_assert(GMP_LIMB_BITS == 64, "The paired kernels assume 64-bit limbs");

namespace {

// Operands up to 640 bits use the fixed arrays: 1024-bit discriminants give
// remainders of at most about 9 limbs. Larger ones silently take the
// reference mpz_xgcd_partial, with the same result.
constexpr int kMaxInputLimbs = 10;
constexpr int kLimbs = kMaxInputLimbs + 2;

typedef unsigned __int128 u128;

// Sign-magnitude integer; n == 0 means zero and then neg == 0. Every limb at
// index n and above is zero, so loops can read past n without bounds checks.
struct FixedInt {
    mp_limb_t d[kLimbs];
    int n;
    int neg;
};

void normalize(FixedInt* x)
{
    while (x->n > 0 && x->d[x->n - 1] == 0)
        x->n--;
    if (x->n == 0)
        x->neg = 0;
}

// Limb access through the mpz fields, which GMP and MPIR share (MPIR lacks
// GMP 6's mpz_limbs_* functions).
void from_mpz(FixedInt* x, mpz_srcptr z)
{
    // Callers pass operands within kMaxInputLimbs and Euclid values derived
    // from them, which are no larger; the bound only makes that visible.
    size_t n = std::min(mpz_size(z), (size_t)kLimbs - 1);
    memset(x->d, 0, sizeof(x->d));
    memcpy(x->d, z->_mp_d, n * sizeof(mp_limb_t));
    x->n = (int)n;
    x->neg = mpz_sgn(z) < 0;
}

void to_mpz(mpz_ptr z, const FixedInt* x)
{
    if (x->n == 0) {
        mpz_set_ui(z, 0);
        return;
    }
    if (z->_mp_alloc < x->n)
        _mpz_realloc(z, x->n);
    memcpy(z->_mp_d, x->d, (size_t)x->n * sizeof(mp_limb_t));
    z->_mp_size = x->neg ? -x->n : x->n;
}

// Same contract as chiavdf_mpz_bitlen_nonneg: 1 for zero.
mp_limb_signed_t bitlen(const FixedInt* x)
{
    if (x->n == 0)
        return 1;
    return (mp_limb_signed_t)(x->n - 1) * GMP_LIMB_BITS + (GMP_LIMB_BITS - __builtin_clzll(x->d[x->n - 1]));
}

// Same contract as chiavdf_mpz_extract_uword_from_shift_nonneg.
mp_limb_signed_t extract(const mp_limb_t* d, mp_limb_signed_t shift)
{
    if (shift <= 0)
        return (mp_limb_signed_t)d[0];
    mp_limb_signed_t idx = shift / GMP_LIMB_BITS;
    mp_limb_signed_t off = shift - idx * GMP_LIMB_BITS;
    mp_limb_t lo = d[idx];
    if (off == 0)
        return (mp_limb_signed_t)lo;
    return (mp_limb_signed_t)((lo >> off) | (d[idx + 1] << (GMP_LIMB_BITS - off)));
}

int cmp_mag(const FixedInt* a, const mp_limb_t* b, int bn)
{
    if (a->n != bn)
        return a->n < bn ? -1 : 1;
    for (int i = bn - 1; i >= 0; --i)
        if (a->d[i] != b[i])
            return a->d[i] < b[i] ? -1 : 1;
    return 0;
}

// out = u*x + v*y, exact, with |u|, |v| < 2^63. out must not alias x or y.
void lincomb(FixedInt* out, mp_limb_signed_t u, const FixedInt* x, mp_limb_signed_t v, const FixedInt* y)
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
            u128 p = (u128)mu * x->d[i] + cp;
            u128 q = (u128)mv * y->d[i] + cq;
            u128 s = (u128)(mp_limb_t)p + (mp_limb_t)q + c;
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
            u128 p = (u128)mu * x->d[i] + cp;
            u128 q = (u128)mv * y->d[i] + cq;
            mp_limb_t pl = (mp_limb_t)p, ql = (mp_limb_t)q;
            out->d[i] = pl - ql - borrow;
            borrow = (pl < ql) | ((pl - ql) < borrow);
            cp = (mp_limb_t)(p >> 64);
            cq = (mp_limb_t)(q >> 64);
        }
        bool negative = (cp < cq) || (cp - cq < borrow);
        out->d[n] = cp - cq - borrow;
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
    for (int i = n + 1; i < old; ++i)
        out->d[i] = 0;
    normalize(out);
}

bool supported(mpz_srcptr r2, mpz_srcptr r1, mpz_srcptr L)
{
    return mpz_sgn(r2) >= 0 && mpz_sgn(r1) >= 0 && mpz_sgn(L) >= 0 && mpz_size(r2) <= kMaxInputLimbs &&
           mpz_size(r1) <= kMaxInputLimbs && mpz_size(L) <= kMaxInputLimbs;
}

// One partial GCD in progress: remainders, cofactors and the limit.
struct Lane {
    FixedInt rb[4], cb[4];
    FixedInt *r2, *r1, *rt, *rs;
    FixedInt *co2, *co1, *ct, *cs;
    mp_limb_t Ld[kLimbs];
    int Ln;
    bool active;

    Lane(mpz_srcptr r2_in, mpz_srcptr r1_in, mpz_srcptr L)
    {
        memset(rb, 0, sizeof(rb));
        memset(cb, 0, sizeof(cb));
        memset(Ld, 0, sizeof(Ld));
        r2 = &rb[0]; r1 = &rb[1]; rt = &rb[2]; rs = &rb[3];
        co2 = &cb[0]; co1 = &cb[1]; ct = &cb[2]; cs = &cb[3];
        from_mpz(r2, r2_in);
        from_mpz(r1, r1_in);
        co1->n = 1; // co2 = 0, co1 = -1
        co1->neg = 1;
        co1->d[0] = 1;
        Ln = (int)mpz_size(L);
        if (Ln)
            memcpy(Ld, L->_mp_d, (size_t)Ln * sizeof(mp_limb_t));
        active = true;
    }

    // Start the next round, or mark the lane finished; a finished lane gets
    // a round that stops immediately.
    void begin(xgcd_lehmer_round* round)
    {
        if (active && !(r1->n != 0 && cmp_mag(r1, Ld, Ln) > 0))
            active = false;
        if (!active) {
            xgcd_lehmer_begin(round, 0, 0, 0, 0);
            return;
        }
        mp_limb_signed_t bits = bitlen(r2) > bitlen(r1) ? bitlen(r2) : bitlen(r1);
        bits -= GMP_LIMB_BITS - 1;
        if (bits < 0)
            bits = 0;
        // With L == 0 (a full GCD) the end state is unique, so once both
        // values fit in a word the words are exact and plain Euclid can
        // finish without the Jebelean tests. For L > 0 keep the reference's
        // decisions exactly.
        xgcd_lehmer_begin(round, extract(r2->d, bits), extract(r1->d, bits), extract(Ld, bits),
                          Ln == 0 && bits == 0);
    }

    void end(const xgcd_lehmer_round* round)
    {
        if (!active)
            return;
        if (round->i == 0)
            division_step();
        else
            apply(round->bb2, round->aa2, round->bb1, round->aa1);
    }

    // new r2 = bb2*r2 + aa2*r1, new r1 = aa1*r1 + bb1*r2 (old values), the
    // same on the cofactors, then the reference's sign fixes.
    void apply(mp_limb_signed_t bb2, mp_limb_signed_t aa2, mp_limb_signed_t bb1, mp_limb_signed_t aa1)
    {
        lincomb(rt, bb2, r2, aa2, r1);
        lincomb(rs, aa1, r1, bb1, r2);
        lincomb(ct, bb2, co2, aa2, co1);
        lincomb(cs, aa1, co1, bb1, co2);
        FixedInt* t;
        t = r2; r2 = rt; rt = t;
        t = r1; r1 = rs; rs = t;
        t = co2; co2 = ct; ct = t;
        t = co1; co1 = cs; cs = t;
        if (r1->neg) {
            co1->neg = co1->n ? !co1->neg : 0;
            r1->neg = 0;
        }
        if (r2->neg) {
            co2->neg = co2->n ? !co2->neg : 0;
            r2->neg = 0;
        }
    }

    // One exact Euclid step (the reference's i == 0 branch). Rare, so it goes
    // through mpz.
    void division_step()
    {
        mpz_t q, a, b, c2, c1;
        mpz_inits(q, a, b, c2, c1, NULL);
        to_mpz(a, r2);
        to_mpz(b, r1);
        to_mpz(c2, co2);
        to_mpz(c1, co1);
        mpz_tdiv_qr(q, a, a, b);
        mpz_submul(c2, c1, q);
        // r2 <- old r1, r1 <- remainder; co2 <- old co1, co1 <- updated co2.
        from_mpz(r2, a);
        from_mpz(co2, c2);
        FixedInt* t = r2; r2 = r1; r1 = t;
        t = co2; co2 = co1; co1 = t;
        mpz_clears(q, a, b, c2, c1, NULL);
    }

    void finish(mpz_ptr co2_out, mpz_ptr co1_out, mpz_ptr r2_out, mpz_ptr r1_out)
    {
        if (r2->neg) {
            co2->neg = co2->n ? !co2->neg : 0;
            co1->neg = co1->n ? !co1->neg : 0;
            r2->neg = 0;
        }
        to_mpz(co2_out, co2);
        to_mpz(co1_out, co1);
        to_mpz(r2_out, r2);
        to_mpz(r1_out, r1);
    }
};

}  // namespace

void mpz_xgcd_partial_fixed(mpz_ptr co2, mpz_ptr co1, mpz_ptr r2, mpz_ptr r1, mpz_srcptr L)
{
    if (!supported(r2, r1, L)) {
        mpz_xgcd_partial(co2, co1, r2, r1, L);
        return;
    }
    Lane lane(r2, r1, L);
    xgcd_lehmer_round round;
    for (;;) {
        lane.begin(&round);
        if (!lane.active)
            break;
        while (xgcd_lehmer_step(&round)) {}
        lane.end(&round);
    }
    lane.finish(co2, co1, r2, r1);
}

void mpz_xgcd_partial_pair(mpz_ptr co2a, mpz_ptr co1a, mpz_ptr r2a, mpz_ptr r1a, mpz_srcptr La,
                           mpz_ptr co2b, mpz_ptr co1b, mpz_ptr r2b, mpz_ptr r1b, mpz_srcptr Lb)
{
    if (!supported(r2a, r1a, La) || !supported(r2b, r1b, Lb)) {
        mpz_xgcd_partial_fixed(co2a, co1a, r2a, r1a, La);
        mpz_xgcd_partial_fixed(co2b, co1b, r2b, r1b, Lb);
        return;
    }
    Lane x(r2a, r1a, La), y(r2b, r1b, Lb);
    xgcd_lehmer_round rx, ry;
    for (;;) {
        x.begin(&rx);
        y.begin(&ry);
        if (!x.active && !y.active)
            break;
        // Both chains in one loop body; the core overlaps their divides.
        while (xgcd_lehmer_step(&rx) & xgcd_lehmer_step(&ry)) {}
        while (xgcd_lehmer_step(&rx)) {}
        while (xgcd_lehmer_step(&ry)) {}
        x.end(&rx);
        y.end(&ry);
    }
    x.finish(co2a, co1a, r2a, r1a);
    y.finish(co2b, co1b, r2b, r1b);
}

void mpz_gcdinv_settle(mpz_ptr g, mpz_ptr v, mpz_srcptr b, mpz_srcptr a)
{
    // mpz_gcdext's cofactor is the unique one with |v| < a/(2g). Euclid's
    // cofactor meets that bound (the last quotient is at least 2 and the
    // cofactor signs alternate). Check it by bit length, exactly if that is
    // inconclusive, and fall back to mpz_gcdext if it ever fails.
    if (mpz_sgn(v) == 0)
        return;
    if (mpz_sizeinbase(v, 2) + mpz_sizeinbase(g, 2) + 1 < mpz_sizeinbase(a, 2))
        return;
    mpz_t bound;
    mpz_init(bound);
    mpz_abs(bound, v);
    mpz_mul(bound, bound, g);
    mpz_mul_2exp(bound, bound, 1);
    if (mpz_cmp(bound, a) >= 0)
        mpz_gcdext(g, v, NULL, b, a);
    mpz_clear(bound);
}

void mpz_gcdinv_pair(mpz_ptr ga, mpz_ptr va, mpz_srcptr ba, mpz_srcptr aa,
                     mpz_ptr gb, mpz_ptr vb, mpz_srcptr bb, mpz_srcptr ab)
{
    struct Scratch {
        mpz_t r2[2], r1[2], co1[2], zero;
        Scratch()
        {
            for (int k = 0; k < 2; ++k)
                mpz_inits(r2[k], r1[k], co1[k], NULL);
            mpz_init(zero);
        }
        ~Scratch()
        {
            for (int k = 0; k < 2; ++k)
                mpz_clears(r2[k], r1[k], co1[k], NULL);
            mpz_clear(zero);
        }
    };
    static thread_local Scratch t;
    const mpz_srcptr bs[2] = {ba, bb}, as[2] = {aa, ab};
    mpz_ptr gs[2] = {ga, gb}, vs[2] = {va, vb};
    for (int k = 0; k < 2; ++k) {
        mpz_set(t.r2[k], as[k]);
        // The Euclid sequence needs r1 < r2; b mod a keeps b's residue.
        if (mpz_cmp(bs[k], as[k]) >= 0)
            mpz_tdiv_r(t.r1[k], bs[k], as[k]);
        else
            mpz_set(t.r1[k], bs[k]);
    }
    // The partial GCD with L = 0 runs Euclid to the end. Then r2 == g and
    // r_i == -co_i * b (mod a) at every step, so the inverse is -co2.
    mpz_xgcd_partial_pair(va, t.co1[0], t.r2[0], t.r1[0], t.zero,
                          vb, t.co1[1], t.r2[1], t.r1[1], t.zero);
    for (int k = 0; k < 2; ++k) {
        mpz_swap(gs[k], t.r2[k]);
        mpz_neg(vs[k], vs[k]);
        mpz_gcdinv_settle(gs[k], vs[k], bs[k], as[k]);
    }
}

void qfb_nudupl_pair(qfb* rx, const qfb* fx, qfb* ry, const qfb* fy,
                     mpz_srcptr D, mpz_srcptr L, bool pair_full_gcd)
{
    static thread_local qfb_nudupl_ctx cx, cy;
    qfb_nudupl_ctx* c[2] = {&cx, &cy};
    const qfb* f[2] = {fx, fy};
    qfb* r[2] = {rx, ry};
    bool partial[2];

    for (int j = 0; j < 2; ++j)
        qfb_nudupl_prepare(*c[j], f[j]);
    if (pair_full_gcd && mpz_sgn(cx.a1) > 0 && mpz_sgn(cy.a1) > 0) {
        mpz_gcdinv_pair(cx.s, cx.v2, cx.b_abs, cx.a1, cy.s, cy.v2, cy.b_abs, cy.a1);
    } else {
        for (int j = 0; j < 2; ++j)
            mpz_gcdext(c[j]->s, c[j]->v2, NULL, c[j]->b_abs, c[j]->a1);
    }
    for (int j = 0; j < 2; ++j)
        partial[j] = qfb_nudupl_reduce_k(*c[j], f[j], L);
    if (partial[0] && partial[1]) {
        mpz_xgcd_partial_pair(cx.co2, cx.co1, cx.r2, cx.r1, L, cy.co2, cy.co1, cy.r2, cy.r1, L);
    } else {
        for (int j = 0; j < 2; ++j)
            if (partial[j])
                mpz_xgcd_partial_fixed(c[j]->co2, c[j]->co1, c[j]->r2, c[j]->r1, L);
    }
    for (int j = 0; j < 2; ++j)
        qfb_nudupl_finish(*c[j], r[j], f[j], D, partial[j]);
}

void qfb_nucomp_pair(qfb* rx, const qfb* fx, const qfb* gx, qfb* ry, const qfb* fy, const qfb* gy,
                     mpz_srcptr D, mpz_srcptr L)
{
    static thread_local qfb_nucomp_ctx cx, cy;
    qfb_nucomp_ctx* c[2] = {&cx, &cy};
    qfb* r[2] = {rx, ry};
    const qfb* right[2];
    bool gcd[2], partial[2];

    for (int j = 0; j < 2; ++j) {
        const qfb* f = j ? fy : fx;
        const qfb* g = j ? gy : gx;
        // Same operand order as qfb_nucomp: the smaller a first.
        bool swap = mpz_cmp(f->a, g->a) > 0;
        right[j] = swap ? f : g;
        gcd[j] = qfb_nucomp_prepare(*c[j], swap ? g : f, right[j]);
    }
    if (gcd[0] && gcd[1]) {
        mpz_gcdinv_pair(cx.sp, cx.v1, cx.t, cx.a1, cy.sp, cy.v1, cy.t, cy.a1);
    } else {
        for (int j = 0; j < 2; ++j)
            if (gcd[j])
                mpz_gcdext(c[j]->sp, c[j]->v1, NULL, c[j]->t, c[j]->a1);
    }
    for (int j = 0; j < 2; ++j)
        partial[j] = qfb_nucomp_reduce_k(*c[j], L);
    if (partial[0] && partial[1]) {
        mpz_xgcd_partial_pair(cx.co2, cx.co1, cx.r2, cx.r1, L, cy.co2, cy.co1, cy.r2, cy.r1, L);
    } else {
        for (int j = 0; j < 2; ++j)
            if (partial[j])
                mpz_xgcd_partial_fixed(c[j]->co2, c[j]->co1, c[j]->r2, c[j]->r1, L);
    }
    for (int j = 0; j < 2; ++j)
        qfb_nucomp_finish(*c[j], r[j], right[j], D, partial[j]);
}

#endif // CHIAVDF_PAIR_KERNELS
