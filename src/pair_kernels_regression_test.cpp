#include "vdf.h"
#include "create_discriminant.h"
#include "prover.hpp"
#include "pair_kernels.h"

#include <gtest/gtest.h>

#if defined(CHIAVDF_PAIR_KERNELS)

#include <vector>

// The paired kernels must produce exactly what the reference kernels produce:
// the same partial-GCD state, the same gcdext cofactor, the same unreduced
// NUDUPL/NUCOMP forms and the same proofs.
namespace {

struct PairFixture {
    integer D, L;
    std::vector<form> chain;  // consecutive reduced squarings
};

PairFixture MakeChain(int bits, size_t n)
{
    set_rounding_mode();
    std::vector<uint8_t> seed = {0x70, 0x61, 0x69, 0x72, uint8_t(bits)};
    PairFixture f;
    f.D = CreateDiscriminant(seed, bits);
    f.L = root(-f.D, 4);
    form x = form::generator(f.D);
    PulmarkReducer reducer;
    for (int i = 0; i < 200; i++) {
        nudupl_form(x, x, f.D, f.L);
        reducer.reduce(x);
    }
    for (size_t i = 0; i < n; i++) {
        f.chain.push_back(x);
        nudupl_form(x, x, f.D, f.L);
        reducer.reduce(x);
    }
    return f;
}

bool SameForm(const form& a, const form& b)
{
    return mpz_cmp(a.a.impl, b.a.impl) == 0 && mpz_cmp(a.b.impl, b.b.impl) == 0 &&
           mpz_cmp(a.c.impl, b.c.impl) == 0;
}

struct PartialResult {
    integer co2, co1, r2, r1;
};

PartialResult RunPartial(void (*fn)(mpz_ptr, mpz_ptr, mpz_ptr, mpz_ptr, mpz_srcptr),
                         const integer& r2, const integer& r1, const integer& limit)
{
    PartialResult out;
    out.r2 = r2;
    out.r1 = r1;
    fn(out.co2.impl, out.co1.impl, out.r2.impl, out.r1.impl, limit.impl);
    return out;
}

void Reference(mpz_ptr co2, mpz_ptr co1, mpz_ptr r2, mpz_ptr r1, mpz_srcptr limit)
{
    mpz_xgcd_partial(co2, co1, r2, r1, limit);
}

void ExpectSamePartial(const PartialResult& a, const PartialResult& b)
{
    EXPECT_TRUE(a.co2 == b.co2 && a.co1 == b.co1 && a.r2 == b.r2 && a.r1 == b.r1);
}

// The operands NUDUPL hands to the partial GCD for form f.
void NudupPartialOperands(const form& f, integer* r2, integer* r1)
{
    integer s, v, b, k, a = f.a;
    mpz_abs(b.impl, f.b.impl);
    mpz_gcdext(s.impl, v.impl, NULL, b.impl, a.impl);
    if (mpz_sgn(f.b.impl) < 0) mpz_neg(v.impl, v.impl);
    mpz_mul(k.impl, v.impl, f.c.impl);
    mpz_neg(k.impl, k.impl);
    if (mpz_cmp_ui(s.impl, 1)) mpz_fdiv_q(a.impl, a.impl, s.impl);
    mpz_fdiv_r(k.impl, k.impl, a.impl);
    *r2 = a;
    *r1 = k;
}

void ExpectGcdext(const integer& g, const integer& v, const integer& b, const integer& a)
{
    integer eg, ev;
    mpz_gcdext(eg.impl, ev.impl, NULL, b.impl, a.impl);
    EXPECT_TRUE(g == eg && v == ev);
}

}  // namespace

