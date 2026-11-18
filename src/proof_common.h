#ifndef PROOF_COMMON_H
#define PROOF_COMMON_H
#include "Reducer.h"
#include "bqfc.c"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <stdexcept>

const int B_bits = 264;
const int B_bytes = (B_bits + 7) / 8;

// Generates a random pseudoprime using the hash and check method:
// Randomly chooses x with bit-length `length`, then applies a mask
//   (for b in bitmask) { x |= (1 << b) }.
// Then return x if it is a pseudoprime, otherwise repeat.
integer HashPrime(std::span<const uint8_t> seed, int length, std::span<const int> bitmask) {
    assert (length % 8 == 0);
    std::array<uint8_t, picosha2::k_digest_size> hash{};
    std::array<uint8_t, BQFC_MAX_D_BITS / 8> blob{};
    std::array<uint8_t, 32> stack_sprout{};
    std::vector<uint8_t> heap_sprout;
    uint8_t* sprout;
    if (seed.size() <= stack_sprout.size()) {
        std::copy(seed.begin(), seed.end(), stack_sprout.begin());
        sprout = stack_sprout.data();
    } else {
        heap_sprout.assign(seed.begin(), seed.end());
        sprout = heap_sprout.data();
    }
    const size_t sprout_size = seed.size();

    while (true) {  // While prime is not found
        size_t blob_size = 0;
        while (blob_size * 8 < static_cast<size_t>(length)) {
            // Increment sprout by 1
            for (size_t i = sprout_size; i > 0; --i) {
                sprout[i - 1]++;
                if (sprout[i - 1])
                    break;
            }
            picosha2::hash256(sprout, sprout + sprout_size, hash.begin(), hash.end());
            const size_t bytes_to_append = std::min(hash.size(), static_cast<size_t>(length / 8) - blob_size);
            std::copy_n(hash.begin(), bytes_to_append, blob.begin() + blob_size);
            blob_size += bytes_to_append;
        }

        assert (blob_size * 8 == static_cast<size_t>(length));
        integer p(blob.data(), blob_size);  // p = 7 (mod 8), 2^1023 <= p < 2^1024
        for (int b: bitmask)
            p.set_bit(b, true);
        // Force the number to be odd
        p.set_bit(0, true);
        if (p.prime())
            return p;
    }
}

inline void SerializeForm(form& y, int d_bits, std::span<uint8_t> output) {
    if (output.size() != BQFC_FORM_SIZE) {
        throw std::invalid_argument("serialized form output must have BQFC_FORM_SIZE bytes");
    }
    y.reduce();
    bqfc_serialize(output.data(), y.a.impl, y.b.impl, d_bits);
}

inline std::vector<unsigned char> SerializeForm(form& y, int d_bits) {
    std::vector<unsigned char> res(BQFC_FORM_SIZE);
    SerializeForm(y, d_bits, std::span<uint8_t>(res.data(), res.size()));
    return res;
}

form DeserializeForm(const integer &D, const uint8_t *bytes, size_t size,
                     bool strict = false)
{
    integer a, b;
    if (bqfc_deserialize(a.impl, b.impl, D.impl, bytes, size, D.num_bits(), strict)) {
        throw std::runtime_error("Deserializing compressed form failed");
    }
    form f = form::from_abd(a, b, D);
    if (!f.is_reduced()) {
        throw std::runtime_error("Form is not reduced");
    }
    return f;
}

integer FastPow(uint64_t a, uint64_t b, integer& c) {
    integer res, a1 = integer(a);
    if (mpz_sgn(c.impl) == 0) {
        throw std::runtime_error("Division by 0");
    }
    mpz_powm_ui(res.impl, a1.impl, b, c.impl);
    return res;
}

integer GetB(const integer& D, form &x, form& y) {
    int d_bits = D.num_bits();
    std::array<uint8_t, 2 * BQFC_FORM_SIZE> serialization{};
    SerializeForm(x, d_bits, std::span<uint8_t>(serialization.data(), BQFC_FORM_SIZE));
    SerializeForm(y, d_bits, std::span<uint8_t>(serialization.data() + BQFC_FORM_SIZE, BQFC_FORM_SIZE));
    const std::array<int, 1> bitmask{B_bits - 1};
    return HashPrime(serialization, B_bits, bitmask);
}

