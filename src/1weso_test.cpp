#include "vdf.h"
#include "create_discriminant.h"
#include "verifier.h"

#include <atomic>

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
    set_rounding_mode();

    integer L=root(-D, 4);
    form f=form::generator(D);

    std::atomic<bool> stopped = false;
    fast_algorithm = false;

    uint64_t iter = iter_multiplier;
    OneWesolowskiCallback weso(D, iter);
    FastStorage* fast_storage = nullptr;
    std::thread vdf_worker(repeated_square, f, D, L, &weso, fast_storage, std::ref(stopped));
    Proof const proof = ProveOneWesolowski(iter, D, &weso, stopped);
    stopped = true;
    vdf_worker.join();

    bool is_valid;
    form x_init = form::generator(D);
    form y, proof_form;
    y = form::from_abd(
        ConvertBytesToInt(proof.y.data(), 0, 65),
        ConvertBytesToInt(proof.y.data(), 65, 2*65),
        D
    );
    proof_form = form::from_abd(
        ConvertBytesToInt(proof.proof.data(), 0, 65),
        ConvertBytesToInt(proof.proof.data(), 65, 2*65),
        D
    );
    VerifyWesolowskiProof(D, x_init, y, proof_form, iter, is_valid);
    std::cout << "Verify result: " << is_valid << "\n";
    return is_valid ? 0 : 1;
}
catch (std::exception const& e) {
    std::cerr << "Exception " << e.what() << '\n';
    return 1;
}
