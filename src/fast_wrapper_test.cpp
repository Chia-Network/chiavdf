#include "c_bindings/fast_wrapper.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [iters]\n";
}

int main(int argc, char** argv) {
    uint64_t iters = 1000;
    if (argc >= 2) {
        try {
            iters = std::stoull(argv[1]);
        } catch (...) {
            usage(argv[0]);
            return 2;
        }
    }

    // Match existing tests.
    const uint8_t challenge_hash[] = {0, 0, 1, 2, 3, 3, 4, 4};
    constexpr size_t challenge_size = sizeof(challenge_hash);
    constexpr size_t discriminant_bits = 1024;

    // BQFC "generator" special encoding: first byte has BQFC_IS_GEN bit set (0x08).
    // `DeserializeForm()` only reads `str[0]` for special forms but insists on size==BQFC_FORM_SIZE (100).
    const uint8_t x_gen_flag = 0x08;
    constexpr size_t form_size = 100;

    ChiavdfByteArray fast = chiavdf_prove_one_weso_fast(
        challenge_hash,
        challenge_size,
        &x_gen_flag,
        form_size,
        discriminant_bits,
        iters);

    if (fast.data == nullptr || fast.length == 0) {
        std::cerr << "chiavdf_prove_one_weso_fast failed\n";
        return 1;
    }
    if (fast.length != 2 * form_size) {
        std::cerr << "Unexpected output length from fast prover: " << fast.length << "\n";
        chiavdf_free_byte_array(fast);
        return 1;
    }

    // Split `y || proof`.
    const uint8_t* y_ref = fast.data;
    const size_t y_ref_size = form_size;

    ChiavdfByteArray streaming = chiavdf_prove_one_weso_fast_streaming_getblock_opt(
        challenge_hash,
        challenge_size,
        &x_gen_flag,
        form_size,
        y_ref,
        y_ref_size,
        discriminant_bits,
        iters);

    if (streaming.data == nullptr || streaming.length == 0) {
        std::cerr << "chiavdf_prove_one_weso_fast_streaming_getblock_opt failed\n";
        chiavdf_free_byte_array(fast);
        return 1;
    }
    if (streaming.length != fast.length) {
        std::cerr << "Length mismatch: fast=" << fast.length << " streaming=" << streaming.length << "\n";
        chiavdf_free_byte_array(streaming);
        chiavdf_free_byte_array(fast);
        return 1;
    }

    const bool same = (std::memcmp(fast.data, streaming.data, fast.length) == 0);
    if (!same) {
        std::cerr << "Mismatch between fast and streaming proofs\n";
        chiavdf_free_byte_array(streaming);
        chiavdf_free_byte_array(fast);
        return 1;
    }

    std::cout << "OK: streaming proof matches fast proof (" << iters << " iters)\n";
    chiavdf_free_byte_array(streaming);
    chiavdf_free_byte_array(fast);
    return 0;
}

