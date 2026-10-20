#include "verifier.h"
#include "vdf_new.h"
#include "create_discriminant.h"
#include "bit_manipulation.h"
#include "double_utility.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

// operator* in vdf_new.h is the older multiply. It does not use qfb_nucomp.
// qfb_nucomp returns an unreduced form, and operator* uses the classical reducer,
// so both results are Pulmark-reduced before the coefficients are compared.
inline form ClassicalProduct(form const& left, form const& right)
{
    form product = left * right;
    PulmarkReducer reducer;
    reducer.reduce(product);
    return product;
}

inline form ClassicalPow(form const& base, integer& D, integer const& exponent)
{
    if (mpz_sgn(exponent.impl) < 0) {
        throw std::invalid_argument("classical exponent must be non-negative");
    }
    if (!mpz_sgn(exponent.impl)) {
        return form::identity(D);
    }
    const int nbits = exponent.num_bits();
    form result = base;
    for (int index = nbits - 2; index >= 0; --index) {
        result = result * result;
        if (mpz_tstbit(exponent.impl, index)) {
            result = result * base;
        }
    }
    return result;
}

inline form FastPowFormNucompWidth1(form x, integer& D, integer num_iterations, integer& L, PulmarkReducer& reducer)
{
    return FastPowFormNucompWidth<1>(x, D, num_iterations, L, reducer);
}

inline form FastPowFormNucompWidth4(form x, integer& D, integer num_iterations, integer& L, PulmarkReducer& reducer)
{
    return FastPowFormNucompWidth<4>(x, D, num_iterations, L, reducer);
}

inline bool FormsEqual(form const& left, form const& right)
{
    return left.a == right.a && left.b == right.b && left.c == right.c;
}

inline form ReducedPow(
    form (*pow)(form, integer&, integer, integer&, PulmarkReducer&),
    form const& base,
    integer& D,
    integer const& exponent,
    integer& L)
{
    PulmarkReducer reducer;
    form result = pow(base, D, exponent, L, reducer);
    reducer.reduce(result);
    return result;
}

inline bool PowPathsMatch(form const& base, integer& D, integer const& exponent, integer& L, std::string& error, char const* label)
{
    form one = ReducedPow(FastPowFormNucompWidth1, base, D, exponent, L);
    form window = ReducedPow(FastPowFormNucompWidth4, base, D, exponent, L);
    form dispatched = ReducedPow(FastPowFormNucomp, base, D, exponent, L);
    form classical = ClassicalPow(base, D, exponent);
    PulmarkReducer reducer;
    reducer.reduce(classical);
    if (!FormsEqual(one, classical) || !FormsEqual(window, classical) || !FormsEqual(dispatched, classical)) {
        error = std::string("pow mismatch at ") + label;
        return false;
    }
    return true;
}

inline std::vector<uint8_t> HexToBytes(char const* hex)
{
    const size_t len = std::strlen(hex);
    std::vector<uint8_t> result;
    result.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        const int hi = hex[i] >= 'a' ? (hex[i] - 'a' + 10) : (hex[i] - '0');
        const int lo = hex[i + 1] >= 'a' ? (hex[i + 1] - 'a' + 10) : (hex[i + 1] - '0');
        result.push_back(static_cast<uint8_t>(hi * 16 + lo));
    }
    return result;
}

inline bool AliasMatchesClassical(form const& left_in, form const& right_in, integer& D, integer& L, std::string& error)
{
    PulmarkReducer reducer;
    form expected = ClassicalProduct(left_in, right_in);

    form to_left = left_in;
    nucomp_form(to_left, to_left, right_in, D, L);
    reducer.reduce(to_left);
    form to_right = right_in;
    nucomp_form(to_right, left_in, to_right, D, L);
    reducer.reduce(to_right);

    form swapped_expected = ClassicalProduct(right_in, left_in);
    form swapped;
    nucomp_form(swapped, right_in, left_in, D, L);
    reducer.reduce(swapped);

    if (!FormsEqual(expected, to_left) || !FormsEqual(expected, to_right) || !FormsEqual(swapped_expected, swapped)) {
        error = "aliased nucomp disagreed with the older multiply";
        return false;
    }
    return true;
}

