#include "vdf.h"
#include "verifier.h"
#include "create_discriminant.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <chrono>

int segments = 7;
int thread_count = 3;
std::atomic<bool> stop_signal{false};

static bool env_truthy(const char* name)
{
    return env_flag(name);
}

Proof CreateProof(integer D, ProverManager& pm, uint64_t iteration) {
    Proof proof = pm.Prove(iteration);
    if (!stop_signal) {
        form x = form::generator(D);
        std::vector<unsigned char> bytes;
        bytes.insert(bytes.end(), proof.y.begin(), proof.y.end());
        bytes.insert(bytes.end(), proof.proof.begin(), proof.proof.end());
        if (CheckProofOfTimeNWesolowski(D, DEFAULT_ELEMENT, bytes.data(), bytes.size(), iteration, 1024, proof.witness_type)) {
            std::cout << "Correct proof";
        } else {
            std::cout << "Incorrect proof\n";
            std::exit(1);
        }
        std::cout << " (iteration: " << iteration << ").\n";
    }
    return proof;
}

int gcd_base_bits=50;
int gcd_128_max_iter=3;

int main() {
    assert(is_vdf_test); //assertions should be disabled in VDF_MODE==0
    init_gmp();
    if(hasAVX2())
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
    bool multi_proc_machine = (std::thread::hardware_concurrency() >= 16) ? true : false;

    WesolowskiCallback* weso = new FastAlgorithmCallback(segments, D, f, multi_proc_machine);
    std::cout << "Discriminant: " << D.to_string() << "\n";
    std::atomic<bool> stopped = false;
    fast_algorithm = true;
    FastStorage* fast_storage = NULL;
    if (multi_proc_machine) {
        fast_storage = new FastStorage((FastAlgorithmCallback*)weso);
    }
    std::thread vdf_worker(repeated_square, 0, f, D, L, weso, fast_storage, std::ref(stopped));
    ProverManager pm(D, (FastAlgorithmCallback*)weso, fast_storage, segments, thread_count);
    pm.start();
    std::vector<std::thread> threads;

    // This binary is used by CI as a correctness test. Historically it also served as a 5-minute
    // soak/stress test; that dominates the wall-clock runtime of the "all tests" run.
    //
    // Default behavior: run the historical long/soak test.
    // Fast/CI-friendly mode: set `CHIAVDF_PROVER_TEST_FAST=1` to run just a few proofs and exit.
    const bool fast_mode = env_truthy("CHIAVDF_PROVER_TEST_FAST");
    const bool is_ci = env_flag("CI") || env_flag("GITHUB_ACTIONS");

    if (!fast_mode) {
        for (int i = 0; i <= 30; i++) {
            threads.emplace_back(CreateProof, D, std::ref(pm), (1ULL << 21) * uint64_t(i) + 60000);
        }
        std::this_thread::sleep_for(std::chrono::seconds(300));
    } else {
        // Keep iterations small enough to complete quickly on CI runners.
        const int max_i = is_ci ? 3 : 6;
        for (int i = 0; i < max_i; i++) {
            threads.emplace_back(CreateProof, D, std::ref(pm), (1ULL << 18) * uint64_t(i) + 60000);
        }
        for (auto& t : threads) t.join();
        threads.clear();
    }

    stop_signal = true;
    std::cout << "Stopping everything.\n";
    pm.stop();
    stopped = true;
    for (auto& t : threads) t.join();
    vdf_worker.join();
    if (fast_storage != NULL)
        delete(fast_storage);
    delete(weso);
}
