#ifndef VDF_H
#define VDF_H

#include "include.h"

#include <x86intrin.h>

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
std::mutex new_event_mutex;

bool debug_mode = false;
bool fast_algorithm = false;
bool two_weso = false;

//always works
void repeated_square_original(vdf_original &vdfo, form& f, const integer& D, const integer& L, uint64 base, uint64 iterations, INUDUPLListener *nuduplListener) {
    vdf_original::form f_in,*f_res;
    f_in.a[0]=f.a.impl[0];
    f_in.b[0]=f.b.impl[0];
    f_in.c[0]=f.c.impl[0];
    f_res=&f_in;

    for (uint64_t i=0; i < iterations; i++) {
        f_res = vdfo.square(*f_res);

        if(nuduplListener!=NULL)
            nuduplListener->OnIteration(NL_FORM,f_res,base+i);
    }

    mpz_set(f.a.impl, f_res->a);
    mpz_set(f.b.impl, f_res->b);
    mpz_set(f.c.impl, f_res->c);
}

// thread safe; but it is only called from the main thread
void repeated_square(form f, const integer& D, const integer& L,
    WesolowskiCallback* weso, FastStorage* fast_storage, std::atomic<bool>& stopped)
{
    #ifdef VDF_TEST
        uint64 num_calls_fast=0;
        uint64 num_iterations_fast=0;
        uint64 num_iterations_slow=0;
    #endif

    uint64_t num_iterations = 0;
    uint64_t last_checkpoint = 0;

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

        uint64 batch_size=c_checkpoint_interval;

        #ifdef ENABLE_TRACK_CYCLES
            print( "track cycles enabled; results will be wrong" );
            repeated_square_original(*weso->vdfo, f, D, L, 100); //randomize the a and b values
        #endif

        // This works single threaded
        square_state_type square_state;
        square_state.pairindex=0;

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
            //the fast algorithm terminated prematurely for whatever reason. f is still valid
            //it might terminate prematurely again (e.g. gcd quotient too large), so will do one iteration of the slow algorithm
            //this will also reduce f if the fast algorithm terminated because it was too big
            repeated_square_original(*weso->vdfo, f, D, L, num_iterations+actual_iterations, 1, weso);

#ifdef VDF_TEST
                ++num_iterations_slow;
                if (vdf_test_correctness) {
                    assert(actual_iterations==0);
                    print( "fast vdf terminated prematurely", num_iterations );
                }
            #endif

            ++actual_iterations;
        }

        num_iterations+=actual_iterations;
        if (num_iterations >= last_checkpoint) {
            weso->iterations = num_iterations;

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

        #ifdef VDF_TEST
            if (vdf_test_correctness) {
                form f_copy_2=f;
                weso->reduce(f_copy_2);

                repeated_square_original(*weso->vdfo, f_copy, D, L, actual_iterations);
                assert(f_copy==f_copy_2);
            }
        #endif
    }

    std::cout << "VDF loop finished. Total iters: " << num_iterations << "\n" << std::flush;
    #ifdef VDF_TEST
        print( "fast average batch size", double(num_iterations_fast)/double(num_calls_fast) );
        print( "fast iterations per slow iteration", double(num_iterations_fast)/double(num_iterations_slow) );
    #endif
}

Proof ProveOneWesolowski(uint64_t iters, integer& D, form f, OneWesolowskiCallback* weso,
    std::atomic<bool>& stopped)
{
    while (weso->iterations < iters) {
        this_thread::sleep_for(1s);
    }
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
    std::cout << "Got simple weso proof: " << proof.hex() << "\n";
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
        std::cout << "Current weso iteration: " << vdf_iteration
                  << ". Extra proof time (in VDF iterations): " << vdf_iteration - iteration
                  << "\n";
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
