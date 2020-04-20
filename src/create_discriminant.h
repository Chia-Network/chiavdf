#ifndef CREATE_DISCRIMINANT_H
#define CREATE_DISCRIMINANT_H

#include "include.h"
#include "integer_common.h"
#include "picosha2.h"

integer CreateDiscriminant(std::vector<uint8_t>& seed, int length = 1024) {
    assert (length % 256 == 0);
    std::vector<uint8_t> hash(picosha2::k_digest_size);  // output of sha256
    std::vector<uint8_t> blob;  // output of 1024 bit hash expansions
    std::vector<uint8_t> sprout = seed;  // seed plus nonce

    while (true) {  // While prime is not found
        blob.resize(0);
        while ((int) blob.size() < length / 8) {
            int i = (int) sprout.size() - 1;
            while (!++sprout[i--]);  // equivalent of sprout++ if it was integer
            picosha2::hash256(sprout.begin(), sprout.end(), hash.begin(), hash.end());
            blob.insert(blob.end(), hash.begin(), hash.end());
        }

        integer p(blob);  // p = 7 (mod 8), p >= 2^1023
        p.set_bit(0, true);
        p.set_bit(1, true);
        p.set_bit(2, true);
        p.set_bit(length - 1, true);
        if (p.prime())
            return p * integer(-1);
    }
}

#endif // CREATE_DISCRIMINANT_H
