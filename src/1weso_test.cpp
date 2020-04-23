#include "vdf.h"
#include "create_discriminant.h"
#include "verifier.h"

int segments = 7;
int thread_count = 3;

Proof CreateProof(ProverManager& pm, uint64_t iteration) {
    return pm.Prove(iteration);
}

int gcd_base_bits=50;
int gcd_128_max_iter=3;

int main() {
    debug_mode = true;
    one_weso = true;
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
    
    WesolowskiCallback weso(segments, D);

    bool stopped = false;

    uint64_t iter = 15000000;
    weso.wanted_iter = iter;
    uint64_t k, l;
    ApproximateParameters(iter, k, l);
    weso.kl = k * l;
    uint64_t space_needed = iter / (k * l) + 100;
    forms = (form*) calloc(space_needed, sizeof(form));
    forms[0] = form::generator(D);

    std::thread vdf_worker(repeated_square, f, D, L, std::ref(weso), std::ref(stopped));
    Proof proof = ProveOneWesolowski(iter, D, &weso);
    stopped = true;
    vdf_worker.join();

    bool is_valid;
    form x_init = form::generator(D);
    form y, proof_form;
    y = form::from_abd(
        ConvertBytesToInt(proof.y.data(), 0, 129),
        ConvertBytesToInt(proof.y.data(), 129, 2*129),
        D
    );
    proof_form = form::from_abd( 
        ConvertBytesToInt(proof.proof.data(), 0, 65),
        ConvertBytesToInt(proof.proof.data(), 65, 2*65),
        D
    );
    VerifyWesolowskiProof(D, x_init, y, proof_form, iter, is_valid);
    std::cout << "Verify result: " << is_valid << "\n";
}
