#include "vdf.h"
#include "create_discriminant.h"
#include "reference_golden.h"

#include <gtest/gtest.h>

// The class-group reference (nudupl_form, nucomp_form, mpz_xgcd_partial) must
// give exactly the results it gave before it was split into the phases of
// nucomp_phases.h and the Lehmer step of xgcd_lehmer_step.h. The digests are
// from main at 9947bfa, identical on macOS arm64 (clang) and Linux x86_64
// (GCC 13), and cover every unreduced intermediate, not only reduced forms.
TEST(ReferenceGoldenRegressionTest, MatchesDigestsFromBeforeThePhaseSplit)
{
    set_rounding_mode();
    const auto digests = reference_golden::digests();
    EXPECT_EQ(digests[0], "52f9ccb2e539a36945a396b1b7ca767ad54753383e234da5ccc8677ef71669de")
        << "NUDUPL chain, unreduced outputs";
    EXPECT_EQ(digests[1], "fbc111f5f8e6e8e3437c473ad23e47ca86e83faa6e1dbece04eb476f3b555017")
        << "NUDUPL chain, reduced";
    EXPECT_EQ(digests[2], "24f233f8267ca9d5b54bd02d2a1081001b747f4768b4054f998305844ec930f0")
        << "NUCOMP bucket products, unreduced";
    EXPECT_EQ(digests[3], "2203fd964d1a0a972bbcc8def30ad419f1ce46a11b3782c7463f91cb21e26106")
        << "mpz_xgcd_partial corpus";
}
