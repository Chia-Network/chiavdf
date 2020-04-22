#ifndef CREATE_DISCRIMINANT_H
#define CREATE_DISCRIMINANT_H

#include "proof_common.h"

integer CreateDiscriminant(std::vector<uint8_t>& seed, int length = 1024) {
    return HashPrime(seed, length, {0, 1, 2, length - 1}) * integer(-1);
}

#endif // CREATE_DISCRIMINANT_H
