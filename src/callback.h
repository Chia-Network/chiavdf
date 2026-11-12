#ifndef CALLBACK_H
#define CALLBACK_H

#include "util.h"
#include "nudupl_listener.h"
#include "two_weso_limits.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

// Applies to n-weso.
const int kWindowSize = 20;

struct TwoWesoLimits {
    // Iteration count at which the worker switches from a 10- to a 100-iteration stride.
    uint64_t switch_threshold_iters = kSwitchIters;
    uint64_t wide_stride_iters = kWideStrideIters;
    uint64_t max_iters = kMaxItersAllowed;
    // log2 of the number of checkpoints per storage block (tests use small blocks).
    uint32_t block_shift = 16;
};

struct TwoWesoProverTuning {
    uint64_t small_segment_iters = kSmallSegmentIters;
};

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
    std::unique_ptr<form[]> forms;
    size_t forms_capacity = 0;
};

class TwoWesolowskiCallback: public WesolowskiCallback {
  public:
    TwoWesolowskiCallback(integer& D, const form& f,
        const TwoWesoLimits& limits = TwoWesoLimits(),
        const TwoWesoProverTuning& tuning = TwoWesoProverTuning())
        : WesolowskiCallback(D), limits_(limits), tuning_(tuning) {
        if (limits_.switch_threshold_iters == 0) {
            throw std::invalid_argument("TwoWesolowskiCallback switch_threshold_iters must be positive");
        }
        if (limits_.switch_threshold_iters % 100 != 0) {
            throw std::invalid_argument("TwoWesolowskiCallback switch_threshold_iters must be a multiple of 100");
        }
        if (limits_.wide_stride_iters % 1000 != 0) {
            throw std::invalid_argument("TwoWesolowskiCallback wide_stride_iters must be a multiple of 1000");
        }
        if (limits_.switch_threshold_iters > std::numeric_limits<uint64_t>::max() - kSwitchSlackIters) {
            throw std::invalid_argument("TwoWesolowskiCallback switch_threshold_iters is too large");
        }
        const uint64_t switch_max = limits_.switch_threshold_iters + kSwitchSlackIters;
        if (switch_max >= limits_.wide_stride_iters) {
            throw std::invalid_argument("TwoWesolowskiCallback switch_threshold_iters plus slack must be before wide_stride_iters");
        }
        if (switch_max > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) - 1) {
            throw std::invalid_argument("TwoWesolowskiCallback switch threshold does not fit the 32-bit switch encoding");
        }
        if (limits_.wide_stride_iters > std::numeric_limits<uint64_t>::max() - kTwoWesoStopMargin ||
            limits_.max_iters <= limits_.wide_stride_iters + kTwoWesoStopMargin) {
            throw std::invalid_argument("TwoWesolowskiCallback max_iters must be past wide_stride_iters by more than the stop margin");
        }
        if (tuning_.small_segment_iters == 0) {
            throw std::invalid_argument("TwoWesolowskiCallback small_segment_iters must be positive");
        }
        if (limits_.block_shift < 4 || limits_.block_shift > 24) {
            throw std::invalid_argument("TwoWesolowskiCallback block_shift must be between 4 and 24");
        }

        const uint64_t mid_points = (limits_.wide_stride_iters - switch_max) / 100;
        const uint64_t late_points = (limits_.max_iters - limits_.wide_stride_iters) / 1000;
        uint64_t capacity = switch_max / 10;
        if (capacity > std::numeric_limits<uint64_t>::max() - mid_points) {
            throw std::overflow_error("TwoWesolowskiCallback forms capacity overflow");
        }
        capacity += mid_points;
        if (capacity > std::numeric_limits<uint64_t>::max() - late_points) {
            throw std::overflow_error("TwoWesolowskiCallback forms capacity overflow");
        }
        capacity += late_points;
        if (capacity == std::numeric_limits<uint64_t>::max()) {
            throw std::overflow_error("TwoWesolowskiCallback forms capacity overflow");
        }
        capacity += 1;
        if (capacity > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            throw std::overflow_error("TwoWesolowskiCallback forms capacity overflow");
        }
        capacity_ = static_cast<size_t>(capacity);
        block_size_ = size_t(1) << limits_.block_shift;
        if (capacity_ > std::numeric_limits<size_t>::max() - (block_size_ - 1)) {
            throw std::overflow_error("TwoWesolowskiCallback blocks directory overflow");
        }
        blocks_.resize((capacity_ + block_size_ - 1) / block_size_);

