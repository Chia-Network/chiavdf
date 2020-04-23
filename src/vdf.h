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
#include "nucomp.h"
#include "vdf_fast.h"

#include "vdf_test.h"
#include <map>
#include <algorithm>

#include <thread>
#include <future>

#include <chrono>
#include <condition_variable>
#include "proof_common.h"

#include <boost/asio.hpp>

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
const int kWindowSize = 20;

// If 'FAST_MACHINE' is set to 1, the machine needs to have a high number 
// of CPUs. This will optimize the runtime,
// by not storing any intermediate values in main VDF worker loop.
// Other threads will come back and redo the work, this
// time storing the intermediates as well.
// For machines with small numbers of CPU, setting this to 1 will slow
// down everything, possible even stall.

const bool fast_machine = (std::thread::hardware_concurrency() >= 16) ? true : false;

bool* intermediates_stored;
bool intermediates_allocated = false;
std::map<uint64_t, form> pending_intermediates;
int intermediates_threads = 6;
std::mutex intermediates_mutex;
std::condition_variable intermediates_cv;
uint64_t intermediates_iter = 0;

form* forms;
// Notifies ProverManager class each time there's a new event.
bool new_event = false;
std::condition_variable new_event_cv;
std::mutex new_event_mutex;

bool debug_mode = false;
bool one_weso = false;

void ApproximateParameters(uint64_t T, uint64_t& l, uint64_t& k) {
    double log_memory = 23.25349666;
    double log_T = log2(T);
    l = 1;
    if (log_T - log_memory > 0.000001) {
        l = ceil(pow(2, log_memory - 20));
    }
    double intermediate = T * (double)0.6931471 / (2.0 * l);
    k = std::max(std::round(log(intermediate) - log(log(intermediate)) + 0.25), 1.0);
}

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

class WesolowskiCallback :public INUDUPLListener {
public:
    //std::atomic<int64_t> iterations = 0; // This must be intialized to zero at start
    int64_t iterations = 0;
    uint64_t kl;
    uint64_t wanted_iter;
    form result;
    int segments;
    // The intermediate values size of a 2^16 segment.
    const int bucket_size1 = 6554;
    // The intermediate values size of a >= 2^18 segment.
    const int bucket_size2 = 21846;
    // Assume provers won't be left behind by more than this # of segments.
    const int window_size = kWindowSize;

    integer D;
    integer L;

    ClassGroupContext *t;
    Reducer *reducer;

    vdf_original* vdfo;

    std::vector<int> buckets_begin;
    form* checkpoints;
    form y_ret;

    WesolowskiCallback(int segments, integer& D) {
        vdfo = new vdf_original();
        t=new ClassGroupContext(4096);
        reducer=new Reducer(*t);
        this->D = D;
        this->L = root(-D, 4);
        form f = form::generator(D);
        if (!one_weso) {
            buckets_begin.push_back(0);
            buckets_begin.push_back(bucket_size1 * window_size);
            this->segments = segments;
            for (int i = 0; i < segments - 2; i++) {
                buckets_begin.push_back(buckets_begin[buckets_begin.size() - 1] + bucket_size2 * window_size);
            }
            
            int space_needed = window_size * (bucket_size1 + bucket_size2 * (segments - 1));
            forms = (form*) calloc(space_needed, sizeof(form));
            checkpoints = (form*) calloc((1 << 18), sizeof(form));
            std::cout << "Calloc'd " << to_string((space_needed + (1 << 17)) * sizeof(form)) << " bytes\n";

            y_ret = form::generator(D);
            for (int i = 0; i < segments; i++)
                forms[buckets_begin[i]] = f;
            checkpoints[0] = f;
        }
    }

    ~WesolowskiCallback() {
        delete(vdfo);
        delete(reducer);
        delete(t);
    }

    void reduce(form& inf) {
#if 0
        // Old reduce from Sundersoft form
        inf.reduce();
#else
        // Pulmark reduce based on Akashnil reduce
        mpz_set(t->a, inf.a.impl);
        mpz_set(t->b, inf.b.impl);
        mpz_set(t->c, inf.c.impl);

        reducer->run();

        mpz_set(inf.a.impl, t->a);
        mpz_set(inf.b.impl, t->b);
        mpz_set(inf.c.impl, t->c);
#endif
    }

