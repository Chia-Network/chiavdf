#include "vdf.h"
#include "create_discriminant.h"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace {
void check_nucomp_scratch_reuse(const std::vector<integer>& discriminants) {
    set_rounding_mode();
    PulmarkReducer reducer;
    for (const auto& d : discriminants) {
        integer l = root(-d, 4);
        form f = form::generator(d);
        for (int step = 0; step < 24; ++step) {
            // NUDUPL provides an independent squaring implementation.
            form expected = f;
            nudupl_form(expected, expected, d, l);
            reducer.reduce(expected);

            form actual = f;
            nucomp_form(actual, actual, actual, d, l);
            reducer.reduce(actual);
            EXPECT_TRUE(actual.a == expected.a && actual.b == expected.b && actual.c == expected.c);

            form inverse = f;
            inverse.b = -inverse.b;
            form product;
            nucomp_form(product, f, inverse, d, l);
            reducer.reduce(product);
            form identity = form::identity(d);
            reducer.reduce(identity);
            // At the reduced-form boundary a=|b|=1, either sign of b
            // represents the principal class; Pulmark can retain b=-1.
            EXPECT_TRUE(product.a == identity.a &&
                        (product.b == identity.b || product.b == -identity.b) &&
                        product.c == identity.c);
            for (bool reverse : {false, true}) {
                form unchanged;
                nucomp_form(unchanged, reverse ? f : identity,
                            reverse ? identity : f, d, l);
                reducer.reduce(unchanged);
                EXPECT_TRUE(unchanged.a == f.a && unchanged.b == f.b && unchanged.c == f.c);
            }
            f = expected;
        }
    }
}
}

TEST(NucompScratchRegressionTest, ReusesScratchAcrossSizesAliasesAndThreads) {
    std::vector<integer> discriminants;
    std::vector<uint8_t> seed = {0, 0, 1, 2, 3, 3, 4, 4};
    for (int bits : {128, 256, 1024, 256, 128, 1024}) {
        discriminants.push_back(CreateDiscriminant(seed, bits));
    }
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back(check_nucomp_scratch_reuse, std::cref(discriminants));
    }
    for (auto& worker : workers) worker.join();
}
