
// If 'FAST_MACHINE' is set to 1, the machine needs to have a high number 
// of CPUs. This will optimize the runtime,
// by not storing any intermediate values in main VDF worker loop.
// Other threads will come back and redo the work, this
// time storing the intermediates as well.
// For machines with small numbers of CPU, setting this to 1 will slow
// down everything, possible even stall.
#ifndef FAST_STORAGE_H
#define FAST_STORAGE_H

#include "vdf_new.h"

extern bool new_event;
extern std::mutex new_event_mutex;
extern std::condition_variable new_event_cv;

class FastStorage {
  public:
    FastStorage(FastAlgorithmCallback* weso) {
        stopped = false;
        this->weso = weso;
        intermediates_stored = new bool[(1 << 19)];
        for (int i = 0; i < (1 << 19); i++)
            intermediates_stored[i] = 0;

        for (int i = 0; i < intermediates_threads; i++) {
            storage_threads.push_back(std::thread([=] {CalculateIntermediatesThread(); }));
        }
    }

    ~FastStorage() {
        {
            std::lock_guard<std::mutex> lk(intermediates_mutex);
            stopped = true;
        }
        intermediates_cv.notify_all();
        for (int i = 0; i < storage_threads.size(); i++) {
            storage_threads[i].join();
        }
        delete[] intermediates_stored;
        std::cout << "Fast storage fully stopped.\n" << std::flush;
    }

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

    void CalculateIntermediatesInner(form y, uint64_t iter_begin) {
        PulmarkReducer reducer;
        integer& D = weso->D;
        integer& L = weso->L;
        int segments = weso->segments;
        for (uint64_t iteration = iter_begin; iteration < iter_begin + (1 << 15); iteration++) {
            for (int i = 0; i < segments; i++) {
                uint64_t power_2 = 1LL << (16 + 2 * i);
                int kl = (i == 0) ? 10 : (12 * (power_2 >> 18));
                if ((iteration % power_2) % kl == 0) {
                    if (stopped) return;
                    form* mulf = weso->GetForm(iteration, i);
                    weso->SetForm(NL_FORM, &y, mulf, /*reduced=*/false);
                }
            }
            nudupl_form(y, y, D, L);
            reducer.reduce(y);   
        }
        AddIntermediates(iter_begin);
    }

    void SubmitCheckpoint(form y_ret, uint64_t iteration) {
        {
            std::lock_guard<std::mutex> lk(intermediates_mutex);
            pending_intermediates[iteration] = y_ret;
        }
        intermediates_cv.notify_all();
    }

    uint64_t GetFinishedSegment() {
        while (intermediates_stored[2 * (intermediates_iter / (1 << 16))] == true &&
               intermediates_stored[2 * (intermediates_iter / (1 << 16)) + 1] == true) {
                    intermediates_iter += (1 << 16);
                }
        return intermediates_iter;
    } 

    void CalculateIntermediatesThread() {
        while (!stopped) {
            {
                std::unique_lock<std::mutex> lk(intermediates_mutex);
                intermediates_cv.wait(lk, [&] {
                    return (!pending_intermediates.empty() || stopped);
                });
                if (!stopped) {
                    uint64_t iter_begin = (*pending_intermediates.begin()).first;
                    form y = (*pending_intermediates.begin()).second;
                    pending_intermediates.erase(pending_intermediates.begin());
                    lk.unlock();
                    CalculateIntermediatesInner(y, iter_begin);
                }
            }
        }
    }

  private:
    std::vector<std::thread> storage_threads;
    FastAlgorithmCallback* weso;
    bool* intermediates_stored;
    bool stopped;
    std::map<uint64_t, form> pending_intermediates;
    int intermediates_threads = 6;
    std::mutex intermediates_mutex;
    std::condition_variable intermediates_cv;
    uint64_t intermediates_iter = 0;
};

#endif // FAST_STORAGE_H
