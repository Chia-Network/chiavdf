#include "bqfc.h"

#include <stdlib.h>
#include <string.h>

int bqfc_compr(struct qfb_c *out_c, mpz_t a, mpz_t b)
{
    mpz_t a_sqrt, a_copy, b_copy, dummy;
    bool sign;

    if (!mpz_cmp(a, b)) {
        mpz_set(out_c->a, a);
        mpz_set_ui(out_c->t, 0);
        mpz_set_ui(out_c->g, 0);
        mpz_set_ui(out_c->b0, 0);
        out_c->b_sign = false;
        return 0;
    }

    mpz_inits(a_sqrt, a_copy, b_copy, dummy, NULL);

    sign = mpz_sgn(b) < 0;
    mpz_sqrt(a_sqrt, a);
    mpz_set(a_copy, a);
    mpz_set(b_copy, b);
    if (sign)
        mpz_neg(b_copy, b_copy);

    /* a and b are copied since xgcd_partial overwrites them */
    mpz_xgcd_partial(dummy, out_c->t, a_copy, b_copy, a_sqrt);
    /* xgcd_partial sets the opposite sign for out_c->t */
    mpz_neg(out_c->t, out_c->t);

    mpz_gcd(out_c->g, a, out_c->t);
    if (!mpz_cmp_ui(out_c->g, 1)) {
        mpz_set(out_c->a, a);
        mpz_set_ui(out_c->b0, 0);
    } else {
        mpz_divexact(out_c->a, a, out_c->g);
        mpz_divexact(out_c->t, out_c->t, out_c->g);

        mpz_tdiv_q(out_c->b0, b, out_c->a);
        if (sign)
            mpz_neg(out_c->b0, out_c->b0);
    }

    out_c->b_sign = sign;
    mpz_clears(a_sqrt, a_copy, b_copy, dummy, NULL);
    return 0;
}

int bqfc_decompr(mpz_t out_a, mpz_t out_b, const mpz_t D, const struct qfb_c *c)
{
    int ret = 0;
    mpz_t tmp, t, t_inv, d;

    if (!mpz_sgn(c->t)) {
        mpz_set(out_a, c->a);
        mpz_set(out_b, c->a);
        return 0;
    }

    mpz_inits(tmp, t, t_inv, d, NULL);

    if (mpz_sgn(c->t) < 0) {
        mpz_add(t, c->t, c->a);
    } else {
        mpz_set(t, c->t);
    }

    mpz_gcdext(tmp, t_inv, NULL, t, c->a);
    if (mpz_cmp_ui(tmp, 1)) {
        ret = -1;
        goto out;
    }
    if (mpz_sgn(t_inv) < 0) {
        mpz_add(t_inv, t_inv, c->a);
    }

    mpz_fdiv_r(d, D, c->a);
    /* tmp = sqrt(t**2 * d % a) */
    mpz_powm_ui(tmp, c->t, 2, c->a);
    mpz_mul(tmp, tmp, d);
    mpz_tdiv_r(tmp, tmp, c->a);
    if (!mpz_perfect_square_p(tmp)) {
        ret = -1;
        goto out;
    }
    mpz_sqrt(tmp, tmp);

    /* out_b = tmp * t_inv % a */
    mpz_mul(out_b, tmp, t_inv);
    mpz_tdiv_r(out_b, out_b, c->a);

    if (mpz_cmp_ui(c->g, 1) > 0) {
        mpz_mul(out_a, c->a, c->g);
    } else {
        mpz_set(out_a, c->a);
    }

    if (mpz_sgn(c->b0) > 0) {
        mpz_addmul(out_b, c->a, c->b0);
    }

    if (c->b_sign) {
        mpz_neg(out_b, out_b);
    }

out:
    mpz_clears(tmp, t, t_inv, d, NULL);
    return ret;
}

static void bqfc_export(uint8_t *out_str, size_t *offset, size_t size,
        const mpz_t n)
{
    size_t bytes;

    mpz_export(&out_str[*offset], &bytes, -1, 1, 0, 0, n);
    if (bytes < size)
        memset(&out_str[*offset + bytes], 0, size - bytes);
    *offset += size;
}

enum BQFC_FLAG_BITS {
    BQFC_B_SIGN_BIT = 0,
    BQFC_T_SIGN_BIT,
    BQFC_IS_1_BIT,
    BQFC_IS_GEN_BIT,
};

enum BQFC_FLAGS {
    BQFC_B_SIGN = 1U << BQFC_B_SIGN_BIT,
    BQFC_T_SIGN = 1U << BQFC_T_SIGN_BIT,
    BQFC_IS_1 = 1U << BQFC_IS_1_BIT,
    BQFC_IS_GEN = 1U << BQFC_IS_GEN_BIT,
};

/*
 * Serialization format for compressed quadratic forms:
 *
 * Size (bytes)            Description
 * 1                       Sign bits and flags for special forms:
 *                         bit0 - b sign; bit1 - t sign;
 *                         bit2 - is identity form; bit3 - is generator form
 *
 * 1                       Size of 'g' in bytes minus 1 (g_size)
 *
 * d_bits / 16 - g_size    a' = a / g
 * d_bits / 32 - g_size    t' = t / g, where t satisfies (a*x + b*t < sqrt(a))
 * g_size + 1              g = gcd(a, t)
 * g_size + 1              b0 = b / a' (truncating division)
 *
 * Notes: 'd_bits' is the bit length of the discriminant, which is rounded up
 * to the next multiple of 32. Serialization of special forms (identity or
 * generator) takes only 1 byte.
 */
