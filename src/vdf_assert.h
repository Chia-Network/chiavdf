#ifndef VDF_ASSERT_H
#define VDF_ASSERT_H

#include <string>
#include <stdexcept>

static void vdf_assert_fail(const char *file, int line)
{
    throw std::runtime_error("Assertion failure at " + std::string(file) +
            ": " + std::to_string(line));
}

#define VDF_ASSERT(expr) do { \
    if (!(expr)) { \
        vdf_assert_fail(__FILE__, __LINE__); \
    } \
} while (0)

#endif // VDF_ASSERT_H
