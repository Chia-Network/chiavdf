#ifndef VDF_H
#define VDF_H

#include "include.h"

#if defined(ARCH_X86) || defined(ARCH_X64)
#include <x86intrin.h>
#endif

#include "parameters.h"

#include "bit_manipulation.h"
#include "double_utility.h"
#include "integer.h"

#include "asm_main.h"

#include "vdf_original.h"

#include "vdf_new.h"
#include "picosha2.h"

#include "gpu_integer.h"
#include "gpu_integer_divide.h"

#include "gcd_base_continued_fractions.h"
//#include "gcd_base_divide_table.h"
#include "gcd_128.h"
#include "gcd_unsigned.h"

#include "gpu_integer_gcd.h"

#include "asm_types.h"

#include "threading.h"
#include "avx512_integer.h"
#include "nucomp.h"
#include "vdf_fast.h"

#include "vdf_test.h"
#include <map>
#include <algorithm>

#include <thread>
#include <future>
#include <memory>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include "proof_common.h"
#include "provers.h"
#include "util.h"
#include "callback.h"
#include "fast_storage.h"
#include <boost/asio.hpp>

#include <atomic>
#include <optional>

bool warn_on_corruption_in_production=false;

using boost::asio::ip::tcp;

struct akashnil_form {
    // y = ax^2 + bxy + y^2
    mpz_t a;
    mpz_t b;
    mpz_t c;
    // mpz_t d; // discriminant
};

const int64_t THRESH = 1UL<<31;
const int64_t EXP_THRESH = 31;

// Notifies ProverManager class each time there's a new event.
bool new_event = false;
std::condition_variable new_event_cv;
std::mutex new_event_mutex, cout_lock;

bool debug_mode = false;
bool fast_algorithm = false;
bool two_weso = false;
bool quiet_mode = false;

static inline bool chiavdf_env_truthy(const char* name) {
    const char* v = std::getenv(name);
    if (!v) return false;
    return std::strcmp(v, "1") == 0 || std::strcmp(v, "true") == 0 || std::strcmp(v, "yes") == 0 ||
           std::strcmp(v, "on") == 0;
}

static inline bool chiavdf_diag_enabled() {
    return chiavdf_env_truthy("CHIAVDF_DIAG") || chiavdf_env_truthy("CHIAVDF_VDF_TEST_STATS");
}

// `repeated_square()` and `vdf_bench` attribute some fast-path bailouts as occurring
// "after a single slow step". That label is only meaningful when the recovery path
// actually performed exactly one slow iteration (not a burst).
static inline bool chiavdf_diag_is_single_slow_step(uint64 slow_iters) {
    return slow_iters == 1;
}

#ifdef VDF_TEST
// Optional test hook: if set, `repeated_square()` will write its diagnostic counters
// here before returning. This keeps the production API unchanged while allowing
// deterministic regression tests for diagnostic attribution.
struct chiavdf_vdf_test_diag_stats {
    uint64 a_not_high_enough_fails = 0;
    uint64 a_not_high_enough_fails_after_single_slow = 0;
    uint64 a_not_high_enough_recovery_iters = 0;
    uint64 a_not_high_enough_recovery_calls = 0;
};
inline thread_local chiavdf_vdf_test_diag_stats* chiavdf_vdf_test_diag_stats_sink = nullptr;
#endif

// vdf_fast uses shared master/slave counters keyed by `square_state.pairindex`.
// The upstream chiavdf binaries run one VDF per process and hardcode `pairindex=0`.
// In embedded/multi-worker setups, multiple VDF computations can run concurrently
// in the same process; they must not share a pairindex.
static inline int vdf_fast_pairindex() {
    constexpr int kSlots = int(sizeof(master_counter) / sizeof(master_counter[0]));
    static std::atomic<int> next_slot{0};
    thread_local int slot = next_slot.fetch_add(1, std::memory_order_relaxed) % kSlots;
    return slot;
}

//always works
void repeated_square_original(vdf_original &vdfo, form& f, const integer& D, const integer& L, uint64 base, uint64 iterations, INUDUPLListener *nuduplListener) {
    vdf_original::form f_in, *f_res;
    f_in.a[0] = f.a.impl[0];
    f_in.b[0] = f.b.impl[0];
    f_in.c[0] = f.c.impl[0];
    f_res = &f_in;

    for (uint64_t i=0; i < iterations; i++) {
        f_res = vdfo.square(*f_res);

        if(nuduplListener!=NULL)
            nuduplListener->OnIteration(NL_FORM, f_res, base + i);
    }

    mpz_set(f.a.impl, f_res->a);
    mpz_set(f.b.impl, f_res->b);
    mpz_set(f.c.impl, f_res->c);
}

