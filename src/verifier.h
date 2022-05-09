#ifndef VERIFIER_H
#define VERIFIER_H

#include "include.h"
#include "integer_common.h"
#include "util.h"
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
        uint64_t segment_iters = BytesToInt64(&proof_blob[i]);
        form proof = DeserializeForm(D, &proof_blob[i + 8 + B_bytes], form_size);
        integer B(&proof_blob[i + 8], B_bytes);
        form xnew;
        if (VerifyWesoSegment(D, x, proof, B, segment_iters, xnew))
            return false;

        x = xnew;
        if (segment_iters > iterations) {
            return false;
        }
        iterations -= segment_iters;
    }

    VerifyWesolowskiProof(D, x,
        DeserializeForm(D, proof_blob, form_size),
        DeserializeForm(D, &proof_blob[form_size], form_size),
        iterations, is_valid);

    return is_valid;
}

bool CheckProofOfTimeNWesolowskiCommon(integer& D, form& x, const uint8_t* proof_blob, int32_t proof_blob_len, uint64_t& iterations, int last_segment, bool skip_check = false) {
    int form_size = BQFC_FORM_SIZE;
    int segment_len = 8 + B_bytes + form_size;
    int i = proof_blob_len - segment_len;
    PulmarkReducer reducer;

    for (; i >= last_segment; i -= segment_len) {
        uint64_t segment_iters = BytesToInt64(&proof_blob[i]);
        form proof = DeserializeForm(D, &proof_blob[i + 8 + B_bytes], form_size);
        integer B(&proof_blob[i + 8], B_bytes);
        form xnew;
        if (!skip_check) {
            if (VerifyWesoSegment(D, x, proof, B, segment_iters, xnew))
                return false;
        } else {
            integer L = root(-D, 4);
            integer r = FastPow(2, segment_iters, B);
            form f1 = FastPowFormNucomp(proof, D, B, L, reducer);
            form f2 = FastPowFormNucomp(x, D, r, L, reducer);
            xnew = f1 * f2;
        }

        x = xnew;
        if (segment_iters > iterations) {
            return false;
        }
        iterations -= segment_iters;
    }
    return true;
}

std::pair<bool, std::vector<uint8_t>> CheckProofOfTimeNWesolowskiWithB(integer D, integer B, const uint8_t* x_s, const uint8_t* proof_blob, int32_t proof_blob_len, uint64_t iterations, int32_t depth) {
    int form_size = BQFC_FORM_SIZE;
    int segment_len = 8 + B_bytes + form_size;
    form x = DeserializeForm(D, x_s, form_size);
    std::vector<uint8_t> result;
    if (proof_blob_len != form_size + depth * segment_len) {
        return {false, result};
    }
    bool is_valid = CheckProofOfTimeNWesolowskiCommon(D, x, proof_blob, proof_blob_len, iterations, form_size);
    if (is_valid == false) {
        return {false, result};
    }
    form proof = DeserializeForm(D, proof_blob, form_size);
    form y_result;
    if (VerifyWesoSegment(D, x, proof, B, iterations, y_result) == -1) {
        return {false, result};
    }
    int d_bits = D.num_bits();
    result = SerializeForm(y_result, d_bits);
    return {true, result};
}

// TODO: Perhaps move?
integer GetBFromProof(integer D, const uint8_t* x_s, const uint8_t* proof_blob, int32_t proof_blob_len, uint64_t iterations, int32_t depth) {
    int form_size = BQFC_FORM_SIZE;
    int segment_len = 8 + B_bytes + form_size;
    form x = DeserializeForm(D, x_s, form_size);
    if (proof_blob_len != 2 * form_size + depth * segment_len) {
        throw std::runtime_error("Invalid proof.");
    }
    bool is_valid = CheckProofOfTimeNWesolowskiCommon(D, x, proof_blob, proof_blob_len, iterations, 2 * form_size, true);
    if (is_valid == false) {
        throw std::runtime_error("Invalid proof.");
    }
    form y = DeserializeForm(D, proof_blob, form_size);
    return GetB(D, x, y);
}

#endif // VERIFIER_H
