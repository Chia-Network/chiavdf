#include "vdf.h"
#include "create_discriminant.h"
#include "verifier.h"

#include <atomic>
#include <thread>

int segments = 7;
int thread_count = 3;

int gcd_base_bits=50;
int gcd_128_max_iter=3;

void CheckProof(integer& D, Proof& proof, uint64_t iteration) {
    form x = form::generator(D);
    std::vector<unsigned char> bytes;
    bytes.insert(bytes.end(), proof.y.begin(), proof.y.end());
    bytes.insert(bytes.end(), proof.proof.begin(), proof.proof.end());
    if (CheckProofOfTimeNWesolowski(D, DEFAULT_ELEMENT, bytes.data(), bytes.size(), iteration, 1024, proof.witness_type)) {
        std::cout << "Correct proof\n";
    } else {
        std::cout << "Incorrect proof\n";
        throw std::runtime_error("incorrect proof");
    }
}

int main(int argc, char const* argv[]) try
{
    // allow setting the multiplier for the number of iterations to test on the
    // command line. This can be used to run smaller and faster tests on CI,
    // specifically with instrumented binaries that aren't as fast
    std::uint64_t const iter_multiplier = (argc > 1)
        ? std::stoull(argv[1]) : 1000000;

    assert(is_vdf_test); //assertions should be disabled in VDF_MODE==0
    init_gmp();
    debug_mode = true;
    const bool has_avx2 = hasAVX2();
    if(has_avx2)
    {
      gcd_base_bits=63;
      gcd_128_max_iter=2;
    }
    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    integer D = CreateDiscriminant(challenge_hash, 1024);

    if (env_flag("warn_on_corruption_in_production")) {
        warn_on_corruption_in_production=true;
    }
    set_rounding_mode();

    integer L=root(-D, 4);
    form f=form::generator(D);
    std::atomic<bool> stopped = false;
    fast_algorithm = false;
    two_weso = true;
    TwoWesolowskiCallback weso(D, f);
    FastStorage* fast_storage = NULL;
    std::thread vdf_worker(repeated_square, 0, f, D, L, &weso, fast_storage, std::ref(stopped));
    auto stop_worker = [&]() {
        stopped = true;
        if (vdf_worker.joinable()) {
            vdf_worker.join();
        }
    };
    try {
        auto run_test = [&](uint64_t iteration) {
            Proof proof = ProveTwoWeso(D, f, iteration, 0, &weso, 0, stopped);
            CheckProof(D, proof, iteration);
        };
        // Test 1 - 1 million iters.
        uint64_t iteration = 1 * iter_multiplier;
        run_test(iteration);
        // Test 2 - 15 million iters.
        iteration = 15 * iter_multiplier;
        run_test(iteration);
        // Test 3 - 100 million iters.
        iteration = 100 * iter_multiplier;
        run_test(iteration);
        // Test stopping gracefully.
        stop_worker();
    } catch (...) {
        stop_worker();
        throw;
    }
    return 0;
}
catch (std::exception const& e) {
    std::cerr << "Exception " << e.what() << '\n';
    return 1;
}
