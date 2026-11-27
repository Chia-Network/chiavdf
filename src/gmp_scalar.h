#ifndef CHIAVDF_GMP_SCALAR_H
#define CHIAVDF_GMP_SCALAR_H

#include <gmp.h>
#include <limits>
#include <type_traits>

namespace chiavdf {

namespace detail {
// Read the library's actual scalar types: MPIR can differ from GMP on Windows.
template<class Function> struct gmp_set_scalar;
template<class Result, class Scalar>
struct gmp_set_scalar<Result (*)(mpz_ptr, Scalar)> {
    using type = Scalar;
};
}

using gmp_signed_scalar = typename detail::gmp_set_scalar<decltype(&mpz_set_si)>::type;
using gmp_unsigned_scalar = typename detail::gmp_set_scalar<decltype(&mpz_set_ui)>::type;

template<class To, class From>
constexpr To checked_gmp_scalar(From value) {
    static_assert(std::is_integral_v<To> && std::is_integral_v<From>,
                  "GMP scalar arguments must be integral");
    static_assert(!std::is_signed_v<From> || std::is_signed_v<To>,
                  "GMP scalar parameter cannot represent negative source values");
    static_assert(std::numeric_limits<From>::digits <= std::numeric_limits<To>::digits,
                  "GMP scalar parameter cannot represent the source type's full range");
    return static_cast<To>(value);
}

template<class T>
inline void checked_mpz_powm_ui(mpz_ptr out, mpz_srcptr base, T exponent, mpz_srcptr modulus) {
    mpz_powm_ui(out, base, checked_gmp_scalar<gmp_unsigned_scalar>(exponent), modulus);
}

template<class T>
inline void checked_mpz_mul_2exp(mpz_ptr out, mpz_srcptr in, T bits) {
    mpz_mul_2exp(out, in, checked_gmp_scalar<mp_bitcnt_t>(bits));
}

template<class T>
inline mp_limb_t checked_mpz_getlimbn(mpz_srcptr in, T index) {
    return mpz_getlimbn(in, checked_gmp_scalar<mp_size_t>(index));
}

template<class T>
inline void checked_mpz_init2(mpz_ptr out, T bits) {
    mpz_init2(out, checked_gmp_scalar<mp_bitcnt_t>(bits));
}

template<class T>
inline void checked_mpz_setbit(mpz_ptr out, T index) {
    mpz_setbit(out, checked_gmp_scalar<mp_bitcnt_t>(index));
}

} // namespace chiavdf

#endif
