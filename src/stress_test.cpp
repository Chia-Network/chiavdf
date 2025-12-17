#include <gmp.h>
#include <cstdint>
#include <vector>
#include <cstring>
#include <iostream>

#include "verifier.h"

int main() {
    const int form_size = BQFC_FORM_SIZE;
    std::vector<uint8_t> seed(32, 0x01);

    for (int disc_bits : {512, 768, 1024}) {
        integer D = CreateDiscriminant(seed, disc_bits);
        PulmarkReducer reducer;
        integer L = root(-D, 4);
        form x_form = form::generator(D);
        nudupl_form(x_form, x_form, D, L);
        reducer.reduce(x_form);
        form y_form = x_form;
        form proof_form = form::identity(D);
        std::vector<uint8_t> x = SerializeForm(x_form, disc_bits);
        std::vector<uint8_t> y_bytes = SerializeForm(y_form, disc_bits);
        std::vector<uint8_t> proof_bytes = SerializeForm(proof_form, disc_bits);

        std::vector<uint8_t> proof_blob;
        proof_blob.reserve(2 * form_size);
        proof_blob.insert(proof_blob.end(), y_bytes.begin(), y_bytes.end());
        proof_blob.insert(proof_blob.end(), proof_bytes.begin(), proof_bytes.end());
        assert(proof_blob.size() == 2 * form_size);

        bool ok = CheckProofOfTimeNWesolowski(D,
            x.data(),
            proof_blob.data(),
            static_cast<int32_t>(proof_blob.size()),
            0ULL,
            disc_bits,
            0);

        std::cout << "[RES ] verify=" << ok << "\n";
    }
    return 0;
}

