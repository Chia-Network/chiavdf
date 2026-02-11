#include "vdf.h"
#include "create_discriminant.h"
#include "verifier.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

int segments = 7;
int thread_count = 3;

int gcd_base_bits=50;
int gcd_128_max_iter=3;

namespace {
std::atomic<uint64_t> g_agent_log_seq{0};

const char* AgentDebugLogPath() {
    const char* env_path = std::getenv("CHIAVDF_AGENT_DEBUG_LOG");
    if (env_path != nullptr && env_path[0] != '\0') {
        return env_path;
    }
    return "/Users/hoffmang/src/chiavdf/.cursor/debug.log";
}

bool AgentDebugShouldMirror(const char* hypothesis_id) {
    const char* mirror_all = std::getenv("CHIAVDF_AGENT_DEBUG_MIRROR_ALL");
    if (mirror_all != nullptr && mirror_all[0] == '1') {
        return true;
    }
    return std::strcmp(hypothesis_id, "H14") == 0 ||
           std::strcmp(hypothesis_id, "H16") == 0 ||
           std::strcmp(hypothesis_id, "H18") == 0 ||
           std::strcmp(hypothesis_id, "H19") == 0 ||
           std::strcmp(hypothesis_id, "H22") == 0 ||
           std::strcmp(hypothesis_id, "H28") == 0 ||
           std::strcmp(hypothesis_id, "H29") == 0 ||
           std::strcmp(hypothesis_id, "H36") == 0 ||
           std::strcmp(hypothesis_id, "H37") == 0;
}

void AgentDebugLog(const char* run_id, const char* hypothesis_id, const char* location, const char* message, const std::string& data_json) {
    std::ofstream out(AgentDebugLogPath(), std::ios::app);
    if (!out.is_open()) {
        return;
    }
    const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    const uint64_t seq = ++g_agent_log_seq;
    out << "{\"id\":\"log_" << ts << "_" << seq
        << "\",\"timestamp\":" << ts
        << ",\"runId\":\"" << run_id
        << "\",\"hypothesisId\":\"" << hypothesis_id
        << "\",\"location\":\"" << location
        << "\",\"message\":\"" << message
        << "\",\"data\":" << data_json
        << "}\n";
    if (AgentDebugShouldMirror(hypothesis_id)) {
        std::cerr << "AGENTLOG "
                  << "{\"id\":\"log_" << ts << "_" << seq
                  << "\",\"timestamp\":" << ts
                  << ",\"runId\":\"" << run_id
                  << "\",\"hypothesisId\":\"" << hypothesis_id
                  << "\",\"location\":\"" << location
                  << "\",\"message\":\"" << message
                  << "\",\"data\":" << data_json
                  << "}\n";
    }
}
}