TEST(PairKernelsRegressionTest, FixedPartialGcdMatchesReference)
{
    gmp_randstate_t rng;
    gmp_randinit_mt(rng);
    gmp_randseed_ui(rng, 20261027);
    for (int n = 0; n < 6000; n++) {
        unsigned bits = 2 + n % 700;
        integer r2, r1, limit;
        mpz_urandomb(r2.impl, rng, bits);
        mpz_urandomb(r1.impl, rng, bits);
        mpz_urandomb(limit.impl, rng, bits / 2 + n % 64);
        if (r2 < r1) std::swap(r2, r1);
        if (n % 7 == 0) r1 = integer(0);
        if (n % 11 == 0) r1 = r2;
        if (n % 13 == 0) limit = integer(0);
        if (n % 19 == 0) limit = r1;
        SCOPED_TRACE(n);
        ExpectSamePartial(RunPartial(Reference, r2, r1, limit),
                          RunPartial(mpz_xgcd_partial_fixed, r2, r1, limit));
    }
    // Powers of two and their neighbours, across limb boundaries.
    for (unsigned bits = 60; bits <= 700; bits += 4) {
        integer p;
        mpz_setbit(p.impl, bits);
        for (int delta : {-1, 0, 1}) {
            integer r2 = p + integer(delta), r1 = (p >> 1) + integer(delta), limit = p >> 2;
            ExpectSamePartial(RunPartial(Reference, r2, r1, limit),
                              RunPartial(mpz_xgcd_partial_fixed, r2, r1, limit));
        }
    }
    gmp_randclear(rng);
}

// With L == 0 the fixed GCD finishes in exact single-word Euclid once both
// values fit in a word. The end state of a full GCD is unique, so it must
// still match the reference, from one-word inputs and from long ones.
TEST(PairKernelsRegressionTest, FullGcdExactTailMatchesReference)
{
    gmp_randstate_t rng;
    gmp_randinit_mt(rng);
    gmp_randseed_ui(rng, 61);
    integer zero(0);
    for (int n = 0; n < 4000; n++) {
        unsigned bits = n < 2000 ? 1 + n % 63 : 64 + n % 600;
        integer r2, r1;
        mpz_urandomb(r2.impl, rng, bits);
        mpz_urandomb(r1.impl, rng, bits);
        if (r2 < r1) std::swap(r2, r1);
        if (n % 9 == 0) {
            // Shared factors, so the GCD is not 1.
            integer g;
            mpz_urandomb(g.impl, rng, 20);
            mpz_add_ui(g.impl, g.impl, 2);
            r2 = r2 * g;
            r1 = r1 * g;
        }
        SCOPED_TRACE(n);
        ExpectSamePartial(RunPartial(Reference, r2, r1, zero), RunPartial(mpz_xgcd_partial_fixed, r2, r1, zero));
    }
    gmp_randclear(rng);
}

TEST(PairKernelsRegressionTest, PairedGcdsMatchReference)
{
    PairFixture f = MakeChain(1024, 512);
    // Real NUDUPL operands, two independent problems per call.
    for (size_t i = 0; i + 1 < f.chain.size(); i += 2) {
        integer r2[2], r1[2];
        NudupPartialOperands(f.chain[i], &r2[0], &r1[0]);
        NudupPartialOperands(f.chain[i + 1], &r2[1], &r1[1]);
        PartialResult got[2];
        for (int j = 0; j < 2; j++) {
            got[j].r2 = r2[j];
            got[j].r1 = r1[j];
        }
        mpz_xgcd_partial_pair(got[0].co2.impl, got[0].co1.impl, got[0].r2.impl, got[0].r1.impl, f.L.impl,
                              got[1].co2.impl, got[1].co1.impl, got[1].r2.impl, got[1].r1.impl, f.L.impl);
        for (int j = 0; j < 2; j++)
            ExpectSamePartial(RunPartial(Reference, r2[j], r1[j], f.L), got[j]);
    }

    // gcdext on chain values and on unreduced bucket products, where
    // gcd(a2 mod a1, a1) != 1 is common: the cofactor must be mpz_gcdext's.
    std::vector<form> buckets(8, form::identity(f.D));
    int non_coprime = 0;
    for (size_t i = 0; i + 1 < f.chain.size(); i += 2) {
        nucomp_form(buckets[i % 8], buckets[i % 8], f.chain[i], f.D, f.L);
        nucomp_form(buckets[(i + 3) % 8], buckets[(i + 3) % 8], f.chain[i + 1], f.D, f.L);
        const form* pairs[2][2] = {{&f.chain[i], &f.chain[i + 1]}, {&buckets[i % 8], &buckets[(i + 3) % 8]}};
        for (auto& p : pairs) {
            integer b[2], a[2], g[2], v[2];
            for (int j = 0; j < 2; j++) {
                const form& x = *p[j];
                const form& y = f.chain[(i + 7 * j + 5) % f.chain.size()];
                // NUCOMP's first gcdext: (a2 mod a1, a1).
                bool swap = mpz_cmp(x.a.impl, y.a.impl) > 0;
                a[j] = swap ? y.a : x.a;
                mpz_fdiv_r(b[j].impl, (swap ? x.a : y.a).impl, a[j].impl);
            }
            mpz_gcdinv_pair(g[0].impl, v[0].impl, b[0].impl, a[0].impl, g[1].impl, v[1].impl, b[1].impl, a[1].impl);
            for (int j = 0; j < 2; j++) {
                ExpectGcdext(g[j], v[j], b[j], a[j]);
                non_coprime += mpz_cmp_ui(g[j].impl, 1) != 0;
            }
        }
    }
    EXPECT_GT(non_coprime, 0);
}

