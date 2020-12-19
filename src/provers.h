#ifndef PROVERS_H
#define PROVERS_H

#include <atomic>

#include "proof_common.h"
#include "util.h"
#include "callback.h"

class Prover {
  public:
    Prover(Segment segm, integer D) {
        this->segm = segm;
        this->D = D;
        this->num_iterations = segm.length;
        is_finished = false;
    }

    virtual form* GetForm(uint64_t iteration) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool PerformExtraStep() = 0;
    virtual void OnFinish() = 0;

    bool IsFinished() {
        return is_finished;
    }

    form GetProof() {
        return proof;
    }

    uint64_t GetBlock(uint64_t i, uint64_t k, uint64_t T, integer& B) {
        integer res = FastPow(2, T - k * (i + 1), B);
        mpz_mul_2exp(res.impl, res.impl, k);
        res = res / B;
        auto res_vector = res.to_vector();
        return res_vector.empty() ? 0 : res_vector[0];
    }

    void GenerateProof() {
        PulmarkReducer reducer;

        integer B = GetB(D, segm.x, segm.y);
        integer L=root(-D, 4);
        form id;
        try {
            id = form::identity(D);
        } catch(std::exception& e) {
            std::cout << "Warning: Could not create identity: " << e.what() << "\n";
            std::cout << "Discriminant: " << D.impl << "\n";
            std::cout << "Segment start:" << segm.start << "\n";
            std::cout << "Segment length:" << segm.length << "\n";
            std::cout << std::flush;

            return ;
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
            uint64_t limit = num_iterations / (k * l);
            if (num_iterations % (k * l))
                limit++;
            for (uint64_t i = 0; i < limit; i++) {
                if (num_iterations >= k * (i * l + j + 1)) {
                    uint64_t b = GetBlock(i*l + j, k, num_iterations, B);
                    if (!PerformExtraStep()) return;
                    tmp = GetForm(i);
                    nucomp_form(ys[b], ys[b], *tmp, D, L);
                }
            }

            for (uint64_t b1 = 0; b1 < (1 << k1); b1++) {
                form z = id;
                for (uint64_t b0 = 0; b0 < (1 << k0); b0++) {
                    if (!PerformExtraStep()) return;
                    nucomp_form(z, z, ys[b1 * (1 << k0) + b0], D, L);
                }
                z = FastPowFormNucomp(z, D, integer(b1 * (1 << k0)), L, reducer);
                nucomp_form(x, x, z, D, L);
            }

            for (uint64_t b0 = 0; b0 < (1 << k0); b0++) {
                form z = id;
                for (uint64_t b1 = 0; b1 < (1 << k1); b1++) {
                    if (!PerformExtraStep()) return;
                    nucomp_form(z, z, ys[b1 * (1 << k0) + b0], D, L);
                }
                z = FastPowFormNucomp(z, D, integer(b0), L, reducer);
                nucomp_form(x, x, z, D, L);
            }
        }
        reducer.reduce(x);
        proof = x;
        OnFinish();
    }

  protected:
    Segment segm;
    integer D;
    form proof;
    uint64_t num_iterations;
    uint32_t k;
    uint32_t l;
    std::atomic<bool> is_finished;
};

class OneWesolowskiProver : public Prover {
  public:
    OneWesolowskiProver(Segment segm, integer D, form* intermediates, std::atomic<bool>& stop_signal)
        : Prover(segm, D), stop_signal(stop_signal)
    {
        this->intermediates = intermediates;
        if (num_iterations >= (1 << 16)) {
            ApproximateParameters(num_iterations, k, l);
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
