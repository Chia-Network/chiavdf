#include "vdf.h"
#include "create_discriminant.h"
#include "verifier.h"

#include <atomic>
#include <cassert>
#ifdef _WIN32
#include <windows.h>
#endif

int segments = 7;
int thread_count = 3;

Proof CreateProof(ProverManager& pm, uint64_t iteration) {
    return pm.Prove(iteration);
}

int gcd_base_bits=50;
int gcd_128_max_iter=3;

#ifdef _WIN32
#ifdef GENERATE_ASM_TRACKING_DATA
static LONG WINAPI dump_asm_tracking_on_crash(EXCEPTION_POINTERS*) {
    std::cerr << "AGENTDBG H16 seh_unhandled_exception\n";
    for (int i = 0; i < num_asm_tracking_data; ++i) {
        if (!asm_tracking_data_comments[i]) {
            continue;
        }
        if (asm_tracking_data[i] == 0) {
            continue;
        }
        std::cerr << "AGENTDBG H16 asm_track idx=" << i
                  << " count=" << asm_tracking_data[i]
                  << " label=" << asm_tracking_data_comments[i] << "\n";
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static LONG CALLBACK agent_vectored_exception_logger(EXCEPTION_POINTERS* info) {
    if (info == nullptr || info->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const auto* rec = info->ExceptionRecord;
    const void* addr = rec->ExceptionAddress;
    const uintptr_t crash_ip = reinterpret_cast<uintptr_t>(addr);
    const uintptr_t avx2_gcd_unsigned_ip = reinterpret_cast<uintptr_t>(&asm_code::asm_avx2_func_gcd_unsigned);
    const uintptr_t cel_gcd_unsigned_ip = reinterpret_cast<uintptr_t>(&asm_code::asm_cel_func_gcd_unsigned);
    const uintptr_t avx2_gcd_128_ip = reinterpret_cast<uintptr_t>(&asm_code::asm_avx2_func_gcd_128);
    const uintptr_t cel_gcd_128_ip = reinterpret_cast<uintptr_t>(&asm_code::asm_cel_func_gcd_128);
    const uintptr_t module_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    std::cerr << "AGENTDBG H24 veh_exception"
              << " code=0x" << std::hex << static_cast<unsigned long>(rec->ExceptionCode) << std::dec
              << " flags=0x" << std::hex << static_cast<unsigned long>(rec->ExceptionFlags) << std::dec
              << " thread_id=" << GetCurrentThreadId()
              << " address=" << addr
              << " params=" << rec->NumberParameters
              << " module_base=0x" << std::hex << module_base
              << " ip_rva=0x" << (crash_ip - module_base)
              << " d_avx2_gcd_unsigned=0x" << (crash_ip - avx2_gcd_unsigned_ip)
              << " d_cel_gcd_unsigned=0x" << (crash_ip - cel_gcd_unsigned_ip)
              << " d_avx2_gcd_128=0x" << (crash_ip - avx2_gcd_128_ip)
              << " d_cel_gcd_128=0x" << (crash_ip - cel_gcd_128_ip)
              << std::dec
              << "\n";
#ifdef GENERATE_ASM_TRACKING_DATA
    int agent_dumped = 0;
    for (int i = 0; i < num_asm_tracking_data; ++i) {
        if (!asm_tracking_data_comments[i] || asm_tracking_data[i] == 0) {
            continue;
        }
        std::cerr << "AGENTDBG H25 asm_track_at_crash idx=" << i
                  << " count=" << asm_tracking_data[i]
                  << " label=" << asm_tracking_data_comments[i]
                  << "\n";
        ++agent_dumped;
        if (agent_dumped >= 80) {
            break;
        }
    }
#endif
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char const* argv[]) try
{
    // allow setting the multiplier for the number of iterations to test on the
    // command line. This can be used to run smaller and faster tests on CI,
    // specifically with instrumented binaries that aren't as fast
    std::uint64_t const iter_multiplier = (argc > 1)
        ? std::stoull(argv[1]) : 1000000;

    assert(is_vdf_test); //assertions should be disabled in VDF_MODE==0
    init_gmp();
#ifdef _WIN32
    void* agent_veh_handle = AddVectoredExceptionHandler(1, agent_vectored_exception_logger);
    (void)agent_veh_handle;
#ifdef GENERATE_ASM_TRACKING_DATA
    SetUnhandledExceptionFilter(dump_asm_tracking_on_crash);
#endif
#endif
    debug_mode = true;
    const bool has_avx2 = hasAVX2();
    // #region agent log
    std::cerr << "AGENTDBG H1 init iter_multiplier=" << iter_multiplier
              << " has_avx2=" << (has_avx2 ? 1 : 0) << "\n";
    // #endregion
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
    std::cerr << "AGENTDBG H3 before_worker iter=" << iter << "\n";
    // #endregion
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
    std::cerr << "AGENTDBG H3 before_prove iter=" << iter << "\n";
    // #endregion
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
    std::cerr << "AGENTDBG H4 after_prove proof_y_size=" << proof.y.size()
              << " proof_proof_size=" << proof.proof.size() << "\n";
    // #endregion
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
    std::cerr << "AGENTDBG H5 after_join iter=" << iter << "\n";
    // #endregion
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