// Inputs outside the Euclid sequence's usual shape (b >= a, b == a, b == 0),
// and the guard that replaces a cofactor outside |v| < a/(2g).
TEST(PairKernelsRegressionTest, GcdinvEdgesAndFallbackMatchGmp)
{
    PairFixture f = MakeChain(1024, 4);
    const integer a = f.chain[0].a;
    const integer bs[] = {a + f.chain[1].a, a, integer(0), f.chain[2].a * integer(3)};
    for (const integer& b : bs) {
        integer g[2], v[2];
        mpz_gcdinv_pair(g[0].impl, v[0].impl, b.impl, a.impl, g[1].impl, v[1].impl, b.impl, a.impl);
        ExpectGcdext(g[0], v[0], b, a);
        ExpectGcdext(g[1], v[1], b, a);
    }

    // A valid inverse in the wrong residue class representative (v + a/g)
    // breaks the bound, and must be replaced by mpz_gcdext's.
    integer b = f.chain[3].a * integer(6), m;
    integer a6 = a * integer(6);
    integer g, v;
    mpz_gcdext(g.impl, v.impl, NULL, b.impl, a6.impl);
    ASSERT_TRUE(g != integer(1));
    mpz_divexact(m.impl, a6.impl, g.impl);
    integer shifted = v + m;
    mpz_gcdinv_settle(g.impl, shifted.impl, b.impl, a6.impl);
    ExpectGcdext(g, shifted, b, a6);
    // An in-bound cofactor is kept as it is.
    integer kept = v;
    mpz_gcdinv_settle(g.impl, kept.impl, b.impl, a6.impl);
    EXPECT_TRUE(kept == v);
}

TEST(PairKernelsRegressionTest, PairedNudupMatchesReference)
{
    for (int bits : {1024, 256}) {
        PairFixture f = MakeChain(bits, 2);
        for (bool pair_full_gcd : {false, true}) {
            form x = f.chain[0], y = f.chain[1], ex = x, ey = y;
            PulmarkReducer reducer;
            for (int step = 0; step < 1500; step++) {
                nudupl_form_pair(x, y, f.D, f.L, pair_full_gcd);
                nudupl_form(ex, ex, f.D, f.L);
                nudupl_form(ey, ey, f.D, f.L);
                // Unreduced outputs match before the reducer runs.
                ASSERT_TRUE(SameForm(x, ex) && SameForm(y, ey)) << "bits " << bits << " step " << step;
                reducer.reduce(x);
                reducer.reduce(y);
                reducer.reduce(ex);
                reducer.reduce(ey);
            }
        }
    }
}

// The identity has a < L, so its lane takes the branch without the partial
// GCD while the other lane runs it; either lane may be the short one.
TEST(PairKernelsRegressionTest, PairedNudupMixedBranchesMatchReference)
{
    PairFixture f = MakeChain(1024, 2);
    for (bool pair_full_gcd : {false, true}) {
        for (int short_lane = 0; short_lane < 2; short_lane++) {
            form lanes[2] = {f.chain[0], f.chain[1]};
            lanes[short_lane] = form::identity(f.D);
            form expected[2] = {lanes[0], lanes[1]};
            nudupl_form_pair(lanes[0], lanes[1], f.D, f.L, pair_full_gcd);
            for (int j = 0; j < 2; j++) {
                nudupl_form(expected[j], expected[j], f.D, f.L);
                EXPECT_TRUE(SameForm(lanes[j], expected[j])) << "short lane " << short_lane;
            }
        }
    }
}