// thread safe; but it is only called from the main thread
void repeated_square(uint64_t iterations, form f, const integer& D, const integer& L,
    WesolowskiCallback* weso, FastStorage* fast_storage, std::atomic<bool>& stopped)
{
    #ifdef VDF_TEST
        uint64 num_calls_fast=0;
        uint64 num_iterations_fast=0;
        uint64 num_iterations_slow=0;

        // Diagnose early termination: how often the fast path bails due to `a <= L`,
        // and whether it happens right after the 1-iteration slow step.
        uint64 a_not_high_enough_fails = 0;
        uint64 a_not_high_enough_fails_after_single_slow = 0;
        uint64 a_not_high_enough_recovery_iters = 0;
        uint64 a_not_high_enough_recovery_calls = 0;
        int64 a_not_high_enough_sum_delta_bits = 0;
        int64 a_not_high_enough_sum_delta_limbs = 0;
        int a_not_high_enough_min_delta_bits = (1 << 30);
        int a_not_high_enough_max_delta_bits = -(1 << 30);
        int a_not_high_enough_min_delta_limbs = (1 << 30);
        int a_not_high_enough_max_delta_limbs = -(1 << 30);
    #endif

    uint64_t num_iterations = 0;
    uint64_t last_checkpoint = 0;
    thread_local bool prev_was_single_slow_step = false;

    while (!stopped) {
        uint64 c_checkpoint_interval=checkpoint_interval;

        #ifdef VDF_TEST
            form f_copy;
            form f_copy_3;
            bool f_copy_3_valid=false;
            if (vdf_test_correctness) {
                f_copy=f;
                c_checkpoint_interval=1;

                f_copy_3=f;
                f_copy_3_valid=square_fast_impl(f_copy_3, D, L, num_iterations);
            }
        #endif

        // Don't overshoot the requested iteration count.
        // This matters for tests and for callers that stop immediately after reaching `iters`
        // (otherwise we may run an entire extra batch before noticing we're done).
        uint64 batch_size=c_checkpoint_interval;
        if (iterations != 0) {
            if (num_iterations >= iterations) {
                break;
            }
            const uint64 remaining = iterations - num_iterations;
            if (remaining < batch_size) {
                batch_size = remaining;
            }
        }

        #ifdef ENABLE_TRACK_CYCLES
            print( "track cycles enabled; results will be wrong" );
            repeated_square_original(*weso->vdfo, f, D, L, 100); //randomize the a and b values
        #endif

        // This works single threaded
        square_state_type square_state;
        square_state.pairindex=vdf_fast_pairindex();
        square_state.entered_after_single_slow = prev_was_single_slow_step;
        prev_was_single_slow_step = false;

        uint64 actual_iterations=repeated_square_fast(square_state, f, D, L, num_iterations, batch_size, weso);

        #ifdef VDF_TEST
            ++num_calls_fast;
            if (actual_iterations!=~uint64(0)) num_iterations_fast+=actual_iterations;
        #endif

        #ifdef ENABLE_TRACK_CYCLES
            print( "track cycles actual iterations", actual_iterations );
            return; //exit the program
        #endif

        if (actual_iterations==~uint64(0)) {
            //corruption; f is unchanged. do the entire batch with the slow algorithm
            repeated_square_original(*weso->vdfo, f, D, L, num_iterations, batch_size, weso);
            actual_iterations=batch_size;

            #ifdef VDF_TEST
                num_iterations_slow+=batch_size;
            #endif

            if (warn_on_corruption_in_production) {
                print( "!!!! corruption detected and corrected !!!!" );
            }
        }

        if (actual_iterations<batch_size) {
            #ifdef VDF_TEST
                if (square_state.last_fail.reason == square_state_type::fast_fail_a_not_high_enough) {
                    ++a_not_high_enough_fails;
                    if (square_state.last_fail.after_single_slow) {
                        ++a_not_high_enough_fails_after_single_slow;
                    }
                    const int delta_bits = int(square_state.last_fail.a_bits) - int(square_state.last_fail.L_bits);
                    const int delta_limbs = int(square_state.last_fail.a_limbs) - int(square_state.last_fail.L_limbs);
                    a_not_high_enough_sum_delta_bits += delta_bits;
                    a_not_high_enough_sum_delta_limbs += delta_limbs;
                    a_not_high_enough_min_delta_bits = std::min(a_not_high_enough_min_delta_bits, delta_bits);
                    a_not_high_enough_max_delta_bits = std::max(a_not_high_enough_max_delta_bits, delta_bits);
                    a_not_high_enough_min_delta_limbs = std::min(a_not_high_enough_min_delta_limbs, delta_limbs);
                    a_not_high_enough_max_delta_limbs = std::max(a_not_high_enough_max_delta_limbs, delta_limbs);
                }
            #endif

            //the fast algorithm terminated prematurely for whatever reason. f is still valid
            //it might terminate prematurely again (e.g. gcd quotient too large), so will do one iteration of the slow algorithm
            //this will also reduce f if the fast algorithm terminated because it was too big
            uint64 slow_recovery_iters = 1;
            repeated_square_original(*weso->vdfo, f, D, L, num_iterations+actual_iterations, 1, weso);

            // If we bailed because `a <= L`, the next fast attempt often fails again immediately
            // (observed on ARM). Keep doing a few slow squarings until we re-enter the fast regime.
            if (square_state.last_fail.reason == square_state_type::fast_fail_a_not_high_enough) {
                const int delta_bits = int(square_state.last_fail.a_bits) - int(square_state.last_fail.L_bits);
                const int delta_limbs = int(square_state.last_fail.a_limbs) - int(square_state.last_fail.L_limbs);

                // Adaptive cap: the further `a` is below `L`, the more slow iterations we allow to
                // recover, to avoid a "slow1 -> fast-fail -> slow1 -> ..." loop.
                uint64 max_slow_recovery_iters = 2;
                if (delta_limbs <= -3 || delta_bits <= -256) max_slow_recovery_iters = 16;
                else if (delta_limbs <= -2 || delta_bits <= -192) max_slow_recovery_iters = 12;
                else if (delta_limbs <= -1 || delta_bits <= -128) max_slow_recovery_iters = 8;
                else if (delta_bits <= -64) max_slow_recovery_iters = 4;

                // If the planned cap wasn't enough (still `a <= L`), extend once (bounded).
                bool extended_once = false;
                constexpr uint64 kAbsoluteMaxSlowRecoveryIters = 32;
                for (;;) {
                    while (slow_recovery_iters < max_slow_recovery_iters) {
                        const bool a_nonneg = (f.a.impl->_mp_size >= 0);
                        const bool a_high_enough_now = a_nonneg && (mpz_cmpabs(f.a.impl, L.impl) > 0);
                        if (a_high_enough_now) break;
                        repeated_square_original(*weso->vdfo, f, D, L,
                                                num_iterations + actual_iterations + slow_recovery_iters,
                                                1, weso);
                        ++slow_recovery_iters;
                    }

                    const bool a_nonneg = (f.a.impl->_mp_size >= 0);
                    const bool a_high_enough_now = a_nonneg && (mpz_cmpabs(f.a.impl, L.impl) > 0);
                    if (a_high_enough_now) break;

                    if (extended_once) break;
                    extended_once = true;
                    max_slow_recovery_iters = std::min<uint64>(kAbsoluteMaxSlowRecoveryIters, max_slow_recovery_iters * 2);
                }
            }

            // Only tag "after single slow step" when it was actually a single step.
            // (When recovery is enabled we may do a burst of slow steps; the next fast batch
            // should not be attributed as "entered after single slow".)
            prev_was_single_slow_step = chiavdf_diag_is_single_slow_step(slow_recovery_iters);

            #ifdef VDF_TEST
                num_iterations_slow += slow_recovery_iters;
                if (square_state.last_fail.reason == square_state_type::fast_fail_a_not_high_enough) {
                    a_not_high_enough_recovery_iters += slow_recovery_iters;
                    ++a_not_high_enough_recovery_calls;
                }
                if (vdf_test_correctness) {
                    assert(actual_iterations==0);
                    print( "fast vdf terminated prematurely", num_iterations );
                }
            #endif

            actual_iterations += slow_recovery_iters;
        }

        num_iterations+=actual_iterations;
        weso->iterations = num_iterations;

        if (num_iterations >= last_checkpoint) {
            // n-weso specific logic.
            if (fast_algorithm) {
                if (fast_storage != NULL) {
                    fast_storage->SubmitCheckpoint(static_cast<FastAlgorithmCallback*> (weso)->y_ret, last_checkpoint);
                } else if (last_checkpoint % (1 << 16) == 0) {
                    // Notify prover event loop, we have a new segment with intermediates stored.
                    {
                        std::lock_guard<std::mutex> lk(new_event_mutex);
                        new_event = true;
                    }
                    new_event_cv.notify_all();
                }
            }

            // 2-weso specific logic.
            if (two_weso) {
                TwoWesolowskiCallback* nweso = (TwoWesolowskiCallback*) weso;
                if (num_iterations >= kSwitchIters && !nweso->LargeConstants()) {
                    uint64 round_up = (100 - num_iterations % 100) % 100;
                    if (round_up > 0) {
                        repeated_square_original(*weso->vdfo, f, D, L, num_iterations, round_up, weso);
                    }
                    num_iterations += round_up;
                    nweso->IncreaseConstants(num_iterations);
                    weso->iterations = num_iterations;
                }
                if (num_iterations >= kMaxItersAllowed - 500000) {
                    std::cout << "Maximum possible number of iterations reached!\n";
                    return ;
                }
            }

            last_checkpoint += (1 << 15);
        }

        if (iterations != 0 && num_iterations > iterations) {
            weso->iterations = num_iterations;
            break;
        }

        #ifdef VDF_TEST
            if (vdf_test_correctness) {
                form f_copy_2=f;
                weso->reduce(f_copy_2);

                repeated_square_original(*weso->vdfo, f_copy, D, L, 0, actual_iterations, nullptr);
                assert(f_copy==f_copy_2);
            }
        #endif
    }
    if (!quiet_mode) {
        // this shouldn't be needed but avoids some false positive in TSAN
        std::lock_guard<std::mutex> lk(cout_lock);
        std::cout << "VDF loop finished. Total iters: " << num_iterations << "\n" << std::flush;
    }

    #ifdef VDF_TEST
        const bool diag = (!quiet_mode) && chiavdf_diag_enabled();
        if (diag) {
            print( "fast average batch size", double(num_iterations_fast)/double(num_calls_fast) );
            print( "fast iterations per slow iteration", double(num_iterations_fast)/double(num_iterations_slow) );
            if (a_not_high_enough_fails != 0) {
                print( "fast early-terminations due to a<=L", a_not_high_enough_fails );
                print( "  after 1-iter slow step", a_not_high_enough_fails_after_single_slow );
                print( "  delta_bits (a_bits-L_bits) min/max/avg",
                       a_not_high_enough_min_delta_bits,
                       a_not_high_enough_max_delta_bits,
                       double(a_not_high_enough_sum_delta_bits) / double(a_not_high_enough_fails) );
                print( "  delta_limbs (a_limbs-L_limbs) min/max/avg",
                       a_not_high_enough_min_delta_limbs,
                       a_not_high_enough_max_delta_limbs,
                       double(a_not_high_enough_sum_delta_limbs) / double(a_not_high_enough_fails) );
                if (a_not_high_enough_recovery_calls != 0) {
                    print( "  slow recovery iters total/avg",
                           a_not_high_enough_recovery_iters,
                           double(a_not_high_enough_recovery_iters) / double(a_not_high_enough_recovery_calls) );
                }
            }
        }

        if (chiavdf_vdf_test_diag_stats_sink != nullptr) {
            chiavdf_vdf_test_diag_stats_sink->a_not_high_enough_fails = a_not_high_enough_fails;
            chiavdf_vdf_test_diag_stats_sink->a_not_high_enough_fails_after_single_slow =
                a_not_high_enough_fails_after_single_slow;
            chiavdf_vdf_test_diag_stats_sink->a_not_high_enough_recovery_iters = a_not_high_enough_recovery_iters;
            chiavdf_vdf_test_diag_stats_sink->a_not_high_enough_recovery_calls = a_not_high_enough_recovery_calls;
        }
    #endif
}