    int GetPosition(uint64_t exponent, int bucket) {
        uint64_t power_2 = 1LL << (16 + 2 * bucket);
        int position = buckets_begin[bucket];
        int size = (bucket == 0) ? bucket_size1 : bucket_size2;
        int kl = (bucket == 0) ? 10 : (12 * (power_2 >> 18));
        position += ((exponent / power_2) % window_size) * size;
        position += (exponent % power_2) / kl;
        return position;
    }

    form *GetForm(uint64_t exponent, int bucket) {
        return &(forms[GetPosition(exponent, bucket)]);
    }

    void SetForm(int type, void *data, form* mulf, bool reduced = false) {
        switch(type) {
            case NL_SQUARESTATE:
            {
                //cout << "NL_SQUARESTATE" << endl;
                uint64 res;

                square_state_type *square_state=(square_state_type *)data;

                if(!square_state->assign(mulf->a, mulf->b, mulf->c, res))
                    cout << "square_state->assign failed" << endl;
                break;
            }
            case NL_FORM:
            {
                //cout << "NL_FORM" << endl;

                vdf_original::form *f=(vdf_original::form *)data;

                mpz_set(mulf->a.impl, f->a);
                mpz_set(mulf->b.impl, f->b);
                mpz_set(mulf->c.impl, f->c);
                break;
            }
            default:
                cout << "Unknown case" << endl;
        }
        if (reduced) {
            reduce(*mulf);
        }
    }
    
    // We need to store: 
    // 2^16 * k + 10 * l
    // 2^(18 + 2*m) * k + 12 * 2^(2*m) * l

    void OnIteration(int type, void *data, uint64 iteration)
    {
        iteration++;

        if (!one_weso) {
            if (fast_machine) {
                if (iteration % (1 << 15) == 0) {
                    SetForm(type, data, &y_ret, /*reduced=*/true);
                }
            } else {
                // If 'FAST_MACHINE' is 0, we store the intermediates
                // right away.
                for (int i = 0; i < segments; i++) {
                    uint64_t power_2 = 1LL << (16 + 2LL * i);
                    int kl = (i == 0) ? 10 : (12 * (power_2 >> 18));
                    if ((iteration % power_2) % kl == 0) {
                        form* mulf = GetForm(iteration, i);
                        SetForm(type, data, mulf, /*reduced=*/true);
                    }
                }
            }

            if (iteration % (1 << 16) == 0) {
                form* mulf = (&checkpoints[(iteration / (1 << 16))]);
                SetForm(type, data, mulf, /*reduced=*/true);
            }
        } else {
            if (iteration <= wanted_iter) {
                if (iteration % kl == 0) {
                    uint64_t pos = iteration / kl;
                    form* mulf = &forms[pos];
                    SetForm(type, data, mulf, /*reduced=*/true);
                }
                if (iteration == wanted_iter) {
                    SetForm(type, data, &result, /*reduced=*/true);
                }
            }
        }
    }
};

// In case 'FAST_MACHINE' = 1, these threads will come back
// and recalculate intermediates for the values VDF loop produced.

void AddIntermediates(uint64_t iter) {
    int bucket = iter / (1 << 16);
    int subbucket = 0;
    if (iter % (1 << 16))
        subbucket = 1;
    bool arrived_segment = false;
    bool has_event = false;
    {
        intermediates_stored[2 * bucket + subbucket] = true;
        if (intermediates_stored[2 * bucket] == true &&
            intermediates_stored[2 * bucket + 1] == true)
                has_event = true;
    }
    if (has_event) {
        {
            std::lock_guard<std::mutex> lk(new_event_mutex);
            new_event = true;
        }
        new_event_cv.notify_all();
    }
}