inline void InitQfb(qfb& dst, form const& src)
{
    mpz_init_set(dst.a, src.a.impl);
    mpz_init_set(dst.b, src.b.impl);
    mpz_init_set(dst.c, src.c.impl);
}

inline void ClearQfb(qfb& dst)
{
    mpz_clears(dst.a, dst.b, dst.c, nullptr);
}

inline form FormFromQfb(qfb const& src, integer const& D)
{
    form out = form::identity(D);
    mpz_set(out.a.impl, src.a);
    mpz_set(out.b.impl, src.b);
    mpz_set(out.c.impl, src.c);
    return out;
}

// qfb_nucomp writes its output only at the end. This calls it with the output
// occupying the same object as either input, and with both inputs the same
// object, and compares the Pulmark-reduced coefficients to operator*.
inline bool QfbAliasMatchesClassical(form const& left_in, form const& right_in, integer& D, integer& L, std::string& error)
{
    form expected = ClassicalProduct(left_in, right_in);
    form expected_swap = ClassicalProduct(right_in, left_in);
    form expected_square = ClassicalProduct(left_in, left_in);
    PulmarkReducer reducer;

    qfb left;
    qfb right;
    bool ok = true;

    InitQfb(left, left_in);
    InitQfb(right, right_in);
    qfb_nucomp(&left, &left, &right, D.impl, L.impl);
    form got_left = FormFromQfb(left, D);
    reducer.reduce(got_left);
    ok = ok && FormsEqual(got_left, expected);
    ClearQfb(left);
    ClearQfb(right);

    InitQfb(left, left_in);
    InitQfb(right, right_in);
    qfb_nucomp(&right, &left, &right, D.impl, L.impl);
    form got_right = FormFromQfb(right, D);
    reducer.reduce(got_right);
    ok = ok && FormsEqual(got_right, expected);
    ClearQfb(left);
    ClearQfb(right);

    InitQfb(left, right_in);
    InitQfb(right, left_in);
    qfb_nucomp(&left, &left, &right, D.impl, L.impl);
    form got_swap = FormFromQfb(left, D);
    reducer.reduce(got_swap);
    ok = ok && FormsEqual(got_swap, expected_swap);
    ClearQfb(left);
    ClearQfb(right);

    InitQfb(left, left_in);
    InitQfb(right, left_in);
    qfb_nucomp(&left, &left, &left, D.impl, L.impl);
    form got_square = FormFromQfb(left, D);
    reducer.reduce(got_square);
    ok = ok && FormsEqual(got_square, expected_square);
    ClearQfb(left);
    ClearQfb(right);

    if (!ok) {
        error = "qfb output aliased to an input disagreed with the older multiply";
        return false;
    }
    return true;
}

inline bool SmallerThanL(form const& value, integer const& L)
{
    return mpz_cmp(value.a.impl, L.impl) < 0;
}