Proof ProveOneWesolowski(uint64_t iters, integer& D, form f, OneWesolowskiCallback* weso,
    std::atomic<bool>& stopped)
{
    while (!stopped && weso->iterations < iters) {
        this_thread::sleep_for(1s);
    }
    if (stopped)
        return Proof();
    Segment sg(
        /*start=*/0,
        /*length=*/iters,
        /*x=*/f,
        /*y=*/weso->result
    );
    OneWesolowskiProver prover(sg, D, weso->forms.get(), stopped);
    prover.start();
    while (!prover.IsFinished()) {
        this_thread::sleep_for(1s);
    }
    int d_bits = D.num_bits();
    std::vector<unsigned char> y_serialized;
    std::vector<unsigned char> proof_serialized;
    y_serialized = SerializeForm(weso->result, d_bits);
    form proof_form = prover.GetProof();
    proof_serialized = SerializeForm(proof_form, d_bits);
    Proof proof(y_serialized, proof_serialized);
    proof.witness_type = 0;
    if (!quiet_mode) {
        // this shouldn't be needed but avoids some false positive in TSAN
        std::lock_guard<std::mutex> lk(cout_lock);
        std::cout << "Got simple weso proof: " << proof.hex() << "\n";
    }
    return proof;
}

Proof ProveTwoWeso(integer& D, form x, uint64_t iters, uint64_t done_iterations,
    TwoWesolowskiCallback* weso, int depth, std::atomic<bool>& stop_signal)
{
    integer L=root(-D, 4);
    if (depth == 2) {
        while (!stop_signal && weso->iterations < done_iterations + iters) {
            std::this_thread::sleep_for (std::chrono::milliseconds(200));
        }
        if (stop_signal)
            return Proof();

        vdf_original vdfo_proof;
        uint64 checkpoint = (done_iterations + iters) - (done_iterations + iters) % 100;
        form y = *(weso->GetForm(checkpoint));
        repeated_square_original(vdfo_proof, y, D, L, 0, (done_iterations + iters) % 100, NULL);

        Segment sg(
            /*start=*/done_iterations,
            /*length=*/iters,
            /*x=*/x,
            /*y=*/y
        );
        TwoWesolowskiProver prover(sg, D, weso, stop_signal);
        prover.GenerateProof();

        if (stop_signal)
            return Proof();
        form proof = prover.GetProof();

        int d_bits = D.num_bits();
        std::vector<unsigned char> y_bytes = SerializeForm(y, d_bits);
        std::vector<unsigned char> proof_bytes = SerializeForm(proof, d_bits);
        Proof final_proof=Proof(y_bytes, proof_bytes);
        return final_proof;
    }

    uint64_t iterations1, iterations2;
    iterations1 = iters * 2 / 3;
    iterations1 = iterations1 - iterations1 % 100;
    iterations2 = iters - iterations1;
    while (!stop_signal && weso->iterations < done_iterations + iterations1) {
        std::this_thread::sleep_for (std::chrono::milliseconds(100));
    }
    if (stop_signal)
        return Proof();

    form y1 = *(weso->GetForm(done_iterations + iterations1));
    Segment sg(
        /*start=*/done_iterations,
        /*lenght=*/iterations1,
        /*x=*/x,
        /*y=*/y1
    );
    TwoWesolowskiProver prover(sg, D, weso, stop_signal);
    prover.start();
    Proof proof2 = ProveTwoWeso(D, y1, iterations2, done_iterations + iterations1, weso, depth + 1, stop_signal);

    while (!stop_signal && !prover.IsFinished()) {
        std::this_thread::sleep_for (std::chrono::milliseconds(100));
    }
    if (stop_signal)
        return Proof();
    form proof = prover.GetProof();

    int d_bits = D.num_bits();
    Proof final_proof;
    final_proof.y = proof2.y;

    uint8_t bytes[8];
    Int64ToBytes(bytes, iterations1);
    std::vector<uint8_t> proof_bytes(proof2.proof);

    VectorAppendArray(proof_bytes, bytes, sizeof(bytes));
    VectorAppend(proof_bytes, GetB(D, x, y1).to_bytes());
    VectorAppend(proof_bytes, SerializeForm(proof, d_bits));
    final_proof.proof = proof_bytes;
    if (depth == 0) {
        final_proof.witness_type = 2;
        std::cout << "Got 2-wesolowski proof for iteration: " << iters << ".\n";
        std::cout << "Proof: " << final_proof.hex() << "\n";
    }
    return final_proof;
}

