#ifndef CALLBACK_H
#define CALLBACK_H

#include "util.h"

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

    virtual void OnIteration(int type, void *data, uint64_t iteration) = 0;

    std::unique_ptr<form[]> forms;
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
            ApproximateParameters(wanted_iter, k, l);
        } else {
            k = 10;
            l = 1;
        }
        kl = k * l;
        uint64_t space_needed = wanted_iter / (k * l) + 100;
        forms.reset(new form[space_needed]);
        forms[0] = f;
    }

    void OnIteration(int type, void *data, uint64_t iteration) {
        iteration++;
        if (iteration > wanted_iter) 
            return ;

        if (iteration % kl == 0) {
            uint64_t pos = iteration / kl;
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
    TwoWesolowskiCallback(integer& D, form f) : WesolowskiCallback(D) {
        int space_needed = kSwitchIters / 10 + (kMaxItersAllowed - kSwitchIters) / 100;
        forms.reset(new form[space_needed]);
        forms[0] = f;
        kl = 10;
        switch_iters = -1;
    }

    void IncreaseConstants(uint64_t num_iters) {
        kl = 100;
        switch_iters = num_iters;
        switch_index = num_iters / 10;
    }

    int GetPosition(uint64_t power) {
        if (switch_iters == -1 || power < switch_iters) {
            return power / 10;
        } else {
            return (switch_index + (power - switch_iters) / 100);
        }
    }

    form *GetForm(uint64_t power) {
        return &(forms[GetPosition(power)]);
    }

    bool LargeConstants() {
        return kl == 100;
    }

    void OnIteration(int type, void *data, uint64_t iteration) {
        iteration++;
        if (iteration % kl == 0) {
            uint64_t pos = GetPosition(iteration);
            form* mulf = &forms[pos];
            SetForm(type, data, mulf);
        }
    }

  private:
    uint64_t switch_index;
    int64_t switch_iters;
    uint32_t kl;
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