inline bool RunFastPowNucompChecks(std::string& error)
{
    set_rounding_mode();
    std::vector<uint8_t> seed = {0, 0, 1, 2, 3, 3, 4, 4};
    integer D = CreateDiscriminant(seed, 1024);
    integer L = root(-D, 4);
    form base = form::generator(D);
    PulmarkReducer reducer;
    reducer.reduce(base);

    form zero_pow = ReducedPow(FastPowFormNucomp, base, D, integer(0), L);
    form identity = form::identity(D);
    reducer.reduce(identity);
    if (!FormsEqual(zero_pow, identity)) {
        error = "exponent 0 did not return the identity for a non-identity base";
        return false;
    }
    if (integer(0).num_bits() != 1) {
        error = "GMP reports a bit length other than 1 for zero";
        return false;
    }
    form reduced_base = base;
    reducer.reduce(reduced_base);
    if (!FormsEqual(ReducedPow(FastPowFormNucomp, base, D, integer(1), L), reduced_base)) {
        error = "exponent 1 did not return the reduced base";
        return false;
    }
    if (!SmallerThanL(base, L) || !SmallerThanL(identity, L)) {
        error = "small multiply fixture is already past L, so it misses the simple branch";
        return false;
    }

    for (int value = 0; value <= 256; ++value) {
        if (!PowPathsMatch(base, D, integer(value), L, error, "0..256")) {
            error += " value " + std::to_string(value);
            return false;
        }
    }
    for (int shift = 0; shift <= 16; ++shift) {
        integer power = integer(1) << shift;
        integer all_ones = power - integer(1);
        if (!PowPathsMatch(base, D, power, L, error, "power of two")) {
            return false;
        }
        if (shift > 0 && !PowPathsMatch(base, D, all_ones, L, error, "one less than a power of two")) {
            return false;
        }
    }

    // 63 bits stays on the one-bit walk. 64 bits is the first window exponent.
    integer bits63 = integer(1) << 62;
    integer bits64 = integer(1) << 63;
    if (bits63.num_bits() != 63 || bits64.num_bits() != 64) {
        error = "bit-length cutoff fixtures are not 63 and 64 bits";
        return false;
    }
    if (!PowPathsMatch(base, D, bits63, L, error, "63-bit")) {
        return false;
    }
    if (!PowPathsMatch(base, D, bits64, L, error, "64-bit")) {
        return false;
    }
    // Identity to a short exponent and to the first window exponent stays the identity.
    for (integer const& id_exp : {integer(17), bits63, bits64}) {
        form id_pow = ReducedPow(FastPowFormNucomp, identity, D, id_exp, L);
        if (!FormsEqual(id_pow, identity) || !PowPathsMatch(identity, D, id_exp, L, error, "identity base")) {
            if (error.empty()) {
                error = "identity base was not stable";
            }
            return false;
        }
    }
    // First window chunk has width 1, 2, 3, and 4. Powers of two keep later digits at 0.
    // All-ones keeps every digit at 15.
    for (int bits = 65; bits <= 68; ++bits) {
        integer power = integer(1) << (bits - 1);
        integer all_ones = (integer(1) << bits) - integer(1);
        if (power.num_bits() != bits || all_ones.num_bits() != bits) {
            error = "window-width fixture has an unexpected bit length";
            return false;
        }
        if (!PowPathsMatch(base, D, power, L, error, "window width power of two") ||
            !PowPathsMatch(base, D, all_ones, L, error, "window width all ones")) {
            error += " bits " + std::to_string(bits);
            return false;
        }
    }

    // Top bit set, low nibble 0, and not a pure power of two.
    integer header_shape = (integer(1) << 263) + (integer(1) << 8);
    if (header_shape.num_bits() != 264 || header_shape.get_bit(0) || header_shape.get_bit(1) ||
        header_shape.get_bit(2) || header_shape.get_bit(3) || !header_shape.get_bit(263)) {
        error = "264-bit fixture does not have the top bit set and the low nibble clear";
        return false;
    }
    if (!PowPathsMatch(base, D, header_shape, L, error, "264-bit")) {
        return false;
    }
    integer header_top = integer(1) << 263;
    integer header_mixed = header_top + (integer(1) << 20) + integer(15);
    if (header_top.num_bits() != 264 || header_mixed.num_bits() != 264 || header_mixed.get_bit(0) == false ||
        !header_mixed.get_bit(20) || !header_mixed.get_bit(263)) {
        error = "mixed 264-bit fixture is not top-bit, a middle bit, and a low nibble";
        return false;
    }
    if (!PowPathsMatch(base, D, header_top, L, error, "264-bit power of two") ||
        !PowPathsMatch(base, D, header_mixed, L, error, "264-bit mixed nibbles")) {
        return false;
    }

    // Every prover exponent for k=10 and k=12, the two values this tree uses.
    for (int k : {10, 12}) {
        const int k0 = k / 2;
        const int k1 = k - k0;
        if (!PowPathsMatch(base, D, integer(1) << k, L, error, "prover 1<<k")) {
            return false;
        }
        for (int b0 = 0; b0 < (1 << k0); ++b0) {
            if (!PowPathsMatch(base, D, integer(b0), L, error, "prover b0")) {
                return false;
            }
        }
        for (int b1 = 0; b1 < (1 << k1); ++b1) {
            if (!PowPathsMatch(base, D, integer(b1) << k0, L, error, "prover b1<<k0")) {
                return false;
            }
        }
    }

    for (integer const& negative : {integer(-1), integer(-16), -(integer(1) << 100)}) {
        for (auto* pow : {FastPowFormNucomp, FastPowFormNucompWidth1, FastPowFormNucompWidth4}) {
            bool threw = false;
            try {
                PulmarkReducer local;
                pow(base, D, negative, L, local);
            } catch (std::invalid_argument const&) {
                threw = true;
            }
            if (!threw) {
                error = "negative exponent did not throw";
                return false;
            }
        }
    }

    form squared = base;
    nudupl_form(squared, squared, D, L);
    reducer.reduce(squared);
    if (!AliasMatchesClassical(base, squared, D, L, error) || !AliasMatchesClassical(identity, base, D, L, error) ||
        !QfbAliasMatchesClassical(base, squared, D, L, error)) {
        return false;
    }

    // Scratch must survive a run of squares. nudupl is the independent square.
    form walked = base;
    for (int step = 0; step < 24; ++step) {
        form by_square = walked;
        nudupl_form(by_square, by_square, D, L);
        reducer.reduce(by_square);
        form by_mul = walked;
        nucomp_form(by_mul, by_mul, by_mul, D, L);
        reducer.reduce(by_mul);
        if (!FormsEqual(by_square, by_mul)) {
            error = "scratch multiply disagreed with squaring";
            return false;
        }
        walked = by_square;
    }

    std::string thread_error;
    std::thread other([&]() {
        std::string local;
        if (!AliasMatchesClassical(base, squared, D, L, local) || !PowPathsMatch(base, D, header_shape, L, local, "thread 264-bit")) {
            thread_error = local.empty() ? "worker mismatch" : local;
        }
    });
    std::string main_error;
    const bool main_ok = AliasMatchesClassical(squared, base, D, L, main_error) &&
        PowPathsMatch(base, D, bits64, L, main_error, "main 64-bit");
    other.join();
    if (!main_ok || !thread_error.empty()) {
        error = !main_ok ? main_error : thread_error;
        return false;
    }

    auto challenge = HexToBytes("9104c5b5e45d48f374efa0488fe6a617790e9aecb3c9cddec06809b09f45ce9b");
    auto x_bytes = HexToBytes(
        "08000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    auto proof = HexToBytes(
        "0200553bf0f382fc65a94f20afad5dbce2c1ee8ba3bf93053559ac9960c8fd80ac2222e9b649701a4141a4d8999f0dbfe0c39ea744096598a7528328e5199f0aa30aec8aae8ab5018bf1245329a8272ddff1afbd87ad2eaba1b7fd57bd25edc62e0b010000003f0ffcd0dc307a2aa4678bafba661c77d176ef23afc86e7ea9f4f9eac52b8e1850748019245ecc96547da9b731dc72cded5582a9b0c63e13fd42446c7b28b41d3ded1d0b666d5ddb5b29719e4ebe70969e67e42ddd8591eae60d83dbe619f1250400");
    integer known_d = CreateDiscriminant(challenge, 1024);
    if (!CheckProofOfTimeNWesolowski(known_d, x_bytes.data(), proof.data(), proof.size(), 129499136, 1024, 0)) {
        error = "known verifier_test.cpp proof was rejected";
        return false;
    }
    form x_form = DeserializeForm(known_d, x_bytes.data(), x_bytes.size());
    form y_form = DeserializeForm(known_d, proof.data(), BQFC_FORM_SIZE);
    integer B = GetB(known_d, x_form, y_form);
    integer r = FastPow(2, 129499136, B);
    integer known_l = root(-known_d, 4);
    if (B.num_bits() < 64 || r.num_bits() < 64) {
        error = "known proof exponents were not on the window side of the cutoff";
        return false;
    }
    form proof_form = DeserializeForm(known_d, proof.data() + BQFC_FORM_SIZE, BQFC_FORM_SIZE);
    if (!PowPathsMatch(proof_form, known_d, B, known_l, error, "known proof^B") ||
        !PowPathsMatch(x_form, known_d, r, known_l, error, "known x^r")) {
        return false;
    }
    if (SmallerThanL(y_form, known_l) || SmallerThanL(proof_form, known_l)) {
        error = "known proof forms are below L, so the partial-gcd multiply branch was not tested";
        return false;
    }
    if (!AliasMatchesClassical(y_form, proof_form, known_d, known_l, error) ||
        !QfbAliasMatchesClassical(y_form, proof_form, known_d, known_l, error)) {
        return false;
    }
    std::vector<uint8_t> bad_proof = proof;
    bad_proof.back() ^= 0x01;
    bool bad_rejected = false;
    try {
        bad_rejected = !CheckProofOfTimeNWesolowski(
            known_d, x_bytes.data(), bad_proof.data(), bad_proof.size(), 129499136, 1024, 0);
    } catch (std::runtime_error const&) {
        bad_rejected = true;
    }
    if (!bad_rejected ||
        CheckProofOfTimeNWesolowski(known_d, x_bytes.data(), proof.data(), proof.size(), 129499135, 1024, 0) ||
        CheckProofOfTimeNWesolowski(known_d, x_bytes.data(), proof.data(), proof.size() - 1, 129499136, 1024, 0)) {
        error = "a damaged proof or the wrong iteration count was accepted";
        return false;
    }

    std::string large_thread_error;
    std::thread large_worker([&]() {
        std::string local;
        for (int i = 0; i < 8; ++i) {
            if (!AliasMatchesClassical(proof_form, y_form, known_d, known_l, local) ||
                !QfbAliasMatchesClassical(y_form, proof_form, known_d, known_l, local)) {
                large_thread_error = local.empty() ? "large worker mismatch" : local;
                return;
            }
        }
    });
    for (int i = 0; i < 8; ++i) {
        if (!AliasMatchesClassical(y_form, proof_form, known_d, known_l, error)) {
            large_worker.join();
            return false;
        }
    }
    large_worker.join();
    if (!large_thread_error.empty()) {
        error = large_thread_error;
        return false;
    }

    integer d512 = CreateDiscriminant(seed, 512);
    integer l512 = root(-d512, 4);
    form base512 = form::generator(d512);
    integer mixed70 = (integer(1) << 69) + (integer(1) << 7) + integer(1);
    if (mixed70.num_bits() != 70 ||
        !PowPathsMatch(base512, d512, integer(0), l512, error, "512-bit disc exponent 0") ||
        !PowPathsMatch(base512, d512, integer(1), l512, error, "512-bit disc exponent 1") ||
        !PowPathsMatch(base512, d512, integer(41), l512, error, "512-bit disc exponent 41") ||
        !PowPathsMatch(base512, d512, bits63, l512, error, "512-bit disc 63-bit") ||
        !PowPathsMatch(base512, d512, bits64, l512, error, "512-bit disc 64-bit") ||
        !PowPathsMatch(base512, d512, mixed70, l512, error, "512-bit disc 70-bit")) {
        return false;
    }
    return true;
}


TEST(FastPowNucompRegression, OneBitWindowBoundaryAliasKnownProofAndThreads) {
    std::string error;
    EXPECT_TRUE(RunFastPowNucompChecks(error)) << error;
}