void CalculateIntermediatesInner(form& y, uint64_t iter_begin, WesolowskiCallback& weso, bool& stopped) {
    PulmarkReducer reducer;
    integer& D = weso.D;
    integer& L = weso.L;
    int segments = weso.segments;
    for (uint64_t iteration = iter_begin; iteration < iter_begin + (1 << 15); iteration++) {
        for (int i = 0; i < segments; i++) {
            uint64_t power_2 = 1LL << (16 + 2 * i);
            int kl = (i == 0) ? 10 : (12 * (power_2 >> 18));
            if ((iteration % power_2) % kl == 0) {
                if (stopped) return;
                form* mulf = weso.GetForm(iteration, i);
                weso.SetForm(NL_FORM, &y, mulf);
            }
        }
        nudupl_form(y, y, D, L);
        reducer.reduce(y);   
    }
    AddIntermediates(iter_begin);
}

void CalculateIntermediatesThread(WesolowskiCallback& weso, bool& stopped) {
    while (!stopped) {
        {
            std::unique_lock<std::mutex> lk(intermediates_mutex);
            while (pending_intermediates.empty() && !stopped) {
                intermediates_cv.wait(lk);
            }
            if (!stopped) {
                uint64_t iter_begin = (*pending_intermediates.begin()).first;
                form y = (*pending_intermediates.begin()).second;
                pending_intermediates.erase(pending_intermediates.begin());
                lk.unlock();
                CalculateIntermediatesInner(y, iter_begin, weso, stopped); 
            }
        }
    }
}

// thread safe; but it is only called from the main thread
void repeated_square(form f, const integer& D, const integer& L, WesolowskiCallback &weso, bool& stopped) {
    #ifdef VDF_TEST
        uint64 num_calls_fast=0;
        uint64 num_iterations_fast=0;
        uint64 num_iterations_slow=0;
    #endif

    uint64_t num_iterations = 0;
    uint64_t last_checkpoint = 0;

    std::vector<std::thread> threads;
    if (!one_weso && fast_machine) {
        intermediates_stored = new bool[(1 << 19)];
        for (int i = 0; i < (1 << 19); i++)
            intermediates_stored[i] = 0;
        intermediates_allocated = true;

        for (int i = 0; i < intermediates_threads; i++) {
            threads.push_back(std::thread(CalculateIntermediatesThread, std::ref(weso), std::ref(stopped)));
        }
    }

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
            repeated_square_original(*weso.vdfo, f, D, L, 100); //randomize the a and b values
        #endif

        // This works single threaded
        square_state_type square_state;
        square_state.pairindex=0;

        uint64 actual_iterations=repeated_square_fast(square_state, f, D, L, num_iterations, batch_size, &weso);

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
            repeated_square_original(*weso.vdfo, f, D, L, num_iterations, batch_size, &weso);
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
            repeated_square_original(*weso.vdfo, f, D, L, num_iterations+actual_iterations, 1, &weso);

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
        if (!one_weso) {
            if (num_iterations >= last_checkpoint) {
                if (fast_machine) {
                    if (last_checkpoint % (1 << 16) == 0) {
                        weso.iterations = num_iterations;
                    }
                    // Since checkpoint_interval is at most 10000, we'll have 
                    // at most 1 intermediate checkpoint.
                    // This needs readjustment if that constant is changed.
                    {
                        std::lock_guard<std::mutex> lk(intermediates_mutex);
                        pending_intermediates[last_checkpoint] = weso.y_ret;
                    }
                    intermediates_cv.notify_all();
                    last_checkpoint += (1 << 15);
                } else {
                    weso.iterations = num_iterations;
                    // Notify prover event loop, we have a new segment with intermediates stored.
                    {
                        std::lock_guard<std::mutex> lk(new_event_mutex);
                        new_event = true;
                    }
                    new_event_cv.notify_all();
                    last_checkpoint += (1 << 16);
                }
            }
        } else {
            weso.iterations = num_iterations;
            if (num_iterations >= weso.wanted_iter)
                break;
        }

        #ifdef VDF_TEST
            if (vdf_test_correctness) {
                form f_copy_2=f;
                weso.reduce(f_copy_2);

                repeated_square_original(&weso.vdfo, f_copy, D, L, actual_iterations);
                assert(f_copy==f_copy_2);
            }
        #endif
    }

    std::cout << "Final number of iterations: " << num_iterations << "\n";
    
    if (!one_weso && fast_machine) {
        intermediates_cv.notify_all();
        for (int i = 0; i < threads.size(); i++) {
            threads[i].join();
        }
        delete[] intermediates_stored;
    }

    #ifdef VDF_TEST
        print( "fast average batch size", double(num_iterations_fast)/double(num_calls_fast) );
        print( "fast iterations per slow iteration", double(num_iterations_fast)/double(num_iterations_slow) );
    #endif
}

