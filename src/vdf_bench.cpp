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

static bool perf_trace_enabled()
{
    const char* value = std::getenv("CHIAVDF_PERF_TRACE");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
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
    const double ips = (1000.0 * static_cast<double>(iters)) / static_cast<double>(duration);
    const bool perf_trace = perf_trace_enabled();
    printf("Time: %d ms; ", duration);
    if (is_comp) {
        if (is_asm)
            printf("n_slow: %d; ", n_slow);

        printf("speed: %d.%dK ips\n", iters/duration, iters*10/duration % 10);
        if (perf_trace) {
            // PERF_INVESTIGATION_TEMP: machine-readable perf output for CI parsing.
            const int avx2 = hasAVX2() ? 1 : 0;
            const int avx512_ifma = enable_avx512_ifma.load(std::memory_order_relaxed) ? 1 : 0;
            printf("PERF_INVESTIGATION_TEMP mode=%s iters=%d duration_ms=%d ips=%.3f n_slow=%d avx2=%d avx512_ifma=%d\n",
                argv[1], iters, duration, ips, n_slow, avx2, avx512_ifma);
        }
        printf("a = %s\n", y.a.to_string().c_str());
        printf("b = %s\n", y.b.to_string().c_str());
        printf("c = %s\n", y.c.to_string().c_str());
    } else {
        printf("speed: %d.%d ms/discr\n", duration/iters, duration*10/iters % 10);
    }
    return 0;
}