        form* slot = SlotUnlocked(0, true);
        *slot = f;
        if (blocks_.size() > 1 && !blocks_[1]) {
            blocks_[1].reset(new form[block_size_]);
        }
        kl.store(10, std::memory_order_relaxed);
        transition_state.store(EncodeTransitionState(/*switch_index=*/0, /*switch_iters=*/-1), std::memory_order_relaxed);
    }

    const TwoWesoLimits& Limits() const { return limits_; }
    const TwoWesoProverTuning& ProverTuning() const { return tuning_; }

    void IncreaseConstants(uint64_t num_iters) {
        if (num_iters >= limits_.switch_threshold_iters + kSwitchSlackIters) {
            throw std::logic_error("TwoWesolowskiCallback::IncreaseConstants past the switch window");
        }
        std::lock_guard<std::mutex> lk(forms_mutex);
        // Publish transition metadata first. `kl` is only a fast-path hint.
        transition_state.store(
            EncodeTransitionState(num_iters / 10, static_cast<int64_t>(num_iters)),
            std::memory_order_release
        );
        kl.store(100, std::memory_order_release);
    }

    size_t GetPosition(uint64_t power) {
        std::lock_guard<std::mutex> lk(forms_mutex);
        return GetPositionUnlocked(power);
    }

    size_t GetPositionUnlocked(uint64_t power) const {
        const uint64_t snapshot = transition_state.load(std::memory_order_acquire);
        return GetPositionFromSnapshot(power, snapshot);
    }

    // The stride of the stratum containing end - 1 (for end == 0 return 10).
    // Checkpoints below `end` exist at every multiple of this value, so a proof
    // segment ending at `end` must step through checkpoints in multiples of it.
    uint32_t StrideCovering(uint64_t end) const {
        if (end == 0) {
            return 10U;
        }
        const uint64_t snapshot = transition_state.load(std::memory_order_acquire);
        const Strata st = StrataFromSnapshot(snapshot);
        return StratumFor(st, end - 1).stride;
    }

    form GetFormCopy(uint64_t power) {
        std::lock_guard<std::mutex> lk(forms_mutex);
        const size_t pos = GetPositionUnlocked(power);
        form* slot = SlotUnlocked(pos, false);
        if (slot == nullptr) {
            throw std::logic_error("TwoWesolowskiCallback::GetFormCopy checkpoint not computed yet");
        }
        return *slot;
    }

    bool LargeConstants() {
        const uint64_t snapshot = transition_state.load(std::memory_order_acquire);
        return DecodeSwitchIters(snapshot) != -1;
    }

    void OnIteration(int type, void *data, uint64_t iteration) {
        iteration++;
        // Most iterations are not checkpoints. Avoid mutex lock/unlock on that hot path.
        const uint32_t current_kl = kl.load(std::memory_order_relaxed);
        if (iteration % current_kl != 0) {
            return;
        }

        bool allocate_next = false;
        size_t next_block = 0;
        {
            std::lock_guard<std::mutex> lk(forms_mutex);
            const uint64_t snapshot = transition_state.load(std::memory_order_acquire);
            const Strata st = StrataFromSnapshot(snapshot);
            const Stratum& stratum = StratumFor(st, iteration);
            if (iteration % stratum.stride != 0) {
                return;
            }
            const size_t pos = static_cast<size_t>(PositionInStrata(st, iteration));
            form* mulf = SlotUnlocked(pos, true);
            SetForm(type, data, mulf);
            if (stratum.stride > current_kl) {
                kl.store(stratum.stride, std::memory_order_release);
            }
            const size_t candidate = (pos >> limits_.block_shift) + 1;
            if (candidate < blocks_.size() && !blocks_[candidate]) {
                allocate_next = true;
                next_block = candidate;
            }
        }
        if (allocate_next) {
            std::unique_ptr<form[]> pending(new form[block_size_]);
            std::lock_guard<std::mutex> lk(forms_mutex);
            if (!blocks_[next_block]) {
                blocks_[next_block] = std::move(pending);
            }
        }
    }

  private:
    struct Stratum {
        uint64_t begin_iter;
        uint64_t begin_pos;
        uint32_t stride;
    };
    // Strata in increasing order; strata[count-1] is the last one in use.
    struct Strata {
        Stratum s[3];
        int count;
    };

    Strata StrataFromSnapshot(uint64_t state) const {
        const int64_t switch_iters = DecodeSwitchIters(state);
        const uint64_t switch_index = DecodeSwitchIndex(state);
        const uint64_t wide = limits_.wide_stride_iters;
        Strata st;
        if (switch_iters == -1) {
            st.s[0] = {0, 0, 10};
            st.s[1] = {wide, wide / 10, 1000};
            st.count = 2;
        } else {
            const uint64_t sw = static_cast<uint64_t>(switch_iters);
            st.s[0] = {0, 0, 10};
            st.s[1] = {sw, switch_index, 100};
            st.s[2] = {wide, switch_index + (wide - sw) / 100, 1000};
            st.count = 3;
        }
        return st;
    }

    const Stratum& StratumFor(const Strata& st, uint64_t power) const {
        const Stratum* chosen = &st.s[0];
        for (int i = 0; i < st.count; i++) {
            if (st.s[i].begin_iter <= power) {
                chosen = &st.s[i];
            } else {
                break;
            }
        }
        return *chosen;
    }

    uint64_t PositionInStrata(const Strata& st, uint64_t power) const {
        const Stratum& stratum = StratumFor(st, power);
        return stratum.begin_pos + (power - stratum.begin_iter) / stratum.stride;
    }

    form* SlotUnlocked(size_t pos, bool allocate) {
        if (pos >= capacity_) {
            throw std::runtime_error("TwoWesolowskiCallback position out of bounds");
        }
        const size_t block = pos >> limits_.block_shift;
        if (!blocks_[block]) {
            if (!allocate) {
                return nullptr;
            }
            blocks_[block].reset(new form[block_size_]);
        }
        return &blocks_[block][pos & (block_size_ - 1)];
    }

    static uint64_t EncodeTransitionState(uint64_t switch_index_value, int64_t switch_iters_value) {
        if (switch_index_value > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            throw std::overflow_error("TwoWesolowskiCallback switch_index overflow");
        }
        // Persist switch_iters in 32 bits with +1 encoding to represent -1 (unset) as 0.
        if (switch_iters_value < -1 ||
            switch_iters_value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max() - 1)) {
            throw std::overflow_error("TwoWesolowskiCallback switch_iters overflow");
        }
        const uint32_t encoded_iters = static_cast<uint32_t>(switch_iters_value + 1);
        return (static_cast<uint64_t>(encoded_iters) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(switch_index_value));
    }

    static uint64_t DecodeSwitchIndex(uint64_t state) {
        return static_cast<uint64_t>(state & 0xffffffffULL);
    }

    static int64_t DecodeSwitchIters(uint64_t state) {
        const uint32_t encoded_iters = static_cast<uint32_t>(state >> 32);
        return static_cast<int64_t>(encoded_iters) - 1;
    }

    uint32_t GetEffectiveKl(uint64_t power, uint64_t state) const {
        return StratumFor(StrataFromSnapshot(state), power).stride;
    }

    size_t GetPositionFromSnapshot(uint64_t power, uint64_t state) const {
        return static_cast<size_t>(PositionInStrata(StrataFromSnapshot(state), power));
    }

    TwoWesoLimits limits_;
    TwoWesoProverTuning tuning_;
    size_t capacity_ = 0;
    size_t block_size_ = 0;
    std::vector<std::unique_ptr<form[]>> blocks_;
    std::atomic<uint64_t> transition_state{0};
    std::atomic<uint32_t> kl{10};
    std::mutex forms_mutex;
};

