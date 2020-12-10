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

// Used only to verify 'Proof' objects in tests. This is not used by chia-blockchain.

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

std::vector<form> DeserializeProof(const uint8_t* proof_bytes, int proof_len, integer &D)
{
    int int_size = (D.num_bits() + 16) >> 4;
    std::vector<form> proof;
    for (int i = 0; i < proof_len; i += 2 * int_size)
    {
        std::vector<uint8_t> tmp_bytes;
        for (int j = 0; j < 2 * int_size; j++)
            tmp_bytes.push_back(proof_bytes[i + j]);
        proof.emplace_back(DeserializeForm(D, tmp_bytes.data(), int_size));
    }
    return proof;
}

bool CheckProofOfTimeNWesolowskiInner(integer &D, form x, const uint8_t* proof_blob,
                                      int blob_len, int iters, int int_size,
                                      std::vector<int> iter_list, int recursion)
{
    uint8_t* result_bytes = new uint8_t[2 * 129];
    uint8_t* proof_bytes = new uint8_t[blob_len - 2 * 129];
    memcpy(result_bytes, proof_blob, 2 * 129);
    memcpy(proof_bytes, proof_blob + 2 * 129, blob_len - 2 * 129);
    form y = DeserializeForm(D, result_bytes, 129);
    std::vector<form> proof = DeserializeProof(proof_bytes, blob_len - 2 * 129, D);
    if (recursion * 2 + 1 != proof.size())
        return false;
    if (proof.size() == 1)
    {
        bool is_valid;
        VerifyWesolowskiProof(D, x, y, proof[0], iters, is_valid);
        delete[] result_bytes;
        delete[] proof_bytes;
        return is_valid;
    }
    else
    {
        if (!(proof.size() % 2 == 1 && proof.size() > 2)) {
            delete[] result_bytes;
            delete[] proof_bytes;
            return false;
        }
        int iters1 = iter_list[iter_list.size() - 1];
        int iters2 = iters - iters1;
        bool ver_outer;
        VerifyWesolowskiProof(D, x, proof[proof.size() - 2], proof[proof.size() - 1], iters1, ver_outer);
        if (!ver_outer) {
            delete[] result_bytes;
            delete[] proof_bytes;
            return false;
        }
        uint8_t* new_proof_bytes = new uint8_t[blob_len - 4 * int_size];
        for (int i = 0; i < blob_len - 4 * int_size; i++)
            new_proof_bytes[i] = proof_blob[i];
        iter_list.pop_back();
        bool ver_inner = CheckProofOfTimeNWesolowskiInner(D, proof[proof.size() - 2], new_proof_bytes, blob_len - 4 * int_size, iters2, int_size, iter_list, recursion - 1);
        delete[] result_bytes;
        delete[] proof_bytes;
        delete[] new_proof_bytes;
        if (ver_inner)
            return true;
        return false;
    }
}

bool CheckProofOfTimeNWesolowski(integer D, form x, const uint8_t* proof_blob, int proof_blob_len, int iters, int recursion)
{
    int int_size = (D.num_bits() + 16) >> 4;
    uint8_t* new_proof_blob = new uint8_t[proof_blob_len];
    int new_cnt = 2 * 129 + 2 * int_size;
    memcpy(new_proof_blob, proof_blob, new_cnt);
    std::vector<int> iter_list;
    for (int i = new_cnt; i < proof_blob_len; i += 4 * int_size + 8)
    {
        auto iter_vector = ConvertBytesToInt(proof_blob, i, i + 8).to_vector();
        iter_list.push_back(iter_vector[0]);
        memcpy(new_proof_blob + new_cnt, proof_blob + i + 8, 4 * int_size);
        new_cnt += 4 * int_size;
    }
    bool is_valid = CheckProofOfTimeNWesolowskiInner(D, x, new_proof_blob, new_cnt, iters, int_size, iter_list, recursion);
    delete[] new_proof_blob;
    return is_valid;
}

#endif // VERIFIER_H
