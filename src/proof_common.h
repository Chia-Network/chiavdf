#ifndef PROOF_COMMON_H
#define PROOF_COMMON_H
#include "Reducer.h"

std::vector<unsigned char> ConvertIntegerToBytes(integer x, uint64_t num_bytes) {
    std::vector<unsigned char> bytes;
    bool negative = false;
    if (x < 0) {
        x = abs(x);
        x = x - integer(1);
        negative = true;
    }
    for (int iter = 0; iter < num_bytes; iter++) {
        auto byte = (x % integer(256)).to_vector();
        if (negative)
            byte[0] ^= 255;
        bytes.push_back(byte[0]);
        x = x / integer(256);
    }
    std::reverse(bytes.begin(), bytes.end());
    return bytes;
}


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
                if (!sprout[i])
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
        if (p.prime())
            return p;
    }
}

std::vector<unsigned char> SerializeForm(form &y, int int_size) {
    y.reduce();
    std::vector<unsigned char> res = ConvertIntegerToBytes(y.a, int_size);
    std::vector<unsigned char> b_res = ConvertIntegerToBytes(y.b, int_size);
    res.insert(res.end(), b_res.begin(), b_res.end());
    return res;
}

integer FastPow(uint64_t a, uint64_t b, integer& c) {
    if (b == 0) {
        return integer(1);
    }
    integer res, a1 = integer(a);
    mpz_powm_ui(res.impl, a1.impl, b, c.impl);
    return res;
}

integer GetB(const integer& D, form &x, form& y) {
    int int_size = (D.num_bits() + 16) >> 4;
    std::vector<unsigned char> serialization = SerializeForm(x, int_size);
    std::vector<unsigned char> serialization_y = SerializeForm(y, int_size);
    serialization.insert(serialization.end(), serialization_y.begin(), serialization_y.end());
    return HashPrime(serialization, 264, {263});
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
    form res = form::identity(D);
    int D_size = D.impl->_mp_size;

    while (1) {
        if (num_iterations.get_bit(0)) {
            nucomp_form(res, res, x, D, L);
            if (res.a.impl->_mp_size > D_size) {
                // Reduce only when 'a' has more limbs than D
                reducer.reduce(res);
            }
        }

        num_iterations >>= 1;
        if (num_iterations == 0) {
            break;
        }

        nudupl_form(x, x, D, L);
        if (x.a.impl->_mp_size > D_size) {
            reducer.reduce(x);
        }
    }
    return res;
}

# endif // PROOF_COMMON_H
