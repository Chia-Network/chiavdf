#include "vdf.h"
#include "verifier.h"
#include "create_discriminant.h"
#include <atomic>

int segments = 7;
int thread_count = 3;
bool stop_signal = false;

Proof CreateProof(integer D, ProverManager& pm, uint64_t iteration) {
    Proof proof = pm.Prove(iteration);
    if (!stop_signal) {
        form x = form::generator(D);
        std::vector<unsigned char> bytes;
        bytes.insert(bytes.end(), proof.y.begin(), proof.y.end());
        bytes.insert(bytes.end(), proof.proof.begin(), proof.proof.end());
        if (CheckProofOfTimeNWesolowski(D, x, bytes.data(), bytes.size(), iteration, proof.witness_type)) {
            std::cout << "Correct proof";
        } else {
            std::cout << "Incorrect proof";
            abort();
        }
        std::cout << " (iteration: " << iteration << ").\n";
    }
    return proof;
}

int gcd_base_bits=50;
int gcd_128_max_iter=3;

int main() {
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
    bool multi_proc_machine = (std::thread::hardware_concurrency() >= 16) ? true : false;

    WesolowskiCallback* weso = new FastAlgorithmCallback(segments, D, f, multi_proc_machine);
    std::cout << "Discriminant: " << D.impl << "\n";
    std::atomic<bool> stopped = false;
    fast_algorithm = true;
    FastStorage* fast_storage = NULL;
    if (multi_proc_machine) {
        fast_storage = new FastStorage((FastAlgorithmCallback*)weso);
    }
    std::thread vdf_worker(repeated_square, f, D, L, weso, fast_storage, std::ref(stopped));
    ProverManager pm(D, (FastAlgorithmCallback*)weso, fast_storage, segments, thread_count); 
    pm.start();
    for (int i = 0; i <= 30; i++) {
        std::thread t(CreateProof, D, std::ref(pm), (1 << 21) * i + 60000);
        t.detach();
    } 
    std::this_thread::sleep_for (std::chrono::seconds(300));
    stop_signal = true;
    std::cout << "Stopping everything.\n";
    pm.stop();
    stopped = true;
    vdf_worker.join();
    if (fast_storage != NULL)
        delete(fast_storage);
    delete(weso);
    exit(0);
}