uint64_t GetBlock(uint64_t i, uint64_t k, uint64_t T, integer& B) {
    integer res = FastPow(2, T - k * (i + 1), B);
    mpz_mul_2exp(res.impl, res.impl, k);
    res = res / B;
    auto res_vector = res.to_vector();
    return res_vector[0];
}

std::string BytesToStr(const std::vector<unsigned char> &in)
{
    std::vector<unsigned char>::const_iterator from = in.cbegin();
    std::vector<unsigned char>::const_iterator to = in.cend();
    std::ostringstream oss;
    for (; from != to; ++from)
       oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(*from);
    return oss.str();
}

struct Proof {
    Proof() {

    }

    Proof(std::vector<unsigned char> y, std::vector<unsigned char> proof) {
        this->y = y;
        this->proof = proof;
    }

    string hex() {
        std::vector<unsigned char> bytes(y);
        bytes.insert(bytes.end(), proof.begin(), proof.end());
        return BytesToStr(bytes);
    }

    std::vector<unsigned char> y;
    std::vector<unsigned char> proof;
    uint8_t witness_type;
};

struct Segment {
    uint64_t start;
    uint64_t length;
    form x;
    form y;
    form proof;
    bool is_empty;

    Segment() {
        is_empty = true;
    }

    Segment(uint64_t start, uint64_t length, form& x, form& y) {        
        this->start = start;
        this->length = length;
        this->x = x;
        this->y = y;
        is_empty = false;
    }

    bool IsWorseThan(Segment& other) {
        if (is_empty) {
            if (!other.is_empty)
                return true;
            return false;
        }
        if (length > other.length)
            return true;
        if (length < other.length)  
            return false;
        return start > other.start;
    }

    int GetSegmentBucket() {
        uint64_t c_length = length;
        length >>= 16;
        int index = 0;
        while (length > 1) {
            index++;
            if (length == 2 || length == 3) {
                std::cout << "Warning: Invalid segment length.\n";
            }
            length >>= 2;
        }
        length = c_length;
        return index;
    }
};

class Prover {
  public:
    Prover(Segment& segm, integer& D, WesolowskiCallback* weso) {
        this->y = segm.y;
        this->x_init = segm.x;
        this->D = D;
        this->done_iterations = segm.start;
        this->num_iterations = segm.length;
        if (!one_weso) {
            this->bucket = segm.GetSegmentBucket();
            if (segm.length <= (1 << 16))
                this->k = 10;
            else
                this->k = 12;
            if (segm.length <= (1 << 18)) {
                this->l = 1;
            } else {
                this->l = (segm.length >> 18);
            }
        } else {
            ApproximateParameters(weso->wanted_iter, this->k, this->l);
        }
        this->weso = weso;
        is_paused = false;
        is_finished = false;
    }

