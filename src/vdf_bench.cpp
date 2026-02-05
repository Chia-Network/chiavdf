#include "include.h"
#include "bit_manipulation.h"
#include "double_utility.h"
#include "parameters.h"
#include "asm_main.h"
#include "integer.h"
#include "vdf.h"
#include "vdf_new.h"
#include "nucomp.h"
#include "picosha2.h"
#include "proof_common.h"

#include "threading.h"
#include "avx512_integer.h"
#include "vdf_fast.h"
#include "create_discriminant.h"

#include <cstdlib>
#include <cstring>

#define CH_SIZE 32

int gcd_base_bits = 50;
int gcd_128_max_iter = 3;

#if defined(ARCH_ARM)
#include <atomic>
extern std::atomic<uint64_t> gcd_unsigned_arm_bad_order_fallbacks;
extern std::atomic<uint64_t> gcd_unsigned_arm_gcd128_fail_fallbacks;
extern std::atomic<uint64_t> gcd_unsigned_arm_exact_division_repairs;
#endif

static void usage(const char *progname)
{
    fprintf(stderr, "Usage: %s {square_asm|square|square_vdf|discr} N [--recover-a]\n", progname);
    fprintf(stderr, "  --recover-a : enable adaptive slow recovery when fast path bails due to a<=L\n");
    fprintf(stderr, "  (compat)    : an optional 'N'/'Y' argument is accepted and ignored\n");
    fprintf(stderr, "Diagnostics:\n");
    fprintf(stderr, "  Set CHIAVDF_DIAG=1 (or CHIAVDF_VDF_TEST_STATS=1) to print extended optimization stats.\n");
}

static bool env_truthy(const char* name) {
    const char* v = std::getenv(name);
    if (!v) return false;
    return std::strcmp(v, "1") == 0 || std::strcmp(v, "true") == 0 || std::strcmp(v, "yes") == 0 ||
           std::strcmp(v, "on") == 0;
}

static void print_diag_build_info() {
    // Keep this simple and stable for log parsing.
    fprintf(stderr, "diag_build: arch=");
#if defined(ARCH_ARM)
    fprintf(stderr, "arm");
#elif defined(ARCH_X64)
    fprintf(stderr, "x64");
#elif defined(ARCH_X86)
    fprintf(stderr, "x86");
#else
    fprintf(stderr, "unknown");
#endif
    fprintf(stderr, " vdf_mode=%d", int(VDF_MODE));

    fprintf(stderr, " tsan=");
#if defined(CHIAVDF_TSAN) || defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
    fprintf(stderr, "1");
#else
    fprintf(stderr, "0");
#endif

    fprintf(stderr, " asan=");
#if defined(__SANITIZE_ADDRESS__)
    fprintf(stderr, "1");
#else
    fprintf(stderr, "0");
#endif

    fprintf(stderr, " ubsan=");
#if defined(__SANITIZE_UNDEFINED__)
    fprintf(stderr, "1");
#else
    fprintf(stderr, "0");
#endif

    fprintf(stderr, "\n");
}

