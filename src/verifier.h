#ifndef VERIFIER_H
#define VERIFIER_H

#include "include.h"
#include "integer_common.h"
#include "vdf_new.h"
#include "picosha2.h"
#include "nucomp.h"
#include "proof_common.h"
#include "create_discriminant.h"

void VerifyWesolowskiProof(integer &D, form x, form y, form proof, uint64_t iters, bool &is_valid)
{
    PulmarkReducer reducer;
    int int_size = (D.num_bits() + 16) >> 4;
    integer L = root(-D, 4);
    integer B = GetB(D, x, y);
    integer r = FastPow(2, iters, B);
    form f1 = FastPowFormNucomp(proof, D, B, L, reducer);
    form f2 = FastPowFormNucomp(x, D, r, L, reducer);
    if (f1 * f2 == y)
    {
        is_valid = true;
    }
    else
    {
        is_valid = false;
    }
}

integer ConvertBytesToInt(const uint8_t* bytes, int start_index, int end_index)
{
    integer res(0);
    bool negative = false;
    if (bytes[start_index] & (1 << 7))
        negative = true;
    for (int i = start_index; i < end_index; i++)
    {
        res = res * integer(256);
        if (!negative)
            res = res + integer(bytes[i]);
        else
            res = res + integer(bytes[i] ^ 255);
    }
    if (negative)
    {
        res = res + integer(1);
        res = res * integer(-1);
    }
    return res;
}

form DeserializeForm(integer &d, const uint8_t* bytes, int int_size)
{
    integer a = ConvertBytesToInt(bytes, 0, int_size);
    integer b = ConvertBytesToInt(bytes, int_size, 2 * int_size);
    form f = form::from_abd(a, b, d);
    return f;
}

bool CheckProofOfTimeNWesolowski(integer D, integer a, integer b, const uint8_t* proof_blob, int proof_blob_len, uint64_t iterations, uint64 disc_size_bits, int depth)
{
    form x = form::from_abd(a,b,D);
    int int_size = (disc_size_bits + 16) >> 4;

    if (proof_blob_len != 4 * int_size + depth * (8 + 4 * int_size))
        return false;

    form y = DeserializeForm(D, proof_blob, int_size);

    std::vector<form> proof;
    proof.reserve(depth * 2 + 1);
    std::vector<uint64_t> iter_list;
    iter_list.reserve(depth);

    proof.emplace_back(DeserializeForm(D, &(proof_blob[2 * int_size]), int_size));

    // Loop depth times
    for (int i = 4 * int_size; i < proof_blob_len; i += 4 * int_size + 8)
    {
        auto iter_vector = ConvertBytesToInt(proof_blob, i, i + 8).to_vector();
        iter_list.push_back(iter_vector[0]);
        proof.emplace_back(DeserializeForm(D, &(proof_blob[i + 8]), int_size));
        proof.emplace_back(DeserializeForm(D, &(proof_blob[i + 8 + 2 * int_size]), int_size));
    }

    if (depth * 2 + 1 != proof.size())
            return false;
    
    bool is_valid = false;
    for (int i=0; i < depth; i++) {
        uint64_t iterations_1=iter_list[iter_list.size() - 1 - i];
        VerifyWesolowskiProof(D, x, 
            proof[proof.size() - 2 - 2 * i], 
            proof[proof.size() - 1 - 2 * i], 
            iterations_1, is_valid);
        if(!is_valid)
            return false;
        x=proof[proof.size() - 2 - 2 * i];
	iterations=iterations - iterations_1;
    }

    VerifyWesolowskiProof(D, x, y, proof[0], iterations, is_valid);
    if(!is_valid)
        return false;
   
    return true;
}

#endif // VERIFIER_H
