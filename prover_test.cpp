#include "vdf.h"
#include "create_discriminant.h"

int segments = 7;
int thread_count = 3;

Proof CreateProof(ProverManager& pm, uint64_t iteration) {
    return pm.Prove(iteration);
}

int gcd_base_bits=50;
int gcd_128_max_iter=3;

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
    
    WesolowskiCallback weso(segments, D);

    bool stopped = false;
    std::thread vdf_worker(repeated_square, f, D, L, std::ref(weso), std::ref(stopped));
    ProverManager pm(D, &weso, segments, thread_count); 
    pm.start();
    for (int i = 0; i <= 30; i++) {
        std::thread t(CreateProof, std::ref(pm), (1 << 21) * i + 60000);
        t.detach();
    }
    std::this_thread::sleep_for (std::chrono::seconds(300));
    std::cout << "Stopping everything.\n";
    pm.stop();
    stopped = true;
    vdf_worker.join();
}