int main(int argc, char **argv)
{
    assert(is_vdf_test); //assertions should be disabled in VDF_MODE==0
    init_gmp();
    set_rounding_mode();
    init_gcd_params_for_cpu();

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }
    int iters = atoi(argv[2]);
    bool enable_recover_a = false;
    // Extended diagnostics are *only* enabled via env vars (so CI / normal runs stay quiet).
    const bool diag = env_truthy("CHIAVDF_DIAG") ||
                      env_truthy("CHIAVDF_VDF_TEST_STATS");
    for (int a = 3; a < argc; a++) {
        if (!std::strcmp(argv[a], "--recover-a")) {
            enable_recover_a = true;
        } else if (!std::strcmp(argv[a], "N") || !std::strcmp(argv[a], "Y")) {
            // Backwards compatibility: some scripts call `./vdf_bench square_asm N ...`.
            // Diagnostics remain strictly env-gated; this flag is ignored.
            continue;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[a]);
            usage(argv[0]);
            return 1;
        }
    }
    if (diag) {
        print_diag_build_info();
        fprintf(stderr, "diag_cmd: mode=%s iters=%d recover_a=%d\n", argv[1], iters, enable_recover_a ? 1 : 0);
    }
    auto D = integer("-141140317794792668862943332656856519378482291428727287413318722089216448567155737094768903643716404517549715385664163360316296284155310058980984373770517398492951860161717960368874227473669336541818575166839209228684755811071416376384551902149780184532086881683576071479646499601330824259260645952517205526679");

    form y = form::generator(D);
    integer L = root(-D, 4);
    int i, n_slow = 0;
    int n_slow_ab_valid = 0;
    int n_slow_a_high_enough = 0;
    int n_slow_other = 0;
    int n_fast_bails = 0;
    int n_fast_bails_a_high_enough = 0;
    int n_fast_bails_ab_valid = 0;
    int n_fast_bails_gcd_failed = 0;
    int n_fast_bails_other = 0;
    int n_a_high_enough_fail_after_single_slow = 0;
    int64_t sum_a_high_enough_delta_bits = 0;
    int64_t sum_a_high_enough_delta_limbs = 0;
    int min_a_high_enough_delta_bits = (1 << 30);
    int max_a_high_enough_delta_bits = -(1 << 30);
    int min_a_high_enough_delta_limbs = (1 << 30);
    int max_a_high_enough_delta_limbs = -(1 << 30);
    int n_recovery_calls = 0;
    int n_recovery_iters = 0;
    PulmarkReducer reducer;
    bool is_comp = true, is_asm = false;
    bool print_abc = true;

    struct NullWeso : public WesolowskiCallback {
        explicit NullWeso(integer& D) : WesolowskiCallback(D) {}
        void OnIteration(int, void*, uint64_t) override {}
    };


    auto t1 = std::chrono::high_resolution_clock::now();
    if (!strcmp(argv[1], "square_asm")) {
#if defined(ARCH_ARM)
        // ARM does not use the phased pipeline; treat `square_asm` as a NUDUPL benchmark for
        // script compatibility (many callers hardcode `square_asm`).
        for (i = 0; i < iters; i++) {
            nudupl_form(y, y, D, L);
            if (__GMP_ABS(y.a.impl->_mp_size) > 8) {
                reducer.reduce(y);
            }
        }
#else
        is_asm = true;
        bool prev_was_single_slow_step = false;
        for (i = 0; i < iters; ) {
            square_state_type sq_state;
            sq_state.pairindex = 0;
            sq_state.entered_after_single_slow = prev_was_single_slow_step;
            prev_was_single_slow_step = false;
            uint64_t done;

            // Mirror the key "fast path" preconditions from `square_state_type::phase_0_master`
            // so we can attribute `n_slow` to specific guards.
            const int max_bits_ab = max_bits_base + num_extra_bits_ab;
            const bool ab_valid =
                (y.a.num_bits() <= max_bits_ab) &&
                (y.b.num_bits() <= max_bits_ab) &&
                (y.a.impl->_mp_size >= 0);
            // Mirror `phase_0_master`'s condition: a > L (using limb-count fast path + exact compare on tie).
            const int a_limbs = __GMP_ABS(y.a.impl->_mp_size);
            const int L_limbs = __GMP_ABS(L.impl->_mp_size);
            const bool a_high_enough =
                (a_limbs > L_limbs) || (a_limbs == L_limbs && mpz_cmpabs(y.a.impl, L.impl) > 0);

            done = repeated_square_fast(sq_state, y, D, L, 0, iters - i, NULL);
            if (!done) {
                ++n_fast_bails;
                switch (sq_state.last_fail.reason) {
                    case square_state_type::fast_fail_ab_invalid: ++n_fast_bails_ab_valid; break;
                    case square_state_type::fast_fail_a_not_high_enough: ++n_fast_bails_a_high_enough; break;
                    case square_state_type::fast_fail_gcd_failed: ++n_fast_bails_gcd_failed; break;
                    default: ++n_fast_bails_other; break;
                }
                if (sq_state.last_fail.reason == square_state_type::fast_fail_a_not_high_enough) {
                    if (sq_state.last_fail.after_single_slow) {
                        ++n_a_high_enough_fail_after_single_slow;
                    }
                    const int delta_bits = int(sq_state.last_fail.a_bits) - int(sq_state.last_fail.L_bits);
                    const int delta_limbs = int(sq_state.last_fail.a_limbs) - int(sq_state.last_fail.L_limbs);
                    sum_a_high_enough_delta_bits += delta_bits;
                    sum_a_high_enough_delta_limbs += delta_limbs;
                    min_a_high_enough_delta_bits = std::min(min_a_high_enough_delta_bits, delta_bits);
                    max_a_high_enough_delta_bits = std::max(max_a_high_enough_delta_bits, delta_bits);
                    min_a_high_enough_delta_limbs = std::min(min_a_high_enough_delta_limbs, delta_limbs);
                    max_a_high_enough_delta_limbs = std::max(max_a_high_enough_delta_limbs, delta_limbs);
                }
                int slow_iters = 1;
                nudupl_form(y, y, D, L);
                reducer.reduce(y);

                if (enable_recover_a && sq_state.last_fail.reason == square_state_type::fast_fail_a_not_high_enough) {
                    const int delta_bits = int(sq_state.last_fail.a_bits) - int(sq_state.last_fail.L_bits);
                    const int delta_limbs = int(sq_state.last_fail.a_limbs) - int(sq_state.last_fail.L_limbs);

                    int max_recovery = 2;
                    if (delta_limbs <= -3 || delta_bits <= -256) max_recovery = 16;
                    else if (delta_limbs <= -2 || delta_bits <= -192) max_recovery = 12;
                    else if (delta_limbs <= -1 || delta_bits <= -128) max_recovery = 8;
                    else if (delta_bits <= -64) max_recovery = 4;

                    bool extended_once = false;
                    const int absolute_max_recovery = 32;
                    for (;;) {
                        while (slow_iters < max_recovery) {
                            const bool a_nonneg = (y.a.impl->_mp_size >= 0);
                            const bool a_high_enough_now = a_nonneg && (mpz_cmpabs(y.a.impl, L.impl) > 0);
                            if (a_high_enough_now) break;
                            nudupl_form(y, y, D, L);
                            reducer.reduce(y);
                            ++slow_iters;
                        }

                        const bool a_nonneg = (y.a.impl->_mp_size >= 0);
                        const bool a_high_enough_now = a_nonneg && (mpz_cmpabs(y.a.impl, L.impl) > 0);
                        if (a_high_enough_now) break;

                        if (extended_once) break;
                        extended_once = true;
                        max_recovery = std::min(absolute_max_recovery, max_recovery * 2);
                    }
                    ++n_recovery_calls;
                    n_recovery_iters += slow_iters;
                }

                i += slow_iters;
                n_slow += slow_iters;
                // Only tag "after single slow step" when it was actually a single step.
                // (When recovery is enabled we may do a burst of slow steps; the next fast batch
                // should not be attributed as "entered after single slow".)
                prev_was_single_slow_step = (slow_iters == 1);
                if (!ab_valid) {
                    n_slow_ab_valid += slow_iters;
                } else if (!a_high_enough) {
                    n_slow_a_high_enough += slow_iters;
                } else {
                    n_slow_other += slow_iters;
                }
            } else if (done == ~0ULL) {
                printf("Fail\n");
                break;
            } else {
                i += done;
            }
        }
#endif
    } else if (!strcmp(argv[1], "square")) {
        for (i = 0; i < iters; i++) {
            nudupl_form(y, y, D, L);
            if (__GMP_ABS(y.a.impl->_mp_size) > 8) {
                reducer.reduce(y);
            }
        }
    } else if (!strcmp(argv[1], "square_vdf")) {
        // Benchmark the same squaring path used by the main loop (`repeated_square`), including
        // the ARM-specific fast/slow selection and recovery behavior.
        NullWeso weso(D);
        FastStorage* fast_storage = nullptr;
        std::atomic<bool> stopped{false};
        repeated_square(/*iterations=*/(uint64_t)iters, y, D, L, &weso, fast_storage, stopped);
        // `repeated_square()` takes its `form` argument by value, so we don't have a cheap way to
        // capture the final form here without adding overhead that would skew IPS.
        // Avoid printing the (unchanged) `a/b/c` values for this mode.
        print_abc = false;
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
        if (diag) {
            printf("WARNING: too few iterations, results will be inaccurate!\n");
        }
        duration = 1;
    }

    if (is_comp) {
        if (diag) {
            printf("Time: %d ms; ", duration);
            if (is_asm) {
                printf("n_slow: %d (ab_valid: %d, a_high_enough: %d, other: %d); ",
                       n_slow, n_slow_ab_valid, n_slow_a_high_enough, n_slow_other);
                if (enable_recover_a) {
                    printf("recovery: on; ");
                }
            }
            if (is_asm && enable_recover_a) {
                printf("fast_bails: %d (ab_valid:%d a_high_enough:%d gcd:%d other:%d); recovery_calls: %d; recovery_slow_iters: %d; ",
                       n_fast_bails,
                       n_fast_bails_ab_valid,
                       n_fast_bails_a_high_enough,
                       n_fast_bails_gcd_failed,
                       n_fast_bails_other,
                       n_recovery_calls,
                       n_recovery_iters);
            }
            if (is_asm && n_fast_bails_a_high_enough != 0) {
                // Delta stats are per fast-bail (not per slow iteration).
                printf("a<=L after single slow: %d; a<=L delta_bits min/max/avg: %d/%d/%.2f; delta_limbs min/max/avg: %d/%d/%.2f; ",
                       n_a_high_enough_fail_after_single_slow,
                       min_a_high_enough_delta_bits,
                       max_a_high_enough_delta_bits,
                       double(sum_a_high_enough_delta_bits) / double(n_fast_bails_a_high_enough),
                       min_a_high_enough_delta_limbs,
                       max_a_high_enough_delta_limbs,
                       double(sum_a_high_enough_delta_limbs) / double(n_fast_bails_a_high_enough));
            }

#if defined(ARCH_ARM)
            if (is_asm) {
                const uint64_t bad_order = gcd_unsigned_arm_bad_order_fallbacks.load(std::memory_order_relaxed);
                const uint64_t gcd128_fail = gcd_unsigned_arm_gcd128_fail_fallbacks.load(std::memory_order_relaxed);
                const uint64_t repairs = gcd_unsigned_arm_exact_division_repairs.load(std::memory_order_relaxed);
                printf("gcd_bad_order: %llu; gcd128_fail: %llu; gcd_repairs: %llu; ",
                       (unsigned long long)bad_order,
                       (unsigned long long)gcd128_fail,
                       (unsigned long long)repairs);
            }
#endif
        }

        if (diag) {
            printf("speed: %d.%dK ips\n", iters/duration, iters*10/duration % 10);
        } else {
            // Default output: keep it minimal and stable for scripts/CI logs.
            printf("%d.%dK ips\n", iters/duration, iters*10/duration % 10);
        }
        if (diag && print_abc) {
            printf("a = %s\n", y.a.to_string().c_str());
            printf("b = %s\n", y.b.to_string().c_str());
            printf("c = %s\n", y.c.to_string().c_str());
        }
    } else {
        if (diag) {
            printf("Time: %d ms; speed: %d.%d ms/discr\n", duration, duration/iters, duration*10/iters % 10);
        } else {
            printf("%d.%d ms/discr\n", duration/iters, duration*10/iters % 10);
        }
    }
    return 0;
}

#if defined(ARCH_ARM)
#include "asm_arm_fallback_impl.inc"
#endif
