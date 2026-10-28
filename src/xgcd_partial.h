#ifndef XGCD_PARTIAL_H
#define XGCD_PARTIAL_H

#include <gmp.h>

// Partial extended GCD for NUDUPL/NUCOMP: Euclid on (r2, r1) until r1 <= L,
// with cofactors co2 and co1 (co2*r1 - co1*r2 == +/- r2_orig).
//
// Defined in xgcd_partial.c, which nucomp.h includes as source. A program
// that calls it from another translation unit (pair_kernels.cpp) must also
// compile nucomp.h into one of its translation units, as every user of the
// class-group arithmetic does.
void mpz_xgcd_partial(mpz_t co2, mpz_t co1, mpz_t r2, mpz_t r1, const mpz_t L);

#endif // XGCD_PARTIAL_H