int bqfc_serialize_only(uint8_t *out_str, const struct qfb_c *c, size_t d_bits)
{
    size_t offset, bytes, size, g_size;

    d_bits = (d_bits + 31) & ~(size_t)31;

    out_str[0] = (uint8_t)c->b_sign << BQFC_B_SIGN_BIT;
    out_str[0] |= (mpz_sgn(c->t) < 0 ? 1 : 0) << BQFC_T_SIGN_BIT;
    g_size = (mpz_sizeinbase(c->g, 2) + 7) / 8 - 1;
    out_str[1] = g_size;
    offset = 2;

    bqfc_export(out_str, &offset, d_bits / 16 - g_size, c->a);
    bqfc_export(out_str, &offset, d_bits / 32 - g_size, c->t);

    bqfc_export(out_str, &offset, g_size + 1, c->g);
    bqfc_export(out_str, &offset, g_size + 1, c->b0);

    return 0;
}

int bqfc_deserialize_only(struct qfb_c *out_c, const uint8_t *str, size_t d_bits)
{
    size_t offset, bytes, g_size;

    d_bits = (d_bits + 31) & ~(size_t)31;

    g_size = str[1];
    if (g_size >= d_bits / 32)
        return -1;

    offset = 2;

    bytes = d_bits / 16 - g_size;
    mpz_import(out_c->a, bytes, -1, 1, 0, 0, &str[offset]);
    offset += bytes;

    bytes = d_bits / 32 - g_size;
    mpz_import(out_c->t, bytes, -1, 1, 0, 0, &str[offset]);
    offset += bytes;

    bytes = g_size + 1;
    mpz_import(out_c->g, bytes, -1, 1, 0, 0, &str[offset]);
    offset += bytes;

    mpz_import(out_c->b0, bytes, -1, 1, 0, 0, &str[offset]);

    out_c->b_sign = str[0] & BQFC_B_SIGN;
    if (str[0] & BQFC_T_SIGN) {
        mpz_neg(out_c->t, out_c->t);
    }

    return 0;
}

int bqfc_get_compr_size(size_t d_bits)
{
    return (d_bits + 31) / 32 * 3 + 4;
}

int bqfc_serialize(uint8_t *out_str, mpz_t a, mpz_t b, size_t d_bits)
{
    struct qfb_c f_c;
    int ret;
    int valid_size = bqfc_get_compr_size(d_bits);

    if (!mpz_cmp_ui(b, 1) && mpz_cmp_ui(a, 2) <= 0) {
        out_str[0] = !mpz_cmp_ui(a, 2) ? BQFC_IS_GEN : BQFC_IS_1;
        memset(&out_str[1], 0, BQFC_FORM_SIZE - 1);
        return 0;
    }

    mpz_inits(f_c.a, f_c.t, f_c.g, f_c.b0, NULL);
    ret = bqfc_compr(&f_c, a, b);
    if (ret)
        goto out;

    ret = bqfc_serialize_only(out_str, &f_c, d_bits);
    if (valid_size != BQFC_FORM_SIZE)
        memset(&out_str[valid_size], 0, BQFC_FORM_SIZE - valid_size);
out:
    mpz_clears(f_c.a, f_c.t, f_c.g, f_c.b0, NULL);
    return ret;
}

static int bqfc_verify_canon(mpz_t a, mpz_t b, const uint8_t *str, size_t d_bits)
{
    uint8_t canon_str[BQFC_FORM_SIZE];
    int ret = bqfc_serialize(canon_str, a, b, d_bits);

    if (ret)
        return ret;

    return memcmp(canon_str, str, BQFC_FORM_SIZE);
}

int bqfc_deserialize(mpz_t out_a, mpz_t out_b, const mpz_t D, const uint8_t *str, size_t size, size_t d_bits)
{
    struct qfb_c f_c;
    int ret;

    if (size != BQFC_FORM_SIZE)
        return -1;

    /* "Identity" (1, 1) and "generator" (2, 1) forms are serialized with a
     * special flag set in the first byte. */
    if (str[0] & (BQFC_IS_1 | BQFC_IS_GEN)) {
        mpz_set_ui(out_a, str[0] & BQFC_IS_GEN ? 2 : 1);
        mpz_set_ui(out_b, 1);
        return 0;
    }

    mpz_inits(f_c.a, f_c.t, f_c.g, f_c.b0, NULL);
    ret = bqfc_deserialize_only(&f_c, str, d_bits);
    if (ret)
        goto out;

    ret = bqfc_decompr(out_a, out_b, D, &f_c);
    if (ret)
        goto out;

    ret = bqfc_verify_canon(out_a, out_b, str, d_bits);
out:
    mpz_clears(f_c.a, f_c.t, f_c.g, f_c.b0, NULL);
    return ret;
}
