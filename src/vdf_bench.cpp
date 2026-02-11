#include "include.h"
#include <chrono>
#include "bit_manipulation.h"
#include "double_utility.h"
#include "parameters.h"
#include "integer.h"
#include "alloc.hpp"
#include "vdf_new.h"
#include "nucomp.h"
#include "picosha2.h"
#include "proof_common.h"

#if (defined(ARCH_X86) || defined(ARCH_X64)) && !defined(CHIA_DISABLE_ASM)
#include "asm_main.h"
#include "threading.h"
#include "avx512_integer.h"
#include "vdf_fast.h"
#endif
#include "create_discriminant.h"

#include <cstdlib>

#define CH_SIZE 32

static inline void agent_debug_log_ndjson_vdf_bench(
    const char* hypothesis_id,
    const char* location,
    const char* message,
    const std::string& data_json,
    const char* run_id = "pre-fix"
) {
    const long long ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    std::ofstream out("/Users/hoffmang/src/chiavdf/.cursor/debug.log", std::ios::app);
    if (!out.good()) {
        return;
    }
    out << "{\"id\":\"log_" << ts_ms << "_" << hypothesis_id
        << "\",\"timestamp\":" << ts_ms
        << ",\"runId\":\"" << run_id
        << "\",\"hypothesisId\":\"" << hypothesis_id
        << "\",\"location\":\"" << location
        << "\",\"message\":\"" << message
        << "\",\"data\":" << data_json
        << "}\n";
}

static void usage(const char *progname)
{
    fprintf(stderr, "Usage: %s {square_asm|square|discr} N\n", progname);
}

int main(int argc, char **argv)
{
    assert(is_vdf_test); //assertions should be disabled in VDF_MODE==0
    init_gmp();
    set_rounding_mode();
    // #region agent log
#ifdef CHIA_DISABLE_ASM
    constexpr int agent_asm_enabled = 0;
#else
    constexpr int agent_asm_enabled = 1;
#endif
    const bool agent_has_avx2 = hasAVX2();
    std::cerr << "AGENTDBG H60 vdf_bench_start"
              << " asm_enabled=" << agent_asm_enabled
              << " has_avx2=" << (agent_has_avx2 ? 1 : 0)
              << " argc=" << argc
              << "\n";
    agent_debug_log_ndjson_vdf_bench(
        "H60",
        "src/vdf_bench.cpp:main:start",
        "vdf_bench_runtime_caps",
        std::string("{\"asm_enabled\":") + std::to_string(agent_asm_enabled) +
            ",\"has_avx2\":" + std::to_string(agent_has_avx2 ? 1 : 0) +
            ",\"argc\":" + std::to_string(argc) + "}",
        "pre-fix"
    );
    // #endregion

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }
    int iters = atoi(argv[2]);
    auto D = integer("-141140317794792668862943332656856519378482291428727287413318722089216448567155737094768903643716404517549715385664163360316296284155310058980984373770517398492951860161717960368874227473669336541818575166839209228684755811071416376384551902149780184532086881683576071479646499601330824259260645952517205526679");

    form y = form::generator(D);
    integer L = root(-D, 4);
    int i, n_slow = 0;
    PulmarkReducer reducer;
    bool is_comp = true, is_asm = false;


    auto t1 = std::chrono::high_resolution_clock::now();
    if (!strcmp(argv[1], "square_asm")) {
        // #region agent log
        std::cerr << "AGENTDBG H61 vdf_bench_square_asm_enter"
                  << " iters=" << iters
                  << " asm_enabled=" << agent_asm_enabled
                  << "\n";
        agent_debug_log_ndjson_vdf_bench(
            "H61",
            "src/vdf_bench.cpp:main:square_asm",
            "entered_square_asm_mode",
            std::string("{\"iters\":") + std::to_string(iters) +
                ",\"asm_enabled\":" + std::to_string(agent_asm_enabled) + "}",
            "pre-fix"
        );
        // #endregion
        is_asm = true;
#if (defined(ARCH_X86) || defined(ARCH_X64)) && !defined(CHIA_DISABLE_ASM)
        for (i = 0; i < iters; ) {
            square_state_type sq_state;
            sq_state.pairindex = 0;
            uint64_t done;

            done = repeated_square_fast(sq_state, y, D, L, 0, iters - i, NULL);
            if (!done) {
                nudupl_form(y, y, D, L);
                reducer.reduce(y);
                i++;
                n_slow++;
            } else if (done == ~0ULL) {
                printf("Fail\n");
                break;
            } else {
                i += done;
            }
        }
#else
        // On non-x86 architectures we don't build the phased/asm pipeline.
        // Keep script compatibility by treating `square_asm` as a NUDUPL benchmark.
        for (i = 0; i < iters; i++) {
            nudupl_form(y, y, D, L);
            if (__GMP_ABS(y.a.impl->_mp_size) > 8) {
                reducer.reduce(y);
            }
        }
        is_asm = false;
#endif
    } else if (!strcmp(argv[1], "square")) {
        for (i = 0; i < iters; i++) {
            nudupl_form(y, y, D, L);
            if (__GMP_ABS(y.a.impl->_mp_size) > 8) {
                reducer.reduce(y);
            }
        }
    } else if (!strcmp(argv[1], "discr")) {
        uint8_t ch[CH_SIZE];
        std::vector<uint8_t> ch_vec;

        is_comp = false;
        for (i = 0; i < CH_SIZE; i++) {
            ch[i] = i;
        }

        ch_vec = std::vector<uint8_t>(ch, ch + CH_SIZE);

        for (i = 0; i < iters; i++) {
            ch_vec[i % CH_SIZE] += 1;
            integer discr = CreateDiscriminant(ch_vec, 1024);
        }
    } else {
        fprintf(stderr, "Unknown command\n");
        usage(argv[0]);
        return 1;
    }

    if (is_comp)
        reducer.reduce(y);

    auto t2 = std::chrono::high_resolution_clock::now();
    int duration = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    if (!duration) {
        printf("WARNING: too few iterations, results will be inaccurate!\n");
        duration = 1;
    }
    printf("Time: %d ms; ", duration);
    if (is_comp) {
        if (is_asm)
            printf("n_slow: %d; ", n_slow);

        printf("speed: %d.%dK ips\n", iters/duration, iters*10/duration % 10);
        printf("a = %s\n", y.a.to_string().c_str());
        printf("b = %s\n", y.b.to_string().c_str());
        printf("c = %s\n", y.c.to_string().c_str());
    } else {
        printf("speed: %d.%d ms/discr\n", duration/iters, duration*10/iters % 10);
    }
    return 0;
}