    void SetIntermediates(std::vector<form>* intermediates) {
        this->intermediates = intermediates;
        have_intermediates = true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(m);
            is_finished = true;
            if (is_paused) {
                is_paused = false;
            }
        }
        cv.notify_one();
    }

    void start() {
        std::thread t([=] { GenerateProof(); });
        t.detach();
    }

    void pause() {
        std::lock_guard<std::mutex> lk(m);
        is_paused = true;
    }

    void resume() {
        {
            std::lock_guard<std::mutex> lk(m);
            is_paused = false;
        }
        cv.notify_one();
    }

    bool IsRunning() {
        return !is_paused;
    }

    bool IsFinished() {
        return is_finished;
    }

    form GetProof() {
        return proof;
    }

    void GenerateProof() {
        auto t1 = std::chrono::high_resolution_clock::now();

        PulmarkReducer reducer;

        integer B = GetB(D, x_init, y);
        integer L=root(-D, 4);
        form id;
        try {
            id = form::identity(D);
        } catch(std::exception& e) {
            std::cout << "Warning: Could not create identity: " << e.what() << "\n";
        }
        uint64_t k1 = k / 2;
        uint64_t k0 = k - k1;

        form x = id;

        for (int64_t j = l - 1; j >= 0; j--) {
            x = FastPowFormNucomp(x, D, integer(1 << k), L, reducer);

            std::vector<form> ys((1 << k));
            for (uint64_t i = 0; i < (1 << k); i++)
                ys[i] = id;

            form *tmp;
            for (uint64_t i = 0; i < ceil(1.0 * num_iterations / (k * l)); i++) {
                if (num_iterations >= k * (i * l + j + 1)) {
                    uint64_t b = GetBlock(i*l + j, k, num_iterations, B);
                    if (is_finished) return ;
                    if (!one_weso) {
                        if (!have_intermediates) {
                            tmp = weso->GetForm(done_iterations + i * k * l, bucket);
                        } else {
                            tmp = &(intermediates->at(i));
                        }
                    } else {
                        tmp = &forms[i];
                    } 
                    nucomp_form(ys[b], ys[b], *tmp, D, L);
                }
                if (is_finished) {
                    return ;
                }
                while (is_paused) {
                    std::unique_lock<std::mutex> lk(m);
                    cv.wait(lk);
                    lk.unlock();
                }
            }

            for (uint64_t b1 = 0; b1 < (1 << k1); b1++) {
                form z = id;
                for (uint64_t b0 = 0; b0 < (1 << k0); b0++) {
                    nucomp_form(z, z, ys[b1 * (1 << k0) + b0], D, L);
                    if (is_finished) {
                        return;
                    }
                    while (is_paused) {
                        std::unique_lock<std::mutex> lk(m);
                        cv.wait(lk);
                        lk.unlock();
                    }
                }
                z = FastPowFormNucomp(z, D, integer(b1 * (1 << k0)), L, reducer);
                nucomp_form(x, x, z, D, L);
            }

            for (uint64_t b0 = 0; b0 < (1 << k0); b0++) {
                form z = id;
                for (uint64_t b1 = 0; b1 < (1 << k1); b1++) {
                    nucomp_form(z, z, ys[b1 * (1 << k0) + b0], D, L);
                    if (is_finished) {
                        return ;
                    }
                    while (is_paused) {
                        std::unique_lock<std::mutex> lk(m);
                        cv.wait(lk);
                        lk.unlock();
                    }
                }
                z = FastPowFormNucomp(z, D, integer(b0), L, reducer);
                nucomp_form(x, x, z, D, L);
            }
        }

        reducer.reduce(x);

        auto t2 = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        proof = x;
        is_finished = true;
        if (!one_weso) {
            // Notify event loop a proving thread is free.
            {
                std::lock_guard<std::mutex> lk(new_event_mutex);
                new_event = true;
            }
            new_event_cv.notify_one();
        }
    }

  private:
    form y;
    form x_init;
    form proof;
    integer D;
    uint64_t num_iterations;
    uint64_t k;
    uint64_t done_iterations;
    uint64_t l;
    int bucket;
    WesolowskiCallback* weso;
    bool is_paused;
    bool is_finished;
    std::condition_variable cv;
    std::mutex m;
    std::vector<form>* intermediates;
    bool have_intermediates = false;
};

Proof ProveOneWesolowski(uint64_t iters, integer& D, WesolowskiCallback* weso) {
    while (weso->iterations < iters) {
        this_thread::sleep_for(3s);
    }
    form f = form::generator(D);
    Segment sg(
        /*start=*/0,
        /*length=*/iters,
        /*x=*/f,
        /*y=*/weso->result
    );
    Prover prover(sg, D, weso);
    prover.start();
    while (!prover.IsFinished()) {
        this_thread::sleep_for(3s);
    }
    int int_size = (D.num_bits() + 16) >> 4;
    std::vector<unsigned char> y_serialized;
    std::vector<unsigned char> proof_serialized;
    y_serialized = SerializeForm(weso->result, 129);
    form proof_form = prover.GetProof();
    proof_serialized = SerializeForm(proof_form, int_size);
    Proof proof(y_serialized, proof_serialized);
    proof.witness_type = 0;
    std::cout << "Got simple weso proof: " << proof.hex() << "\n";
    return proof;
}

