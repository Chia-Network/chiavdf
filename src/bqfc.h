#ifndef BQFC_H
#define BQFC_H

#include <gmp.h>

#include <stdbool.h>
#include <stdint.h>


struct qfb_c {
    mpz_t a;
    mpz_t t;
    mpz_t g;
    mpz_t b0;
    bool b_sign;
};

#define BQFC_MAX_D_BITS 1024
/* Force all forms to have the same size (100 bytes). */
#define BQFC_FORM_SIZE ((BQFC_MAX_D_BITS + 31) / 32 * 3 + 4)

int bqfc_compr(struct qfb_c *out_c, mpz_t a, mpz_t b);

int bqfc_decompr(mpz_t out_a, mpz_t out_b, const mpz_t D, const struct qfb_c *c);

int bqfc_serialize_only(uint8_t *out_str, const struct qfb_c *c, size_t d_bits);
int bqfc_deserialize_only(struct qfb_c *out_c, const uint8_t *str, size_t d_bits);

int bqfc_serialize(uint8_t *out_str, mpz_t a, mpz_t b, size_t d_bits);
int bqfc_deserialize(mpz_t out_a, mpz_t out_b, const mpz_t D, const uint8_t *str, size_t d_bits);

#endif // BQFC_H
