#define CHIAVDF_NUCOMP_DECLARATION_ONLY
#include "nucomp.h"

// One scratch per thread. File scope, so operand ordering cannot re-enter it.
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

static thread_local qfb_nucomp_ctx qfb_nucomp_scratch;

// MSVC link-time optimization miscompiles this function when it is inlined,
// and the Windows package then divides by zero. Keep the definition out of line.
#if defined(_MSC_VER)
__declspec(noinline)
#endif
void qfb_nucomp(qfb_t r, const qfb_t f, const qfb_t g, mpz_t const& D, mpz_t const& L)
{
    // Order the inputs once. A recursive call would re-enter the scratch above.
    const qfb *left = f;
    const qfb *right = g;
    if (mpz_cmp(f->a, g->a) > 0) {
        left = g;
        right = f;
    }

    qfb_nucomp_ctx *ctx = &qfb_nucomp_scratch;

    mpz_set(ctx->a1, left->a);
    mpz_set(ctx->a2, right->a);
    mpz_set(ctx->c2, right->c);

    mpz_add(ctx->ss, left->b, right->b);
    mpz_fdiv_q_2exp(ctx->ss, ctx->ss, 1);

    mpz_sub(ctx->m, left->b, right->b);
    mpz_fdiv_q_2exp(ctx->m, ctx->m, 1);

    mpz_fdiv_r(ctx->t, ctx->a2, ctx->a1);
    if (!mpz_cmp_ui(ctx->t, 0)) {
        mpz_set_ui(ctx->v1, 0);
        mpz_set(ctx->sp, ctx->a1);
    } else {
        mpz_gcdext(ctx->sp, ctx->v1, NULL, ctx->t, ctx->a1);
    }

    mpz_mul(ctx->k, ctx->m, ctx->v1);
    mpz_fdiv_r(ctx->k, ctx->k, ctx->a1);

    if (mpz_cmp_ui(ctx->sp, 1)) {
        mpz_gcdext(ctx->s, ctx->v2, ctx->u2, ctx->ss, ctx->sp);

        /* k = k*u2 - v2*c2 */
        mpz_mul(ctx->k, ctx->k, ctx->u2);
        mpz_mul(ctx->t, ctx->v2, ctx->c2);
        mpz_sub(ctx->k, ctx->k, ctx->t);

        if (mpz_cmp_ui(ctx->s, 1)) {
            mpz_divexact(ctx->a1, ctx->a1, ctx->s);
            mpz_divexact(ctx->a2, ctx->a2, ctx->s);
            mpz_mul(ctx->c2, ctx->c2, ctx->s);
        }

        mpz_fdiv_r(ctx->k, ctx->k, ctx->a1);
    }

    if (mpz_cmp(ctx->a1, L) < 0) {
        mpz_mul(ctx->t, ctx->a2, ctx->k);

        mpz_mul(ctx->ca, ctx->a2, ctx->a1);

        mpz_mul_2exp(ctx->cb, ctx->t, 1);
        mpz_add(ctx->cb, ctx->cb, right->b);

        mpz_add(ctx->cc, right->b, ctx->t);
        mpz_mul(ctx->cc, ctx->cc, ctx->k);
        mpz_add(ctx->cc, ctx->cc, ctx->c2);

        mpz_divexact(ctx->cc, ctx->cc, ctx->a1);
    } else {
        mpz_set(ctx->r2, ctx->a1);
        mpz_set(ctx->r1, ctx->k);

        mpz_xgcd_partial(ctx->co2, ctx->co1, ctx->r2, ctx->r1, L);

        /* m1 = (m*co1 + a2*r1) / a1 */
        mpz_mul(ctx->t, ctx->a2, ctx->r1);
        mpz_mul(ctx->m1, ctx->m, ctx->co1);
        mpz_add(ctx->m1, ctx->m1, ctx->t);
        mpz_divexact(ctx->m1, ctx->m1, ctx->a1);

        /* m2 = (ss*r1 - c2*co1) / a1 */
        mpz_mul(ctx->m2, ctx->ss, ctx->r1);
        mpz_mul(ctx->temp, ctx->c2, ctx->co1);
        mpz_sub(ctx->m2, ctx->m2, ctx->temp);
        mpz_divexact(ctx->m2, ctx->m2, ctx->a1);

        /* ca = -sgn(co1) * (r1*m1 - co1*m2) */
        mpz_mul(ctx->ca, ctx->r1, ctx->m1);
        mpz_mul(ctx->temp, ctx->co1, ctx->m2);
        if (mpz_sgn(ctx->co1) < 0)
            mpz_sub(ctx->ca, ctx->ca, ctx->temp);
        else
            mpz_sub(ctx->ca, ctx->temp, ctx->ca);

        /* cb = (2 * (t - ca*co2) / co1 - right->b) % (2*ca) */
        mpz_mul(ctx->cb, ctx->ca, ctx->co2);
        mpz_sub(ctx->cb, ctx->t, ctx->cb);
        mpz_mul_2exp(ctx->cb, ctx->cb, 1);
        mpz_divexact(ctx->cb, ctx->cb, ctx->co1);
        mpz_sub(ctx->cb, ctx->cb, right->b);
        mpz_mul_2exp(ctx->temp, ctx->ca, 1);
        mpz_fdiv_r(ctx->cb, ctx->cb, ctx->temp);

        /* cc = (cb*cb - D) / (4*ca) */
        mpz_mul(ctx->cc, ctx->cb, ctx->cb);
        mpz_sub(ctx->cc, ctx->cc, D);
        mpz_divexact(ctx->cc, ctx->cc, ctx->ca);
        mpz_fdiv_q_2exp(ctx->cc, ctx->cc, 2);

        if (mpz_sgn(ctx->ca) < 0) {
            mpz_neg(ctx->ca, ctx->ca);
            mpz_neg(ctx->cc, ctx->cc);
        }
    }

    mpz_set(r->a, ctx->ca);
    mpz_set(r->b, ctx->cb);
    mpz_set(r->c, ctx->cc);
}
