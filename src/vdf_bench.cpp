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
    fprintf(stderr, "Usage: %s {square_asm|square|discr} N [--recover-a]\n", progname);
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
    const bool enable_recover_a = (argc >= 4 && !strcmp(argv[3], "--recover-a"));
    auto D = integer("-141140317794792668862943332656856519378482291428727287413318722089216448567155737094768903643716404517549715385664163360316296284155310058980984373770517398492951860161717960368874227473669336541818575166839209228684755811071416376384551902149780184532086881683576071479646499601330824259260645952517205526679");

    form y = form::generator(D);
    integer L = root(-D, 4);
    int i, n_slow = 0;
    int n_slow_ab_valid = 0;
    int n_slow_a_high_enough = 0;
    int n_slow_other = 0;
    int n_fast_bails = 0;
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


    auto t1 = std::chrono::high_resolution_clock::now();
    if (!strcmp(argv[1], "square_asm")) {
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
                prev_was_single_slow_step = true;
                if (!ab_valid) {
                    n_slow_ab_valid++;
                } else if (!a_high_enough) {
                    n_slow_a_high_enough++;
                } else {
                    n_slow_other++;
                }
            } else if (done == ~0ULL) {
                printf("Fail\n");
                break;
            } else {
                i += done;
            }
        }
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
            printf("n_slow: %d (ab_valid: %d, a_high_enough: %d, other: %d); ",
                   n_slow, n_slow_ab_valid, n_slow_a_high_enough, n_slow_other);
        if (is_asm && enable_recover_a) {
            printf("recovery: on; fast_bails: %d; recovery_calls: %d; recovery_slow_iters: %d; ",
                   n_fast_bails, n_recovery_calls, n_recovery_iters);
        }
        if (is_asm && n_slow_a_high_enough != 0) {
            printf("a<=L after slow: %d; a<=L delta_bits min/max/avg: %d/%d/%.2f; delta_limbs min/max/avg: %d/%d/%.2f; ",
                   n_a_high_enough_fail_after_single_slow,
                   min_a_high_enough_delta_bits,
                   max_a_high_enough_delta_bits,
                   double(sum_a_high_enough_delta_bits) / double(n_slow_a_high_enough),
                   min_a_high_enough_delta_limbs,
                   max_a_high_enough_delta_limbs,
                   double(sum_a_high_enough_delta_limbs) / double(n_slow_a_high_enough));
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

        printf("speed: %d.%dK ips\n", iters/duration, iters*10/duration % 10);
        printf("a = %s\n", y.a.to_string().c_str());
        printf("b = %s\n", y.b.to_string().c_str());
        printf("c = %s\n", y.c.to_string().c_str());
    } else {
        printf("speed: %d.%d ms/discr\n", duration/iters, duration*10/iters % 10);
    }
    return 0;
}

#if defined(ARCH_ARM)
#include "asm_arm_fallback_impl.inc"
#endif
