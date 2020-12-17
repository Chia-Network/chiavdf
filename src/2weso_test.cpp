#include "vdf.h"
#include "create_discriminant.h"
#include "verifier.h"

#include <atomic>

int segments = 7;
int thread_count = 3;

int gcd_base_bits=50;
int gcd_128_max_iter=3;

void CheckProof(integer& D, Proof& proof, uint64_t iteration) {
    form x = form::generator(D);
    std::vector<unsigned char> bytes;
    bytes.insert(bytes.end(), proof.y.begin(), proof.y.end());
    bytes.insert(bytes.end(), proof.proof.begin(), proof.proof.end());
    if (CheckProofOfTimeNWesolowski(D, x, bytes.data(), bytes.size(), iteration, proof.witness_type)) {
        std::cout << "Correct proof\n";
    } else {
        std::cout << "Incorrect proof\n";
        abort();
    }
}

int main() {
    debug_mode = true;
    if(hasAVX2())
    {
      gcd_base_bits=63;
      gcd_128_max_iter=2;
    }
    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    integer D = CreateDiscriminant(challenge_hash, 1024);

    if (getenv( "warn_on_corruption_in_production" )!=nullptr) {
        warn_on_corruption_in_production=true;
    }
    assert(is_vdf_test); //assertions should be disabled in VDF_MODE==0
    init_gmp();
    allow_integer_constructor=true; //make sure the old gmp allocator isn't used
    set_rounding_mode();

    integer L=root(-D, 4);
    form f=form::generator(D);

    std::atomic<bool> stopped = false;
    fast_algorithm = false;
    two_weso = true;
    TwoWesolowskiCallback weso(D, f);
    FastStorage* fast_storage = NULL;
    std::thread vdf_worker(repeated_square, f, D, L, &weso, fast_storage, std::ref(stopped));
    // Test 1 - 1 million iters.
    uint64_t iteration = 1000000;
    Proof proof = ProveTwoWeso(D, f, 1000000, 0, &weso, 0, stopped);
    CheckProof(D, proof, iteration);
    // Test 2 - 15 million iters.
    iteration = 15000000;
    proof = ProveTwoWeso(D, f, iteration, 0, &weso, 0, stopped);
    CheckProof(D, proof, iteration);
    // Test 3 - 100 million iters.
    iteration = 30000000;
    proof = ProveTwoWeso(D, f, iteration, 0, &weso, 0, stopped);
    CheckProof(D, proof, iteration);
    // Test stopping gracefully.
    stopped = true;
    vdf_worker.join();
    exit(0);
}
