#ifndef REFERENCE_GOLDEN_H
#define REFERENCE_GOLDEN_H

// Digests of the reference class-group arithmetic (nudupl_form, nucomp_form,
// mpz_xgcd_partial) on fixed inputs, for reference_golden_regression_test.cpp.
// Inputs come from SHA-256, not GMP's random generator, so they are the same
// with GMP and MPIR on every platform. Include after vdf.h.

#include "picosha2.h"

#include <array>
#include <string>
#include <vector>

namespace reference_golden {

class Digest {
  public:
    void add(const std::string &s) { data_.insert(data_.end(), s.begin(), s.end()); data_.push_back('|'); }
    void add(mpz_srcptr z) { add(mpz_get_str_owned(z)); }
    void add(const form &f) { add(f.a.impl); add(f.b.impl); add(f.c.impl); }
    std::string hex() const { return picosha2::hash256_hex_string(data_); }

  private:
    static std::string mpz_get_str_owned(mpz_srcptr z)
    {
        std::vector<char> buf(mpz_sizeinbase(z, 16) + 2);
        mpz_get_str(buf.data(), 16, z);
        return std::string(buf.data());
    }
    std::vector<unsigned char> data_;
};

// Deterministic integer of the given bit length from SHA-256(tag, counter).
inline void hashed_integer(mpz_ptr out, const std::string &tag, uint64_t counter, unsigned bits)
{
    mpz_set_ui(out, 0);
    for (uint64_t block = 0; mpz_sizeinbase(out, 2) < bits + 256; block++) {
        std::string seed = tag + ":" + std::to_string(counter) + ":" + std::to_string(block);
        std::vector<unsigned char> h(picosha2::k_digest_size);
        picosha2::hash256(seed.begin(), seed.end(), h.begin(), h.end());
        for (unsigned char byte : h) {
            mpz_mul_2exp(out, out, 8);
            mpz_add_ui(out, out, byte);
        }
    }
    mpz_fdiv_r_2exp(out, out, bits);
}

// [0] NUDUPL chain (unreduced outputs), [1] the same chain reduced,
// [2] unreduced NUCOMP bucket products, [3] mpz_xgcd_partial corpus.
inline std::array<std::string, 4> digests()
{
    std::vector<uint8_t> seed = {0x67, 0x6f, 0x6c, 0x64, 0x65, 0x6e};
    integer D = CreateDiscriminant(seed, 1024);
    integer L = root(-D, 4);
    PulmarkReducer reducer;
    std::array<std::string, 4> out;

    Digest unreduced, reduced;
    std::vector<form> values;
    form f = form::generator(D);
    for (int i = 0; i < 20000; i++) {
        nudupl_form(f, f, D, L);
        unreduced.add(f);
        reducer.reduce(f);
        reduced.add(f);
        if (i % 5 == 0)
            values.push_back(f);
    }
    out[0] = unreduced.hex();
    out[1] = reduced.hex();

    Digest products;
    std::vector<form> buckets(16, form::identity(D));
    for (size_t i = 0; i < values.size(); i++) {
        form &y = buckets[(i * 7) % 16];
        if (i % 3)
            nucomp_form(y, y, values[i], D, L);
        else
            nucomp_form(y, values[i], y, D, L);
        products.add(y);
    }
    out[2] = products.hex();

    Digest partial;
    mpz_t r2, r1, limit, co2, co1;
    mpz_inits(r2, r1, limit, co2, co1, NULL);
    for (uint64_t n = 0; n < 5000; n++) {
        unsigned bits = 2 + n % 700;
        hashed_integer(r2, "r2", n, bits);
        hashed_integer(r1, "r1", n, bits);
        hashed_integer(limit, "L", n, bits / 2 + n % 64);
        if (mpz_cmp(r2, r1) < 0)
            mpz_swap(r2, r1);
        if (n % 7 == 0) mpz_set_ui(r1, 0);
        if (n % 11 == 0) mpz_set(r1, r2);
        if (n % 13 == 0) mpz_set_ui(limit, 0);
        if (n % 19 == 0) mpz_set(limit, r1);
        mpz_xgcd_partial(co2, co1, r2, r1, limit);
        partial.add(co2);
        partial.add(co1);
        partial.add(r2);
        partial.add(r1);
    }
    mpz_clears(r2, r1, limit, co2, co1, NULL);
    out[3] = partial.hex();
    return out;
}

}  // namespace reference_golden

#endif // REFERENCE_GOLDEN_H