class FastAlgorithmCallback : public WesolowskiCallback {
  public:
    FastAlgorithmCallback(int segments, integer& D, form f, bool multi_proc_machine) : WesolowskiCallback(D) {
        if (segments < 1) {
            throw std::invalid_argument("FastAlgorithmCallback requires at least one segment");
        }

        buckets_begin.push_back(0);
        this->segments = segments;
        this->multi_proc_machine = multi_proc_machine;
        if (segments > 1) {
            buckets_begin.push_back(bucket_size1 * window_size);
        }
        for (int i = 2; i < segments; i++) {
            buckets_begin.push_back(buckets_begin.back() + bucket_size2 * window_size);
        }
        const uint64_t per_window =
            static_cast<uint64_t>(bucket_size1) +
            static_cast<uint64_t>(bucket_size2) * static_cast<uint64_t>(segments - 1);
        const uint64_t total_forms = static_cast<uint64_t>(window_size) * per_window;
        if (total_forms > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            throw std::overflow_error("FastAlgorithmCallback forms capacity overflow");
        }
        forms_capacity = static_cast<size_t>(total_forms);
        forms.reset(new form[forms_capacity]);
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
    std::unique_ptr<form[]> forms;
    size_t forms_capacity = 0;
    // The intermediate values size of a 2^16 segment.
    const int bucket_size1 = 6554;
    // The intermediate values size of a >= 2^18 segment.
    const int bucket_size2 = 21846;
    // Assume provers won't be left behind by more than this # of segments.
    const int window_size = kWindowSize;
    bool multi_proc_machine;
};

#endif // CALLBACK_H
