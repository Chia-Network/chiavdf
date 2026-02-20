#ifndef CHECKED_CAST_H
#define CHECKED_CAST_H

#include <limits>
#include <stdexcept>
#include <type_traits>

template <typename To, typename From>
inline To checked_cast(From value) {
    static_assert(std::is_integral_v<To>, "checked_cast requires integral destination type");
    static_assert(std::is_integral_v<From>, "checked_cast requires integral source type");

    if constexpr (std::is_signed_v<From> == std::is_signed_v<To>) {
        if constexpr (sizeof(To) < sizeof(From)) {
            if (value < static_cast<From>(std::numeric_limits<To>::min()) ||
                value > static_cast<From>(std::numeric_limits<To>::max())) {
                throw std::overflow_error("checked_cast: value out of range");
            }
        }
    } else if constexpr (std::is_signed_v<From>) {
        if (value < 0 ||
            static_cast<std::make_unsigned_t<From>>(value) > std::numeric_limits<To>::max()) {
            throw std::overflow_error("checked_cast: value out of range");
        }
    } else {
        using to_unsigned_t = std::make_unsigned_t<To>;
        using compare_t = std::conditional_t<(sizeof(From) > sizeof(to_unsigned_t)), From, to_unsigned_t>;
        if (static_cast<compare_t>(value) > static_cast<compare_t>(std::numeric_limits<To>::max())) {
            throw std::overflow_error("checked_cast: value out of range");
        }
    }

    return static_cast<To>(value);
}

#endif
