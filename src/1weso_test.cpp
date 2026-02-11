#include "vdf.h"
#include "create_discriminant.h"
#include "verifier.h"

#include <atomic>
#include <cassert>

int segments = 7;
int thread_count = 3;

Proof CreateProof(ProverManager& pm, uint64_t iteration) {
    return pm.Prove(iteration);
}

int gcd_base_bits=50;
int gcd_128_max_iter=3;

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
    // #region agent log
    agent_debug_log_ndjson(
        "H1",
        "src/1weso_test.cpp:main:init",
        "initialized_1weso_test",
        std::string("{\"iter_multiplier\":") + std::to_string(iter_multiplier) +
            ",\"has_avx2\":" + std::to_string(has_avx2 ? 1 : 0) + "}"
    );
    // #endregion
    if(has_avx2)
    {
      gcd_base_bits=63;
      gcd_128_max_iter=2;
    }
    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    int d_bits = 1024;
    integer D = CreateDiscriminant(challenge_hash, d_bits);

    if (getenv( "warn_on_corruption_in_production" )!=nullptr) {
        warn_on_corruption_in_production=true;
    }
    set_rounding_mode();

    integer L=root(-D, 4);
    form f=form::generator(D);

    std::atomic<bool> stopped = false;
    fast_algorithm = false;

    uint64_t iter = iter_multiplier;
    OneWesolowskiCallback weso(D, f, iter);
    FastStorage* fast_storage = nullptr;
    // #region agent log
    agent_debug_log_ndjson(
        "H3",
        "src/1weso_test.cpp:main:before_worker",
        "starting_worker_thread",
        std::string("{\"iter\":") + std::to_string(iter) + "}"
    );
    // #endregion
    std::thread vdf_worker(repeated_square, iter, f, D, L, &weso, fast_storage, std::ref(stopped));
    // #region agent log
    agent_debug_log_ndjson(
        "H3",
        "src/1weso_test.cpp:main:before_prove",
        "calling_prove_one_wesolowski",
        std::string("{\"iter\":") + std::to_string(iter) + "}"
    );
    // #endregion
    Proof const proof = ProveOneWesolowski(iter, D, f, &weso, stopped);
    // #region agent log
    agent_debug_log_ndjson(
        "H4",
        "src/1weso_test.cpp:main:after_prove",
        "prove_one_wesolowski_returned",
        std::string("{\"proof_y_size\":") + std::to_string(proof.y.size()) +
            ",\"proof_proof_size\":" + std::to_string(proof.proof.size()) + "}"
    );
    // #endregion
    stopped = true;
    vdf_worker.join();
    // #region agent log
    agent_debug_log_ndjson(
        "H5",
        "src/1weso_test.cpp:main:after_join",
        "worker_joined_about_to_verify",
        std::string("{\"iter\":") + std::to_string(iter) + "}"
    );
    // #endregion

    bool is_valid;
    form x_init = form::generator(D);
    form y = DeserializeForm(D, proof.y.data(), proof.y.size());
    form proof_form = DeserializeForm(D, proof.proof.data(), proof.proof.size());
    VerifyWesolowskiProof(D, x_init, y, proof_form, iter, is_valid);
    std::cout << "Verify result: " << is_valid << "\n";
    assert(is_valid);
}
catch (std::exception const& e) {
    std::cerr << "Exception " << e.what() << '\n';
    return 1;
}
