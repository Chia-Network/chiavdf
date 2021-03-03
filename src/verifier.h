#ifndef VERIFIER_H
#define VERIFIER_H

#include "include.h"
#include "integer_common.h"
#include "vdf_new.h"
#include "picosha2.h"
#include "nucomp.h"
#include "proof_common.h"
#include "create_discriminant.h"

const uint8_t DEFAULT_ELEMENT[] = { 0x08 };

int VerifyWesoSegment(integer &D, form x, form proof, integer &B, uint64_t iters, form &out_y)
{
    PulmarkReducer reducer;
    integer L = root(-D, 4);
    integer r = FastPow(2, iters, B);
    form f1 = FastPowFormNucomp(proof, D, B, L, reducer);
    form f2 = FastPowFormNucomp(x, D, r, L, reducer);
    out_y = f1 * f2;

    return B == GetB(D, x, out_y) ? 0 : -1;
}

void VerifyWesolowskiProof(integer &D, form x, form y, form proof, uint64_t iters, bool &is_valid)
{
    PulmarkReducer reducer;
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

integer ConvertBytesToInt(const uint8_t* bytes, int32_t start_index, int32_t end_index)
{
    integer res(0);
    bool negative = false;
    if (bytes[start_index] & (1 << 7))
        negative = true;
    for (int32_t i = start_index; i < end_index; i++)
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

bool CheckProofOfTimeNWesolowski(integer D, const uint8_t* x_s, const uint8_t* proof_blob, int32_t proof_blob_len, uint64_t iterations, uint64 disc_size_bits, int32_t depth)
{
    int form_size = BQFC_FORM_SIZE;
    int segment_len = 8 + B_bytes + form_size;
    int i = proof_blob_len - segment_len;
    form x = DeserializeForm(D, x_s, form_size);

    if (proof_blob_len != 2 * form_size + depth * segment_len)
        return false;

    // Loop depth times
    bool is_valid = false;
    for (; i >= 2 * form_size; i -= segment_len) {
        auto iter_vector = ConvertBytesToInt(proof_blob, i, i + 8).to_vector();
        form proof = DeserializeForm(D, &proof_blob[i + 8 + B_bytes], form_size);
        integer B(&proof_blob[i + 8], B_bytes);
        form xnew;
        if (VerifyWesoSegment(D, x, proof, B, iter_vector[0], xnew))
            return false;

        x=xnew;
        iterations=iterations - iter_vector[0];
    }

    VerifyWesolowskiProof(D, x,
        DeserializeForm(D, proof_blob, form_size),
        DeserializeForm(D, &proof_blob[form_size], form_size),
        iterations, is_valid);

    return is_valid;
}

#endif // VERIFIER_H