TEST(PairKernelsRegressionTest, PairedNucompMatchesReference)
{
    PairFixture f = MakeChain(1024, 300);
    // Buckets start at the identity, so early products take the branch
    // without a GCD while their partner runs both.
    std::vector<form> expected(8, form::identity(f.D)), actual(8, form::identity(f.D));
    for (int rep = 0; rep < 4; rep++) {
        // Odd passes put the grid value first, so qfb_nucomp's operand
        // swap is exercised; each output aliases one of its own inputs.
        bool grid_first = rep % 2;
        for (size_t i = 0; i + 1 < f.chain.size(); i += 2) {
            size_t bx = (i * 5 + rep) % 8, by = (bx + 1 + i % 7) % 8;
            const form& gx = f.chain[i];
            const form& gy = f.chain[i + 1];
            if (grid_first) {
                nucomp_form(expected[bx], gx, expected[bx], f.D, f.L);
                nucomp_form(expected[by], gy, expected[by], f.D, f.L);
                nucomp_form_pair(actual[bx], gx, actual[bx], actual[by], gy, actual[by], f.D, f.L);
            } else {
                nucomp_form(expected[bx], expected[bx], gx, f.D, f.L);
                nucomp_form(expected[by], expected[by], gy, f.D, f.L);
                nucomp_form_pair(actual[bx], actual[bx], gx, actual[by], actual[by], gy, f.D, f.L);
            }
            ASSERT_TRUE(SameForm(expected[bx], actual[bx]) && SameForm(expected[by], actual[by]))
                << "rep " << rep << " i " << i;
        }
    }
}

namespace {
class FormsProver : public ParallelProver {
public:
    FormsProver(Segment segm, integer D, const std::vector<form>& forms, uint32_t kk, uint32_t ll, bool paired)
        : ParallelProver(segm, D), forms(forms), paired(paired)
    {
        k = kk;
        l = ll;
    }
    form GetForm(uint64_t iteration) override { return forms.at(iteration); }
    void start() override { GenerateProof(); }
    void stop() override {}
    bool PerformExtraStep() override { return true; }
    void OnFinish() override { is_finished = true; }

protected:
    void ComposeTwo(form& a, const form& b, const form& c, form& d, const form& e, const form& f) override
    {
        if (paired)
            nucomp_form_pair(a, b, c, d, e, f, D, L);
        else
            ParallelProver::ComposeTwo(a, b, c, d, e, f);
    }

private:
    const std::vector<form>& forms;
    bool paired;
};
}  // namespace

TEST(PairKernelsRegressionTest, PairedComposeTwoMatchesParallelProver)
{
    PairFixture f = MakeChain(1024, 1);
    PulmarkReducer reducer;
    form x = f.chain[0];
    for (uint32_t k : {1u, 2u, 4u, 5u}) {
        for (uint32_t l : {3u, 8u}) {
            for (uint64_t iterations : {uint64_t(k * l), uint64_t(37 * k * l + 5)}) {
                uint64_t rows = iterations / (k * l) + (iterations % (k * l) != 0);
                std::vector<form> forms(rows);
                forms[0] = x;
                form y = x;
                for (uint64_t i = 1; i <= iterations; i++) {
                    nudupl_form(y, y, f.D, f.L);
                    reducer.reduce(y);
                    if (i % (k * l) == 0 && i / (k * l) < rows) forms[i / (k * l)] = y;
                }
                Segment seg(0, iterations, x, y);
                FormsProver plain(seg, f.D, forms, k, l, false);
                FormsProver paired(seg, f.D, forms, k, l, true);
                plain.start();
                paired.start();
                ASSERT_TRUE(plain.IsFinished() && paired.IsFinished());
                form expected = plain.GetProof(), actual = paired.GetProof();
                EXPECT_TRUE(expected == actual) << "k " << k << " l " << l << " iterations " << iterations;
                bool valid = false;
                VerifyWesolowskiProof(f.D, x, y, actual, iterations, valid);
                EXPECT_TRUE(valid);
            }
        }
    }
}

#endif  // CHIAVDF_PAIR_KERNELS