class ProverManager {
  public:
    ProverManager(integer& D, WesolowskiCallback* weso, int segment_count, int max_proving_threads) {
        this->segment_count = segment_count;
        this->max_proving_threads = max_proving_threads;
        this->D = D;
        this->weso = weso;
        std::vector<Segment> tmp;
        for (int i = 0; i < segment_count; i++) {
            pending_segments.push_back(tmp);
            done_segments.push_back(tmp);
            last_appended.push_back(0);
        }
    }

    void start() {
        std::thread t([=] {RunEventLoop(); });
        t.detach();
    }

    void stop() {        
        stopped = true;
        for (int i = 0; i < provers.size(); i++)
            provers[i].first->stop();
        proof_cv.notify_all();
        last_segment_cv.notify_all();
        {
            std::lock_guard<std::mutex> lk(new_event_mutex);
            new_event = true;
        }
        new_event_cv.notify_all();
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
                return stopped;
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
            std::vector<form> intermediates(iteration % (1 << 16) / 10 + 1);

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
            // TODO: stop this prover as well in case stop signal arrives.
            Prover prover(sg, D, weso);
            prover.SetIntermediates(&intermediates);
            prover.GenerateProof();
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
                return stopped;
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
        // y, proof, [iters1, y1, proof1], [iters2, y2, proof2], ...
        int int_size = (D.num_bits() + 16) >> 4;
        std::vector<unsigned char> y_serialized;
        std::vector<unsigned char> proof_serialized;
        // Match ClassGroupElement type from the blockchain.
        y_serialized = SerializeForm(y, 129);
        proof_serialized = SerializeForm(proof_segments[proof_segments.size() - 1].proof, int_size);
        for (int i = proof_segments.size() - 2; i >= 0; i--) {
            std::vector<unsigned char> tmp = ConvertIntegerToBytes(integer(proof_segments[i].length), 8);
            proof_serialized.insert(proof_serialized.end(), tmp.begin(), tmp.end());
            tmp.clear();
            tmp = SerializeForm(proof_segments[i].y, int_size);
            proof_serialized.insert(proof_serialized.end(), tmp.begin(), tmp.end());
            tmp.clear();
            tmp = SerializeForm(proof_segments[i].proof, int_size);
            proof_serialized.insert(proof_serialized.end(), tmp.begin(), tmp.end());
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
        const bool c_fast_machine = (std::thread::hardware_concurrency() >= 16) ? true : false;
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
                if (!increased_proving && c_fast_machine) {
                    std::cout << "Warning: VDF running longer than (expected) 5 minutes. Adding 3 more proving threads.\n";
                    max_proving_threads += 4;
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
                        provers.erase(provers.begin() + i);
                        i--;
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

            if (c_fast_machine) {
                if (intermediates_allocated) {
                    while (intermediates_stored[2 * (intermediates_iter / (1 << 16))] == true &&
                           intermediates_stored[2 * (intermediates_iter / (1 << 16)) + 1] == true) {
                                intermediates_iter += (1 << 16);
                    }
                }
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
                    provers.emplace_back(
                        std::make_pair(
                            std::make_unique<Prover>(best, D, weso),                            
                            best
                        )
                    );
                    provers[provers.size() - 1].first->start();
                    pending_segments[index].erase(pending_segments[index].begin());
                }
            }
        }
    }

  private:
    bool stopped = false;
    int segment_count;
    // Maximum amount of proving threads running at once.
    int max_proving_threads;
    WesolowskiCallback* weso;
    // The discriminant used.
    integer D;
    // Active or paused provers currently running.
    std::vector<std::pair<std::unique_ptr<Prover>, Segment>> provers;
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
    uint64_t vdf_iteration = 0;
    bool proof_done;
};
