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

} // namespace chiavdf

#endif
