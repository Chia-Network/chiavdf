#ifndef PROOF_COMMON_H
#define PROOF_COMMON_H
#include "Reducer.h"
#include "bqfc.c"

const int B_bits = 264;
const int B_bytes = (B_bits + 7) / 8;


// Generates a random psuedoprime using the hash and check method:
// Randomly chooses x with bit-length `length`, then applies a mask
//   (for b in bitmask) { x |= (1 << b) }.
// Then return x if it is a psuedoprime, otherwise repeat.
integer HashPrime(std::vector<uint8_t> seed, int length, vector<int> bitmask) {
    assert (length % 8 == 0);
    std::vector<uint8_t> hash(picosha2::k_digest_size);  // output of sha256
    std::vector<uint8_t> blob;  // output of 1024 bit hash expansions
    std::vector<uint8_t> sprout = seed;  // seed plus nonce

    while (true) {  // While prime is not found
        blob.resize(0);
        while ((int) blob.size() * 8 < length) {
            // Increment sprout by 1
            for (int i = (int) sprout.size() - 1; i >= 0; --i) {
                sprout[i]++;
                if (sprout[i])
                    break;
            }
            picosha2::hash256(sprout.begin(), sprout.end(), hash.begin(), hash.end());
            blob.insert(blob.end(), hash.begin(),
                std::min(hash.end(), hash.begin() + length / 8 - blob.size()));
        }

        assert ((int) blob.size() * 8 == length);
        integer p(blob);  // p = 7 (mod 8), 2^1023 <= p < 2^1024
        for (int b: bitmask)
            p.set_bit(b, true);
        // Force the number to be odd
        p.set_bit(0, true);
        if (p.prime())
            return p;
    }
}

std::vector<unsigned char> SerializeForm(form &y, int d_bits)
{
    y.reduce();
    std::vector<unsigned char> res(BQFC_FORM_SIZE);
    bqfc_serialize(res.data(), y.a.impl, y.b.impl, d_bits);
    return res;
}

form DeserializeForm(const integer &D, const uint8_t *bytes, size_t size)
{
    integer a, b;
    if (bqfc_deserialize(a.impl, b.impl, D.impl, bytes, size, D.num_bits())) {
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
    mpz_powm_ui(res.impl, a1.impl, b, c.impl);
    return res;
}

integer GetB(const integer& D, form &x, form& y) {
    int d_bits = D.num_bits();
    std::vector<unsigned char> serialization = SerializeForm(x, d_bits);
    std::vector<unsigned char> serialization_y = SerializeForm(y, d_bits);
    serialization.insert(serialization.end(), serialization_y.begin(), serialization_y.end());
    return HashPrime(serialization, B_bits, {B_bits - 1});
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

form FastPowFormNucomp(form x, integer &D, integer num_iterations, integer &L, PulmarkReducer& reducer)
{
    if (!mpz_sgn(num_iterations.impl))
        return form::identity(D);

    form res = x;
    int max_size = -D.impl->_mp_size / 2 + 1, i;

    // Do exponentiation by squaring from top bits of exponent to bottom
    for (i = num_iterations.num_bits() - 2; i >= 0; i--) {
        nudupl_form(res, res, D, L);
        if (res.a.impl->_mp_size > max_size) {
            // Reduce only when 'a' exceeds a half of the discriminant size by
            // more than one limb
            reducer.reduce(res);
        }

        if (num_iterations.get_bit(i)) {
            nucomp_form(res, res, x, D, L);
        }
    }

    reducer.reduce(res);
    return res;
}

# endif // PROOF_COMMON_H
