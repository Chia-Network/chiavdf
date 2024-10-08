#ifndef PROVERS_H
#define PROVERS_H

#include "prover_base.hpp"
#include "callback.h"

class OneWesolowskiProver : public Prover {
  public:
    OneWesolowskiProver(Segment segm, integer D, form* intermediates, std::atomic<bool>& stop_signal)
        : Prover(segm, D), stop_signal(stop_signal)
    {
        this->intermediates = intermediates;
        if (num_iterations >= (1 << 16)) {
            ApproximateParameters(num_iterations, l, k);
        } else {
            k = 10;
            l = 1;
        }
    }

    form* GetForm(uint64_t iteration) {
        return &intermediates[iteration];
    }

    void start() {
        GenerateProof();
    }

    void stop() {
    }

    bool PerformExtraStep() {
        return !stop_signal;
    }

    void OnFinish() {
        is_finished = true;
    }

  private:
    form* intermediates;
    std::atomic<bool>& stop_signal;
};

class TwoWesolowskiProver : public Prover{
  public:
    TwoWesolowskiProver(Segment segm, integer D, TwoWesolowskiCallback* weso, std::atomic<bool>& stop_signal):
        Prover(segm, D), stop_signal(stop_signal)
    {
        this->weso = weso;
        this->done_iterations = segm.start;
        k = 10;
        l = (segm.length < 10000000) ? 1 : 10;
    }

    void start() {
        std::thread t([=] { GenerateProof(); });
        t.detach();
    }

    virtual form* GetForm(uint64_t i) {
        return weso->GetForm(done_iterations + i * k * l);
    }

    void stop() {
    }

    bool PerformExtraStep() {
        return !stop_signal;
    }

    void OnFinish() {
        is_finished = true;
    }

  private:
    TwoWesolowskiCallback* weso;
    std::atomic<bool>& stop_signal;
    uint64_t done_iterations;
};

extern bool new_event;
extern std::mutex new_event_mutex;
extern std::condition_variable new_event_cv;

class InterruptableProver: public Prover {
  public:
    InterruptableProver(Segment segm, integer D, FastAlgorithmCallback* weso) : Prover(segm, D) {
        this->weso = weso;
        this->done_iterations = segm.start;
        this->bucket = segm.GetSegmentBucket();
        if (segm.length <= (1 << 16))
            k = 10;
        else
            k = 12;
        if (segm.length <= (1 << 18))
            l = 1;
        else
            l = (segm.length >> 18);
        is_paused = false;
        is_fully_finished = false;
        joined = false;
    }

    ~InterruptableProver() {
        if (!joined) {
            th->join();
        }
        delete(th);
    }

    form* GetForm(uint64_t i) {
        return weso->GetForm(done_iterations + i * k * l, bucket);
    }

    void start() {
        th = new std::thread([=] { GenerateProof(); });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(m);
            is_finished = true;
            is_fully_finished = true;
            if (is_paused) {
                is_paused = false;
            }
        }
        cv.notify_one();
        th->join();
        joined = true;
    }

    bool PerformExtraStep() {
        if (is_finished) {
            return false;
        }
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] {
            return !is_paused;
        });
        return true;
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

    bool IsFullyFinished() {
        return is_fully_finished;
    }

    void OnFinish() {
        is_finished = true;
        if (!is_fully_finished) {
            // Notify event loop a proving thread is free.
            {
                std::lock_guard<std::mutex> lk(new_event_mutex);
                new_event = true;
            }
            new_event_cv.notify_one();
            is_fully_finished = true;
        }
    }

  private:
    std::thread* th;
    FastAlgorithmCallback* weso;
    std::condition_variable cv;
    std::mutex m;
    bool is_paused;
    std::atomic<bool> is_fully_finished;
    bool joined;
    uint64_t done_iterations;
    int bucket;
};

#endif // PROVERS_H