void CheckProof(integer& D, Proof& proof, uint64_t iteration) {
    form x = form::generator(D);
    std::vector<unsigned char> bytes;
    bytes.insert(bytes.end(), proof.y.begin(), proof.y.end());
    bytes.insert(bytes.end(), proof.proof.begin(), proof.proof.end());
    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H3",
        "2weso_test.cpp:CheckProof:before_verify",
        "CheckProof start",
        std::string("{\"iteration\":") + std::to_string(iteration) +
            ",\"y_size\":" + std::to_string(proof.y.size()) +
            ",\"proof_size\":" + std::to_string(proof.proof.size()) +
            ",\"witness_type\":" + std::to_string(proof.witness_type) + "}");
    // #endregion
    if (CheckProofOfTimeNWesolowski(D, DEFAULT_ELEMENT, bytes.data(), bytes.size(), iteration, 1024, proof.witness_type)) {
        // #region agent log
        AgentDebugLog(
            "pre-fix",
            "H3",
            "2weso_test.cpp:CheckProof:verify_true",
            "CheckProof returned true",
            std::string("{\"iteration\":") + std::to_string(iteration) + "}");
        // #endregion
        std::cout << "Correct proof\n";
    } else {
        // #region agent log
        AgentDebugLog(
            "pre-fix",
            "H3",
            "2weso_test.cpp:CheckProof:verify_false",
            "CheckProof returned false",
            std::string("{\"iteration\":") + std::to_string(iteration) + "}");
        // #endregion
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
    // #region agent log
    AgentDebugLog(
        "post-fix",
        "H37",
        "2weso_test.cpp:main:instrumentation_version_marker",
        "Instrumentation marker for artifact/version validation",
        "{\"instrumentation_version\":\"H54\",\"source\":\"2weso_test\"}");
    // #endregion

    assert(is_vdf_test); //assertions should be disabled in VDF_MODE==0
    init_gmp();
    debug_mode = true;
    const bool has_avx2 = hasAVX2();
    if(has_avx2)
    {
      gcd_base_bits=63;
      gcd_128_max_iter=2;
    }
    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H5",
        "2weso_test.cpp:main:post_cpu_init",
        "CPU feature and gcd parameters set",
        std::string("{\"iter_multiplier\":") + std::to_string(iter_multiplier) +
            ",\"has_avx2\":" + (has_avx2 ? "true" : "false") +
            ",\"gcd_base_bits\":" + std::to_string(gcd_base_bits) +
            ",\"gcd_128_max_iter\":" + std::to_string(gcd_128_max_iter) + "}");
    // #endregion
    std::vector<uint8_t> challenge_hash({0, 0, 1, 2, 3, 3, 4, 4});
    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H6",
        "2weso_test.cpp:main:before_create_discriminant",
        "About to call CreateDiscriminant",
        "{\"bits\":1024,\"challenge_hash_size\":8}");
    // #endregion
    integer D = CreateDiscriminant(challenge_hash, 1024);
    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H6",
        "2weso_test.cpp:main:after_create_discriminant",
        "CreateDiscriminant returned",
        std::string("{\"discriminant_sign\":") + (D < 0 ? "-1" : "1") + "}");
    // #endregion

    if (getenv( "warn_on_corruption_in_production" )!=nullptr) {
        warn_on_corruption_in_production=true;
    }
    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H7",
        "2weso_test.cpp:main:after_warn_env",
        "warn_on_corruption flag evaluated",
        std::string("{\"warn_on_corruption_in_production\":") +
            (warn_on_corruption_in_production ? "true" : "false") + "}");
    // #endregion
    set_rounding_mode();
    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H8",
        "2weso_test.cpp:main:after_set_rounding_mode",
        "set_rounding_mode completed",
        "{}");
    // #endregion

    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H9",
        "2weso_test.cpp:main:before_root_generator",
        "About to compute L and generator",
        "{}");
    // #endregion
    integer L=root(-D, 4);
    form f=form::generator(D);
    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H9",
        "2weso_test.cpp:main:after_root_generator",
        "Computed L and generator",
        "{}");
    // #endregion

    std::atomic<bool> stopped = false;
    fast_algorithm = false;
    two_weso = true;
    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H10",
        "2weso_test.cpp:main:before_callback_ctor",
        "About to construct TwoWesolowskiCallback",
        "{}");
    // #endregion
    const uint64_t max_test_iteration = 100 * iter_multiplier;
    TwoWesolowskiCallback weso(D, f);
    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H10",
        "2weso_test.cpp:main:after_callback_ctor",
        "Constructed TwoWesolowskiCallback",
        "{}");
    // #endregion
    FastStorage* fast_storage = NULL;
    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H1",
        "2weso_test.cpp:main:before_worker_start",
        "Starting repeated_square worker thread",
        std::string("{\"fast_algorithm\":") + (fast_algorithm ? "true" : "false") +
            ",\"two_weso\":" + (two_weso ? "true" : "false") + "}");
    // #endregion
    std::thread vdf_worker(repeated_square, 0, f, D, L, &weso, fast_storage, std::ref(stopped));
    // #region agent log
    AgentDebugLog(
        "pre-fix",
        "H1",
        "2weso_test.cpp:main:after_worker_start",
        "Worker thread started",
        std::string("{\"worker_joinable\":") + (vdf_worker.joinable() ? "true" : "false") + "}");
    // #endregion
    auto stop_worker = [&]() {
        // #region agent log
        AgentDebugLog(
            "pre-fix",
            "H4",
            "2weso_test.cpp:main:stop_worker_enter",
            "stop_worker invoked",
            std::string("{\"worker_joinable_before\":") + (vdf_worker.joinable() ? "true" : "false") + "}");
        // #endregion
        stopped = true;
        if (vdf_worker.joinable()) {
            vdf_worker.join();
        }
        // #region agent log
        AgentDebugLog(
            "pre-fix",
            "H4",
            "2weso_test.cpp:main:stop_worker_exit",
            "stop_worker completed",
            std::string("{\"worker_joinable_after\":") + (vdf_worker.joinable() ? "true" : "false") + "}");
        // #endregion
    };
    try {
        auto run_test = [&](uint64_t iteration) {
            // #region agent log
            AgentDebugLog(
                "pre-fix",
                "H2",
                "2weso_test.cpp:main:before_prove",
                "Calling ProveTwoWeso",
                std::string("{\"iteration\":") + std::to_string(iteration) + "}");
            // #endregion
            Proof proof = ProveTwoWeso(D, f, iteration, 0, &weso, 0, stopped);
            // #region agent log
            AgentDebugLog(
                "pre-fix",
                "H2",
                "2weso_test.cpp:main:after_prove",
                "ProveTwoWeso returned",
                std::string("{\"iteration\":") + std::to_string(iteration) +
                    ",\"y_size\":" + std::to_string(proof.y.size()) +
                    ",\"proof_size\":" + std::to_string(proof.proof.size()) +
                    ",\"witness_type\":" + std::to_string(proof.witness_type) + "}");
            // #endregion
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
