#include <gmp.h>

int main(void) {
    mpz_t left;
    mpz_t right;
    mpz_t product;

    mpz_inits(left, right, product, NULL);
    if (mpz_set_str(left, "123456789012345678901234567890", 10) != 0 ||
        mpz_set_str(right, "987654321098765432109876543210", 10) != 0) {
        mpz_clears(left, right, product, NULL);
        return 1;
    }

    mpz_mul(product, left, right);
    const int result = mpz_sizeinbase(product, 10) == 60 ? 0 : 2;
    mpz_clears(left, right, product, NULL);
    return result;
}
