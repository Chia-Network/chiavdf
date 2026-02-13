#ifndef CALLBACK_H
#define CALLBACK_H

#include "util.h"
#include "nudupl_listener.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <stdexcept>

// Applies to n-weso.
const int kWindowSize = 20;

// Applies only to 2-weso.
const int kMaxItersAllowed = 8e8;
const int kSwitchIters = 91000000;

class WesolowskiCallback :public INUDUPLListener {
public:
    WesolowskiCallback(integer& D) {
        vdfo = new vdf_original();
        reducer = new PulmarkReducer();
        this->D = D;
        this->L = root(-D, 4);
    }

    virtual ~WesolowskiCallback() {
        delete(vdfo);
        delete(reducer);
    }

    void reduce(form& inf) {
        reducer->reduce(inf);
    }

    void SetForm(int type, void *data, form* mulf, bool reduced = true) {
        switch(type) {
            case NL_SQUARESTATE:
            {
#if (defined(ARCH_X86) || defined(ARCH_X64)) && !defined(CHIA_DISABLE_ASM)
                //cout << "NL_SQUARESTATE" << endl;
                uint64 res;

                square_state_type *square_state=(square_state_type *)data;

                if(!square_state->assign(mulf->a, mulf->b, mulf->c, res))
                    cout << "square_state->assign failed" << endl;
#else
                // Phased pipeline is x86/x64-only.
                (void)data;
                cout << "NL_SQUARESTATE unsupported on this architecture" << endl;
#endif
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

    virtual void OnIteration(int type, void *data, uint64_t iteration) = 0;

    std::unique_ptr<form[]> forms;
    size_t forms_capacity = 0;
    std::atomic<int64_t> iterations{0};
    integer D;
    integer L;
    PulmarkReducer* reducer;
    vdf_original* vdfo;
};

class OneWesolowskiCallback: public WesolowskiCallback {
  public:
    OneWesolowskiCallback(integer& D, form& f, uint64_t wanted_iter) : WesolowskiCallback(D) {
        uint32_t k, l;
        this->wanted_iter = wanted_iter;
        if (wanted_iter >= (1 << 16)) {
            ApproximateParameters(wanted_iter, l, k);
        } else {
            k = 10;
            l = 1;
        }
        const uint64_t step = static_cast<uint64_t>(k) * static_cast<uint64_t>(l);
        if (step == 0) {
            throw std::overflow_error("OneWesolowskiCallback invalid checkpoint stride");
        }
        if (step > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            throw std::overflow_error("OneWesolowskiCallback checkpoint stride too large");
        }
        kl = static_cast<uint32_t>(step);

        const uint64_t space_needed = wanted_iter / step + 100;
        if (space_needed > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            throw std::overflow_error("OneWesolowskiCallback forms capacity overflow");
        }
        forms_capacity = static_cast<size_t>(space_needed);
        forms.reset(new form[forms_capacity]);
        forms[0] = f;
    }

    void OnIteration(int type, void *data, uint64_t iteration) {
        iteration++;
        if (iteration > wanted_iter)
            return ;

        if (iteration % kl == 0) {
            const size_t pos = static_cast<size_t>(iteration / kl);
            if (pos >= forms_capacity) {
                throw std::runtime_error("OneWesolowskiCallback::OnIteration out of bounds");
            }
            form* mulf = &forms[pos];
            SetForm(type, data, mulf);
        }
        if (iteration == wanted_iter) {
            SetForm(type, data, &result);
        }
    }

    uint64_t wanted_iter;
    uint32_t kl;
    form result;
};

class TwoWesolowskiCallback: public WesolowskiCallback {
  public:
    TwoWesolowskiCallback(integer& D, const form& f) : WesolowskiCallback(D) {
        const uint64_t early_points = static_cast<uint64_t>(kSwitchIters) / 10;
        const uint64_t late_points =
            (static_cast<uint64_t>(kMaxItersAllowed) - static_cast<uint64_t>(kSwitchIters)) / 100;
        const size_t space_needed = static_cast<size_t>(early_points + late_points);
        forms_capacity = space_needed;
        forms.reset(new form[space_needed]);
        forms[0] = f;
        kl.store(10, std::memory_order_relaxed);
        switch_iters.store(-1, std::memory_order_relaxed);
        switch_index.store(0, std::memory_order_relaxed);
    }

    void IncreaseConstants(uint64_t num_iters) {
        // Publish transition metadata first, then publish kl=100.
        // OnIteration acquires kl before consuming switch state.
        switch_index.store(num_iters / 10, std::memory_order_relaxed);
        switch_iters.store(static_cast<int64_t>(num_iters), std::memory_order_relaxed);
        kl.store(100, std::memory_order_release);
    }

    int GetPosition(uint64_t power) {
        return GetPositionUnlocked(power);
    }

    int GetPositionUnlocked(uint64_t power) const {
        const int64_t current_switch_iters = switch_iters.load(std::memory_order_acquire);
        if (current_switch_iters == -1 || power < static_cast<uint64_t>(current_switch_iters)) {
            return power / 10;
        } else {
            const uint64_t current_switch_index = switch_index.load(std::memory_order_relaxed);
            return static_cast<int>(current_switch_index + (power - static_cast<uint64_t>(current_switch_iters)) / 100);
        }
    }

    form GetFormCopy(uint64_t power) {
        std::lock_guard<std::mutex> lk(forms_mutex);
        const int pos = GetPositionUnlocked(power);
        if (pos < 0 || static_cast<size_t>(pos) >= forms_capacity) {
            throw std::runtime_error("TwoWesolowskiCallback::GetFormCopy out of bounds");
        }
        return forms[static_cast<size_t>(pos)];
    }

    bool LargeConstants() {
        return kl.load(std::memory_order_relaxed) == 100;
    }

    void OnIteration(int type, void *data, uint64_t iteration) {
        iteration++;
        // Most iterations are not checkpoints. Avoid mutex lock/unlock on that hot path.
        const uint32_t current_kl = kl.load(std::memory_order_relaxed);
        if (iteration % current_kl != 0) {
            return;
        }

        std::lock_guard<std::mutex> lk(forms_mutex);
        const uint32_t locked_kl = kl.load(std::memory_order_acquire);
        if (iteration % locked_kl != 0) {
            return;
        }
        const int pos = GetPositionUnlocked(iteration);
        if (pos < 0 || static_cast<size_t>(pos) >= forms_capacity) {
            throw std::runtime_error("TwoWesolowskiCallback::OnIteration out of bounds");
        }
        form* mulf = &forms[static_cast<size_t>(pos)];
        SetForm(type, data, mulf);
    }

  private:
    std::atomic<uint64_t> switch_index{0};
    std::atomic<int64_t> switch_iters{-1};
    std::atomic<uint32_t> kl{10};
    std::mutex forms_mutex;
};

class FastAlgorithmCallback : public WesolowskiCallback {
  public:
    FastAlgorithmCallback(int segments, integer& D, form f, bool multi_proc_machine) : WesolowskiCallback(D) {
        buckets_begin.push_back(0);
        buckets_begin.push_back(bucket_size1 * window_size);
        this->segments = segments;
        this->multi_proc_machine = multi_proc_machine;
        for (int i = 0; i < segments - 2; i++) {
            buckets_begin.push_back(buckets_begin[buckets_begin.size() - 1] + bucket_size2 * window_size);
        }
        int space_needed = window_size * (bucket_size1 + bucket_size2 * (segments - 1));
        forms_capacity = static_cast<size_t>(space_needed);
        forms.reset(new form[space_needed]);
        checkpoints.reset(new form[1 << 18]);

        y_ret = f;
        for (int i = 0; i < segments; i++)
            forms[buckets_begin[i]] = f;
        checkpoints[0] = f;
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
        uint64_t pos = GetPosition(exponent, bucket);
        return &(forms[pos]);
    }

    // We need to store:
    // 2^16 * k + 10 * l
    // 2^(18 + 2*m) * k + 12 * 2^(2*m) * l
    void OnIteration(int type, void *data, uint64_t iteration) {
        iteration++;
        if (multi_proc_machine) {
            if (iteration % (1 << 15) == 0) {
                SetForm(type, data, &y_ret);
            }
        } else {
            // If 'multi_proc_machine' is 0, we store the intermediates
            // right away.
            for (int i = 0; i < segments; i++) {
                uint64_t power_2 = 1LL << (16 + 2LL * i);
                int kl = (i == 0) ? 10 : (12 * (power_2 >> 18));
                if ((iteration % power_2) % kl == 0) {
                    form* mulf = GetForm(iteration, i);
                    SetForm(type, data, mulf);
                }
            }
        }

        if (iteration % (1 << 16) == 0) {
            form* mulf = (&checkpoints[(iteration / (1 << 16))]);
            SetForm(type, data, mulf);
        }
    }

    std::vector<int> buckets_begin;
    std::unique_ptr<form[]> checkpoints;
    form y_ret;
    int segments;
    // The intermediate values size of a 2^16 segment.
    const int bucket_size1 = 6554;
    // The intermediate values size of a >= 2^18 segment.
    const int bucket_size2 = 21846;
    // Assume provers won't be left behind by more than this # of segments.
    const int window_size = kWindowSize;
    bool multi_proc_machine;
};

#endif // CALLBACK_H