class PulmarkReducer {
    ClassGroupContext *t;
    Reducer *reducer;

  public:
    PulmarkReducer() {
        t=new ClassGroupContext(4096);
        reducer=new Reducer(*t);
    }

    ~PulmarkReducer() {
        delete(reducer);
        delete(t);
    }

    void reduce(form &f) {
        mpz_set(t->a, f.a.impl);
        mpz_set(t->b, f.b.impl);
        mpz_set(t->c, f.c.impl);

        reducer->run();

        mpz_set(f.a.impl, t->a);
        mpz_set(f.b.impl, t->b);
        mpz_set(f.c.impl, t->c);
    }
};

// One left-to-right walk. Width 1 does not build x^2..x^15. Width 4 does.
// Exponent 0 is the identity. GMP reports that zero has bit length 1, so a
// length check alone would reduce the input instead of returning the identity.
template <int Width>
form FastPowFormNucompWidth(form x, integer &D, integer num_iterations, integer &L, PulmarkReducer& reducer)
{
    static_assert(Width == 1 || Width == 4, "only the one-bit and four-bit walks are used");
    if (mpz_sgn(num_iterations.impl) < 0) {
        throw std::invalid_argument("FastPowFormNucomp exponent must be non-negative");
    }
    if (!mpz_sgn(num_iterations.impl)) {
        return form::identity(D);
    }

    const int max_size = -D.impl->_mp_size / 2;
    auto reduce_if_large = [&](form& f) {
        if (f.a.impl->_mp_size > max_size) {
            reducer.reduce(f);
        }
    };

    constexpr int kTable = 1 << Width;
    form table[kTable];
    // Slot 0 is unused. A zero digit skips the multiply, and the leading digit
    // includes the top bit so it is not zero. The default form is (0, 0, 0);
    // reading it would divide by zero.
    table[1] = x;
    for (int i = 2; i < kTable; ++i) {
        nucomp_form(table[i], table[i - 1], x, D, L);
        reduce_if_large(table[i]);
    }

    const int nbits = num_iterations.num_bits();
    int index = nbits - 1;
    auto take_bits = [&](int count) {
        unsigned digit = 0;
        for (int i = 0; i < count; ++i) {
            digit = (digit << 1) | static_cast<unsigned>(num_iterations.get_bit(index));
            --index;
        }
        return digit;
    };

    const int first_width = (nbits % Width == 0) ? Width : (nbits % Width);
    const unsigned first_digit = take_bits(first_width);
    assert(first_digit != 0);
    form res = table[first_digit];

    while (index >= 0) {
        for (int i = 0; i < Width; ++i) {
            nudupl_form(res, res, D, L);
            reduce_if_large(res);
        }
        const unsigned digit = take_bits(Width);
        if (digit != 0) {
            assert(digit < static_cast<unsigned>(kTable));
            nucomp_form(res, res, table[digit], D, L);
            reduce_if_large(res);
        }
    }

    reducer.reduce(res);
    return res;
}

// Header checks use a 264-bit exponent. Proof construction stays under about 2^13.
// A dense exponent pays back the 14-entry table at roughly 56 bits, so 64 sits
// above every proof-builder call and on the fast side of the header check.
form FastPowFormNucomp(form x, integer &D, integer num_iterations, integer &L, PulmarkReducer& reducer)
{
    if (mpz_sgn(num_iterations.impl) < 0) {
        throw std::invalid_argument("FastPowFormNucomp exponent must be non-negative");
    }
    if (!mpz_sgn(num_iterations.impl)) {
        return form::identity(D);
    }
    if (num_iterations.num_bits() < 64) {
        return FastPowFormNucompWidth<1>(x, D, num_iterations, L, reducer);
    }
    return FastPowFormNucompWidth<4>(x, D, num_iterations, L, reducer);
}

# endif // PROOF_COMMON_H