class ProverManager {
  public:
    ProverManager(integer& D, FastAlgorithmCallback* weso, FastStorage* fast_storage, int segment_count, int max_proving_threads) {
        this->segment_count = segment_count;
        this->max_proving_threads = max_proving_threads;
        this->D = D;
        this->weso = weso;
        this->fast_storage = fast_storage;
        std::vector<Segment> tmp;
        for (int i = 0; i < segment_count; i++) {
            pending_segments.push_back(tmp);
            done_segments.push_back(tmp);
            last_appended.push_back(0);
        }
    }

    void start() {
        main_loop.emplace(&ProverManager::RunEventLoop, this);
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(new_event_mutex);
            stopped = true;
            new_event = true;
        }
        new_event_cv.notify_all();
        main_loop->join();
        std::cout << "Prover event loop finished.\n" << std::flush;

        for (int i = 0; i < provers.size(); i++) {
            provers[i].first->stop();
        }
        std::cout << "Segment provers finished.\n" << std::flush;

        proof_cv.notify_all();
        last_segment_cv.notify_all();
    }

    Proof Prove(uint64_t iteration) {
        {
            std::lock_guard<std::mutex> lkg(proof_mutex);
            pending_iters.insert(iteration);
        }
        {
            std::lock_guard<std::mutex> lkg(last_segment_mutex);
            pending_iters_last_sg.insert(iteration - iteration % (1 << 16));
        }
        std::vector<Segment> proof_segments;
        // Wait for weso.iteration to reach the last segment.
        {
            std::unique_lock<std::mutex> lk(last_segment_mutex);
            last_segment_cv.wait(lk, [this, iteration] {
                if (vdf_iteration >= iteration - iteration % (1 << 16))
                    return true;
                return stopped.load();
            });
            if (stopped) {
                return Proof();
            }
            pending_iters_last_sg.erase(iteration - iteration % (1 << 16));
            lk.unlock();
        }
        Segment last_segment;
        form y = weso->checkpoints[iteration / (1 << 16)];
        if (iteration % (1 << 16)) {
            // Recalculate everything from the checkpoint, since there is no guarantee the iter didn't arrive late.
            integer L = root(-D, 4);
            PulmarkReducer reducer;
            std::unique_ptr<form[]> intermediates(new form[(iteration % (1 << 16)) / 10 + 100]);
            for (int i = 0; i < iteration % (1 << 16); i++) {
                if (i % 10 == 0) {
                    intermediates[i / 10] = y;
                }
                nudupl_form(y, y, D, L);
                reducer.reduce(y);
                if (stopped) {
                    return Proof();
                }
            }
            Segment sg(
                /*start=*/iteration - iteration % (1 << 16),
                /*length=*/iteration % (1 << 16),
                /*x=*/weso->checkpoints[iteration / (1 << 16)],
                /*y=*/y
            );
            OneWesolowskiProver prover(sg, D, intermediates.get(), stopped);
            prover.start();
            sg.proof = prover.GetProof();
            if (stopped) {
                return Proof();
            }
            last_segment = sg;
        }
        uint64_t proved_iters = 0;
        bool valid_proof = false;
        while (!valid_proof && !stopped) {
            std::unique_lock<std::mutex> lk(proof_mutex);
            proof_cv.wait(lk, [this, iteration] {
                if (max_proving_iteration >= iteration - iteration % (1 << 16))
                    return true;
                return stopped.load();
            });
            if (stopped)
                return Proof();
            int blobs = 0;
            for (int i = segment_count - 1; i >= 0; i--) {
                uint64_t segment_size = (1LL << (16 + 2 * i));
                uint64_t position = proved_iters / segment_size;
                while (position < done_segments[i].size() && proved_iters + segment_size <= iteration) {
                    proof_segments.emplace_back(done_segments[i][position]);
                    position++;
                    proved_iters += segment_size;
                    blobs++;
                }
            }
            pending_iters.erase(iteration);
            lk.unlock();
            if (blobs > 63 || proved_iters < iteration - iteration % (1 << 16)) {
                std::cout << "Warning: Insufficient segments yet. Retrying in 1 minute\n";
                proof_segments.clear();
                proved_iters = 0;
                this_thread::sleep_for(60s);
                {
                    std::lock_guard<std::mutex> lkg(proof_mutex);
                    pending_iters.insert(iteration);
                }
            } else {
                valid_proof = true;
            }
        }
        if (!last_segment.is_empty) {
            proof_segments.emplace_back(last_segment);
        }
        // y, proof, [iters1, B1, proof1], [iters2, B2, proof2], ...
        int d_bits = D.num_bits();
        std::vector<unsigned char> y_serialized;
        std::vector<unsigned char> proof_serialized;
        // Match ClassgroupElement type from the blockchain.
        y_serialized = SerializeForm(y, d_bits);
        proof_serialized = SerializeForm(proof_segments[proof_segments.size() - 1].proof, d_bits);
        for (int i = proof_segments.size() - 2; i >= 0; i--) {
            uint8_t bytes[8];
            Int64ToBytes(bytes, proof_segments[i].length);
            VectorAppendArray(proof_serialized, bytes, sizeof(bytes));

            VectorAppend(proof_serialized, GetB(D, proof_segments[i].x, proof_segments[i].y).to_bytes());
            VectorAppend(proof_serialized, SerializeForm(proof_segments[i].proof, d_bits));
        }
        Proof proof(y_serialized, proof_serialized);
        proof.witness_type = proof_segments.size() - 1;
        uint64_t vdf_iteration = weso->iterations;
        std::cout << "Got proof for iteration: " << iteration << ". ("
                  << proof_segments.size() - 1 << "-wesolowski proof)\n";
        std::cout << "Proof: " << proof.hex() << "\n";
        if (!quiet_mode && chiavdf_diag_enabled()) {
            std::cout << "Current weso iteration: " << vdf_iteration;
            if (vdf_iteration >= iteration) {
                std::cout << ". Extra proof time (in VDF iterations): " << (vdf_iteration - iteration) << "\n";
            } else {
                // Avoid unsigned underflow spam (shows up as a huge 2^64-... number).
                std::cout << ". Extra proof time (in VDF iterations): " << 0
                          << " (weso behind by " << (iteration - vdf_iteration) << ")\n";
            }
        }
        return proof;
    }

    void RunEventLoop() {
        // this is running in a separate thread. Any member variables it
        // accesses must be one of:
        // * protected by a mutex
        // * owned entirely by this thread
        // * atomic
        const bool multi_proc_machine = (std::thread::hardware_concurrency() >= 16) ? true : false;
        bool warned = false;
        bool increased_proving = false;
        while (!stopped) {
            // Wait for some event to happen.
            {
                std::unique_lock<std::mutex> lk(new_event_mutex);
                new_event_cv.wait(lk, []{return new_event;});
                new_event = false;
                lk.unlock();
            }
            if (stopped)
                return;
            // Check if we can prove the last segment for some iteration.
            vdf_iteration = weso->iterations;
            // VDF running longer than expected, increase proving threads count.
            if (vdf_iteration >= 5e8) {
                if (!increased_proving && multi_proc_machine) {
                    std::cout << "Warning: VDF running longer than (expected) 5 minutes. Adding 2 more proving threads.\n";
                    max_proving_threads += 2;
                    increased_proving = true;
                }
            }
            bool new_last_segment = false;
            {
                std::lock_guard<std::mutex> lk(last_segment_mutex);
                if (pending_iters_last_sg.size() > 0 && vdf_iteration >= *pending_iters_last_sg.begin()) {
                    new_last_segment = true;
                }
            }
            if (new_last_segment) {
                last_segment_cv.notify_all();
            }
            uint64_t best_pending_iter = (1LL << 63);
            {
                // Protect done_segments, pending_iters and max_proving_iters.
                std::lock_guard<std::mutex> lk(proof_mutex);
                bool new_small_segment = false;
                // Check if some provers finished.
                for (int i = 0; i < provers.size(); i++) {
                    if (provers[i].first->IsFinished()) {
                        provers[i].second.proof = provers[i].first->GetProof();
                        if (debug_mode) {
                            std::cout << "Done segment: [" << provers[i].second.start
                                      << ", " << provers[i].second.start + provers[i].second.length
                                      << "]. Bucket: " << provers[i].second.GetSegmentBucket() << ".\n";
                        }
                        if (provers[i].second.length == (1 << 16)) {
                            new_small_segment = true;
                        }
                        int index = provers[i].second.GetSegmentBucket();
                        int position = provers[i].second.start / provers[i].second.length;
                        while (done_segments[index].size() <= position)
                            done_segments[index].emplace_back(Segment());
                        done_segments[index][position] = provers[i].second;
                        if (provers[i].first->IsFullyFinished()) {
                            provers.erase(provers.begin() + i);
                            i--;
                        }
                    }
                }

                // We can advance the proving iterations only when a new 2^16 segment is done.
                if (new_small_segment) {
                    uint64_t expected_proving_iters = 0;
                    if (done_segments[0].size() > 0) {
                        expected_proving_iters = done_segments[0][done_segments[0].size() - 1].start +
                                                 done_segments[0][done_segments[0].size() - 1].length;
                    }

                    if (pending_iters.size() > 0)
                        best_pending_iter = *pending_iters.begin();
                    if (pending_iters.size() > 0 && expected_proving_iters >= best_pending_iter - best_pending_iter % (1 << 16)) {
                        // Calculate the real number of iters we can prove.
                        // It needs to stick to 64-wesolowski limit.
                        // In some cases, the last 2^16 segment might be slightly inaccurate,
                        // so calculate the real number.
                        max_proving_iteration = 0;
                        int proof_blobs = 0;
                        for (int i = segment_count - 1; i >= 0 && proof_blobs < 63; i--) {
                            uint64_t segment_size = (1LL << (16 + 2 * i));
                            if (max_proving_iteration % segment_size != 0) {
                                std::cout << "Warning: segments don't have the proper sizes.\n";
                            } else {
                                int position = max_proving_iteration / segment_size;
                                while (position < done_segments[i].size() && !done_segments[i][position].is_empty) {
                                    max_proving_iteration += segment_size;
                                    proof_blobs++;
                                    position++;
                                    if (proof_blobs == 63)
                                        break;
                                }
                            }
                        }
                    }
                }
            }
            // We have all the proof for some iter, except for the small segment.
            if (max_proving_iteration >= best_pending_iter - best_pending_iter % (1 << 16)) {
                proof_cv.notify_all();
            }

            if (fast_storage != NULL) {
                intermediates_iter = fast_storage -> GetFinishedSegment();
            } else {
                intermediates_iter = vdf_iteration;
            }

            // Check if new segments have arrived, and add them as pending proof.
            for (int i = 0; i < segment_count; i++) {
                uint64_t sg_length = 1LL << (16 + 2 * i);
                while (last_appended[i] + sg_length <= intermediates_iter) {
                    if (stopped) return ;
                    Segment sg(
                        /*start=*/last_appended[i],
                        /*length=*/sg_length,
                        /*x=*/weso->checkpoints[last_appended[i] / (1 << 16)],
                        /*y=*/weso->checkpoints[(last_appended[i] + sg_length) / (1 << 16)]
                    );
                    pending_segments[i].emplace_back(sg);
                    if (!warned && pending_segments[i].size() >= kWindowSize - 2) {
                        warned = true;
                        std::cout << "Warning: VDF loop way ahead of proving loop. "
                                  << "Possible proof corruption. Please increase kWindowSize.\n";
                    }
                    last_appended[i] += sg_length;
                }
            }
            // If we have free proving threads, use them first.
            int active_provers = 0;
            for (int i = 0; i < provers.size(); i++) {
                if (provers[i].first->IsRunning())
                    active_provers++;
            }

            while (!stopped) {
                // Find the best pending/paused segment and remember where it is.
                Segment best;
                int index;
                bool new_segment;
                for (int i = 0; i < provers.size(); i++) {
                    if (!provers[i].first->IsRunning()) {
                        if (best.IsWorseThan(provers[i].second)) {
                            best = provers[i].second;
                            index = i;
                            new_segment = false;
                        }
                    }
                }
                for (int i = 0; i < segment_count; i++) {
                    if (pending_segments[i].size() > 0) {
                        if (best.IsWorseThan(pending_segments[i][0])) {
                            best = pending_segments[i][0];
                            index = i;
                            new_segment = true;
                        }
                    }
                }
                // If nothing to run, stop.
                if (best.is_empty)
                    break;
                bool spawn_best = false;
                // If we have free threads, use them.
                if (active_provers < max_proving_threads) {
                    spawn_best = true;
                    active_provers++;
                } else {
                    // Otherwise, pause one already running segment, if candidate is better.
                    Segment worst_running;
                    int worst_index;
                    for (int i = 0; i < provers.size(); i++)
                        if (provers[i].first->IsRunning()) {
                            if (worst_running.is_empty == true || provers[i].second.IsWorseThan(worst_running)) {
                                worst_running = provers[i].second;
                                worst_index = i;
                            }
                        }
                    // If the candidate is better than worst running, stop worst running and spawn candidate.
                    if (worst_running.IsWorseThan(best)) {
                        spawn_best = true;
                        provers[worst_index].first->pause();
                    }
                }
                // Best provers are already running, nothing will change until a new event happens.
                if (!spawn_best) {
                    break;
                }
                // Spawn the best segment.
                if (!new_segment) {
                    provers[index].first->resume();
                } else {
                    if (!stopped) {
                        provers.emplace_back(
                            std::make_pair(
                                std::make_unique<InterruptableProver>(best, D, weso),
                                best
                            )
                        );
                        provers[provers.size() - 1].first->start();
                        pending_segments[index].erase(pending_segments[index].begin());
                    }
                }
            }
        }
    }

  private:
    std::atomic<bool> stopped = false;
    int segment_count;
    // Maximum amount of proving threads running at once.
    int max_proving_threads;
    std::optional<std::thread> main_loop;
    FastAlgorithmCallback* weso;
    FastStorage* fast_storage;
    // The discriminant used.
    integer D;
    // Active or paused provers currently running.
    std::vector<std::pair<std::unique_ptr<InterruptableProver>, Segment>> provers;
    // Vectors of segments needing proving, for each segment length.
    std::vector<std::vector<Segment>> pending_segments;
    // For each segment length, remember the endpoint of the last segment marked as pending.
    std::vector<uint64_t> last_appended;
    // Finished segments.
    std::vector<std::vector<Segment>> done_segments;
    // Iterations that we need proof for.
    std::set<uint64_t> pending_iters;
    // Last segment beginning for our pending iters.
    std::set<uint64_t> pending_iters_last_sg;
    // Protect pending_iters and done_segments.
    std::mutex proof_mutex;
    std::mutex last_segment_mutex;
    // Notify proving threads when they are ready to prove.
    std::condition_variable proof_cv;
    std::condition_variable last_segment_cv;
    // Maximum iter that can be proved.
    uint64_t max_proving_iteration = 0;
    // Where the VDF thread is at.
    std::atomic<uint64_t> vdf_iteration{0};
    bool proof_done;
    uint64_t intermediates_iter;
};

#endif // VDF_H
