#include "fast_wrapper.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "../vdf.h"
#include "../create_discriminant.h"

// Runtime configuration knobs required by `parameters.h`.
// These are `extern` variables there, but each binary defines them explicitly.
bool use_divide_table = false;
int gcd_base_bits = 50;
int gcd_128_max_iter = 3;
std::string asmprefix = "cel_";
bool enable_all_instructions = false;

namespace {
std::once_flag init_once;
std::atomic<uint64_t> bucket_memory_budget_bytes(128ULL * 1024ULL * 1024ULL);
std::atomic<bool> streaming_stats_enabled(false);

struct LastStreamingParameters {
    uint32_t k = 0;
    uint32_t l = 0;
    bool tuned = false;
    bool set = false;
};

thread_local LastStreamingParameters last_streaming_parameters;

struct LastStreamingStats {
    uint64_t checkpoint_total_ns = 0;
    uint64_t checkpoint_event_total_ns = 0;
    uint64_t finalize_total_ns = 0;
    uint64_t checkpoint_calls = 0;
    uint64_t bucket_updates = 0;
    bool set = false;
};

thread_local LastStreamingStats last_streaming_stats;

void init_chiavdf_fast() {
    init_gmp();
    set_rounding_mode();

    // Match the vdf_client runtime selection for AVX2.
    if (hasAVX2()) {
        gcd_base_bits = 63;
        gcd_128_max_iter = 2;
    } else {
        gcd_base_bits = 50;
        gcd_128_max_iter = 3;
    }

    // Ensure we run the one-wesolowski path by default.
    fast_algorithm = false;
    two_weso = false;
    quiet_mode = true;
}

ChiavdfByteArray empty_result() { return ChiavdfByteArray{nullptr, 0}; }

uint64_t saturating_add_u64(uint64_t lhs, uint64_t rhs) {
    if (lhs > std::numeric_limits<uint64_t>::max() - rhs) {
        return std::numeric_limits<uint64_t>::max();
    }
    return lhs + rhs;
}

bool try_pow2_u64_shift(uint32_t shift, uint64_t& out) {
    if (shift >= 64) {
        return false;
    }
    out = 1ULL << shift;
    return true;
}

struct BatchProgressContext {
    uint64_t completed_before = 0;
    ChiavdfProgressCallback progress_cb = nullptr;
    void* progress_user_data = nullptr;
};

void batch_progress_trampoline(uint64_t iters_done, void* user_data) {
    auto* ctx = static_cast<BatchProgressContext*>(user_data);
    if (ctx == nullptr || ctx->progress_cb == nullptr) {
        return;
    }
    ctx->progress_cb(saturating_add_u64(ctx->completed_before, iters_done), ctx->progress_user_data);
}

uint64_t estimate_bucket_form_bytes(size_t discriminant_size_bits) {
    // Be conservative: class group forms contain 3 GMP-backed integers that
    // quickly grow to the discriminant size (or beyond) during NUCOMP.
    //
    // This estimate is intentionally larger than the raw serialized size to
    // avoid picking parameters that risk paging/OOM.
    uint64_t discr_bytes = (static_cast<uint64_t>(discriminant_size_bits) + 7) / 8;
    uint64_t estimate = discr_bytes * 16;
    if (estimate < 2048) {
        estimate = 2048;
    }
    return estimate;
}

bool tune_streaming_parameters(
    uint64_t num_iterations,
    size_t discriminant_size_bits,
    uint64_t memory_budget_bytes,
    uint32_t& out_l,
    uint32_t& out_k) {
    if (memory_budget_bytes == 0) {
        return false;
    }

    // Keep headroom for GMP scratch allocations and general process overhead.
    uint64_t budget = (memory_budget_bytes * 80) / 100;
    uint64_t bytes_per_form = estimate_bucket_form_bytes(discriminant_size_bits);
    if (budget < bytes_per_form) {
        return false;
    }

    unsigned __int128 best_cost = std::numeric_limits<unsigned __int128>::max();
    bool found = false;
#ifndef NDEBUG
    uint32_t best_k = 0;
    uint32_t best_l = 0;
    unsigned __int128 best_updates = 0;
    unsigned __int128 best_checkpoints = 0;
    unsigned __int128 best_fold = 0;
#endif

    // Empirical tuning notes (1024-bit discriminants, AVX2 build):
    // - Each bucket update (NUCOMP) and each fold unit is ~5µs.
    // - Per-checkpoint event overhead (SetForm + bookkeeping) is ~0.3µs.
    //
    // So checkpoint counts should be weighted much lower than updates/fold.
    constexpr unsigned __int128 update_weight = 16;
    constexpr unsigned __int128 fold_weight = 16;
    constexpr unsigned __int128 checkpoint_weight = 1;

    // Search a small grid of `(k,l)` values. Higher `k` reduces checkpoint work
    // (~T/k) but increases fold work (~l·2^k) and bucket memory (~l·2^k).
    for (uint32_t k = 4; k <= 20; k++) {
        unsigned __int128 buckets_per_row = static_cast<unsigned __int128>(1) << k;

        for (uint32_t l = 1; l <= 64; l++) {
            unsigned __int128 form_count = buckets_per_row * static_cast<unsigned __int128>(l);
            unsigned __int128 mem_required =
                form_count * static_cast<unsigned __int128>(bytes_per_form);
            if (mem_required > static_cast<unsigned __int128>(budget)) {
                continue;
            }

            uint64_t kl = static_cast<uint64_t>(k) * static_cast<uint64_t>(l);
            unsigned __int128 checkpoints = static_cast<unsigned __int128>(
                (num_iterations + kl - 1) / kl);
            // Each checkpoint can trigger up to `l` bucket updates (one per sub-block).
            // Model update work as checkpoint-count scaled by `l`.
            unsigned __int128 updates = checkpoints * static_cast<unsigned __int128>(l);
            unsigned __int128 fold = static_cast<unsigned __int128>(l) << (k + 1);
            unsigned __int128 cost =
                updates * update_weight + checkpoints * checkpoint_weight + fold * fold_weight;

            if (!found || cost < best_cost) {
                found = true;
                best_cost = cost;
                out_k = k;
                out_l = l;
#ifndef NDEBUG
                best_k = k;
                best_l = l;
                best_updates = updates;
                best_checkpoints = checkpoints;
                best_fold = fold;
#endif
            }
        }
    }

#ifndef NDEBUG
    if (found) {
        assert(best_k >= 4 && best_k <= 20);
        assert(best_l >= 1 && best_l <= 64);
        std::fprintf(
            stderr,
            "[chiavdf] tune_streaming_parameters: T=%llu, budget=%llu, selected=(k=%u,l=%u), "
            "components{updates=%llu, checkpoints=%llu, fold=%llu}, weights{u=16,c=1,f=16}\n",
            static_cast<unsigned long long>(num_iterations),
            static_cast<unsigned long long>(memory_budget_bytes),
            best_k,
            best_l,
            static_cast<unsigned long long>(best_updates),
            static_cast<unsigned long long>(best_checkpoints),
            static_cast<unsigned long long>(best_fold));
        if (best_k == 20 && num_iterations < (1ULL << 24)) {
            std::fprintf(
                stderr,
                "[chiavdf] tune_streaming_parameters: high-k selection for moderate T "
                "(k=20, T=%llu); verify measured update/fold timing assumptions.\n",
                static_cast<unsigned long long>(num_iterations));
        }
    }
#endif

    return found;
}

uint64_t get_block(uint64_t i, uint64_t k, uint64_t T, integer& B) {
    integer res = FastPow(2, T - k * (i + 1), B);
    mpz_mul_2exp(res.impl, res.impl, k);
    res = res / B;
    auto res_vector = res.to_vector();
    return res_vector.empty() ? 0 : res_vector[0];
}

class ProgressOneWesolowskiCallback final : public OneWesolowskiCallback {
  public:
    ProgressOneWesolowskiCallback(
        integer& D,
        form& f,
        uint64_t wanted_iter,
        uint64_t progress_interval,
        ChiavdfProgressCallback progress_cb,
        void* progress_user_data)
        : OneWesolowskiCallback(D, f, wanted_iter),
          progress_interval(progress_interval),
          progress_cb(progress_cb),
          progress_user_data(progress_user_data),
          next_progress(progress_interval) {}

    void OnIteration(int type, void* data, uint64_t iteration) override {
        OneWesolowskiCallback::OnIteration(type, data, iteration);

        if (progress_cb == nullptr || progress_interval == 0) {
            return;
        }

        uint64_t done = iteration + 1;
        if (done > wanted_iter) {
            return;
        }

        if (done >= next_progress) {
            progress_cb(next_progress, progress_user_data);
            next_progress += progress_interval;
        }
    }

  private:
    uint64_t progress_interval;
    ChiavdfProgressCallback progress_cb;
    void* progress_user_data;
    uint64_t next_progress;
};

class StreamingWesolowskiBuckets {
  public:
    StreamingWesolowskiBuckets(
        integer& D,
        integer& L,
        uint64_t wanted_iter,
        uint32_t k,
        uint32_t l,
        uint64_t limit,
        integer B,
        bool use_getblock_opt)
        : D(D),
          L(L),
          wanted_iter(wanted_iter),
          k(k),
          l(l),
          kl(static_cast<uint64_t>(k) * static_cast<uint64_t>(l)),
          limit(limit),
          B(std::move(B)),
          use_getblock_opt(use_getblock_opt),
          stats_enabled(streaming_stats_enabled.load(std::memory_order_relaxed)) {
        form id = form::identity(D);
        uint64_t bucket_span_u64 = 0;
        if (!try_pow2_u64_shift(k, bucket_span_u64)) {
            getblock_ok = false;
            return;
        }

        bucket_span = static_cast<size_t>(bucket_span_u64);
        if (bucket_span != 0 && static_cast<size_t>(l) > std::numeric_limits<size_t>::max() / bucket_span) {
            getblock_ok = false;
            return;
        }

        buckets.resize(static_cast<size_t>(l) * bucket_span, id);

        if (use_getblock_opt) {
            getblock_ok = init_getblock_opt_state();
        }
    }

    uint64_t wanted_iterations() const { return wanted_iter; }

	    uint64_t checkpoint_stride() const { return kl; }

	    uint64_t checkpoint_limit() const { return limit; }

	    void process_checkpoint(uint64_t i, const form& checkpoint, bool record_stats = true) {
	        const bool do_stats = stats_enabled && record_stats;
	        auto started_at = std::chrono::steady_clock::time_point{};
	        if (do_stats) {
	            started_at = std::chrono::steady_clock::now();
        }

        uint64_t local_updates = 0;
        for (uint32_t j = 0; j < l; j++) {
            uint64_t p = i * static_cast<uint64_t>(l) + static_cast<uint64_t>(j);
            uint64_t needed = static_cast<uint64_t>(k) * (p + 1);
            if (wanted_iter < needed) {
                break;
            }
            uint64_t b = use_getblock_opt ? get_block_opt(p) : get_block(p, k, wanted_iter, B);
            if (do_stats) {
                local_updates++;
            }
            nucomp_form(bucket(j, b), bucket(j, b), checkpoint, D, L);
        }

        if (do_stats) {
            checkpoint_calls++;
            bucket_updates += local_updates;
            checkpoint_total_ns += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started_at)
                    .count());
        }
	    }

	    bool init_ok() const { return getblock_ok; }

	    form finalize_proof() const {
	        auto started_at = std::chrono::steady_clock::time_point{};
	        if (stats_enabled) {
	            started_at = std::chrono::steady_clock::now();
	        }

	        PulmarkReducer reducer;
	        form id = form::identity(D);

        uint64_t k1 = k / 2;
        uint64_t k0 = k - k1;
        uint64_t span_k0 = 0;
        uint64_t span_k1 = 0;
        if (!try_pow2_u64_shift(static_cast<uint32_t>(k0), span_k0) ||
            !try_pow2_u64_shift(static_cast<uint32_t>(k1), span_k1)) {
            return form::identity(D);
        }
        form x = id;

        for (int64_t j = static_cast<int64_t>(l) - 1; j >= 0; j--) {
            x = FastPowFormNucomp(x, D, integer(static_cast<uint64_t>(bucket_span)), L, reducer);

            for (uint64_t b1 = 0; b1 < span_k1; b1++) {
                form z = id;
                for (uint64_t b0 = 0; b0 < (1ULL << k0); b0++) {
                    nucomp_form(
                        z,
                        z,
                        bucket(static_cast<uint32_t>(j), b1 * (1ULL << k0) + b0),
                        D,
                        L);
                }
                z = FastPowFormNucomp(
                    z,
                    D,
                    integer(static_cast<uint64_t>(b1 * span_k0)),
                    L,
                    reducer);
                nucomp_form(x, x, z, D, L);
            }

            for (uint64_t b0 = 0; b0 < span_k0; b0++) {
                form z = id;
                for (uint64_t b1 = 0; b1 < (1ULL << k1); b1++) {
                    nucomp_form(
                        z,
                        z,
                        bucket(static_cast<uint32_t>(j), b1 * (1ULL << k0) + b0),
                        D,
                        L);
                }
                z = FastPowFormNucomp(z, D, integer(b0), L, reducer);
                nucomp_form(x, x, z, D, L);
            }
        }

        reducer.reduce(x);

        if (stats_enabled) {
            finalize_total_ns += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started_at)
                    .count());
        }
        return x;
	    }

	    bool stats_ok() const { return stats_enabled; }

	    void record_checkpoint_event_ns(uint64_t ns) {
	        if (stats_enabled) {
	            checkpoint_event_total_ns += ns;
	        }
	    }

	    LastStreamingStats stats() const {
	        LastStreamingStats out;
	        out.checkpoint_total_ns = checkpoint_total_ns;
        out.checkpoint_event_total_ns = checkpoint_event_total_ns;
        out.finalize_total_ns = finalize_total_ns;
        out.checkpoint_calls = checkpoint_calls;
        out.bucket_updates = bucket_updates;
        out.set = stats_enabled;
        return out;
    }

  private:
    form& bucket(uint32_t j, uint64_t b) {
        size_t idx = static_cast<size_t>(j) * bucket_span + static_cast<size_t>(b);
        return buckets[idx];
    }

    const form& bucket(uint32_t j, uint64_t b) const {
        size_t idx = static_cast<size_t>(j) * bucket_span + static_cast<size_t>(b);
        return buckets[idx];
    }

    integer& D;
    integer& L;
    uint64_t wanted_iter;
    uint32_t k;
	    uint32_t l;
	    uint64_t kl;
	    uint64_t limit;
	    integer B;
	    std::vector<form> buckets;

    bool use_getblock_opt;
    bool getblock_ok = true;
    uint64_t getblock_next_p = 0;
    integer getblock_inv_2k;
    integer getblock_r;
    integer getblock_tmp;
    uint64_t batch_start_iteration = 1;
    uint64_t batch_end_iteration = 0;
    std::vector<BatchCheckpoint> current_batch_checkpoints;

	    bool stats_enabled;
	    uint64_t checkpoint_total_ns = 0;
	    uint64_t checkpoint_event_total_ns = 0;
	    mutable uint64_t finalize_total_ns = 0;
	    uint64_t checkpoint_calls = 0;
	    uint64_t bucket_updates = 0;

    bool init_getblock_opt_state() {
        if (k == 0) {
            return false;
        }
        uint64_t k_u64 = static_cast<uint64_t>(k);
        if (wanted_iter < k_u64) {
            getblock_next_p = 0;
            return true;
        }

        integer two_k_mod = FastPow(2, k_u64, B);
        if (mpz_invert(getblock_inv_2k.impl, two_k_mod.impl, B.impl) == 0) {
            return false;
        }

        getblock_r = FastPow(2, wanted_iter - k_u64, B);
        getblock_next_p = 0;
        return true;
    }

    uint64_t get_block_opt(uint64_t p) {
        if (!getblock_ok || wanted_iter < static_cast<uint64_t>(k)) {
            return get_block(p, k, wanted_iter, B);
        }

        // Expected call pattern is sequential `p`. If we ever get out of sync,
        // advance state forward or fall back to the slow mapping.
        if (p < getblock_next_p) {
            return get_block(p, k, wanted_iter, B);
        }
        while (getblock_next_p < p) {
            mpz_mul(getblock_r.impl, getblock_r.impl, getblock_inv_2k.impl);
            mpz_mod(getblock_r.impl, getblock_r.impl, B.impl);
            getblock_next_p++;
        }

        mpz_mul_2exp(getblock_tmp.impl, getblock_r.impl, k);
        mpz_fdiv_q(getblock_tmp.impl, getblock_tmp.impl, B.impl);
        uint64_t b = mpz_get_ui(getblock_tmp.impl);

        mpz_mul(getblock_r.impl, getblock_r.impl, getblock_inv_2k.impl);
        mpz_mod(getblock_r.impl, getblock_r.impl, B.impl);
        getblock_next_p++;

        return b;
    }
};

class StreamingOneWesolowskiCallback final : public WesolowskiCallback {
  public:
    StreamingOneWesolowskiCallback(
        integer& discriminant,
        uint64_t wanted_iter,
        uint32_t k,
        uint32_t l,
        uint64_t limit,
        integer B,
        bool use_getblock_opt,
        uint64_t progress_interval,
        ChiavdfProgressCallback progress_cb,
        void* progress_user_data)
        : WesolowskiCallback(discriminant),
          buckets(
              this->D,
              this->L,
              wanted_iter,
              k,
              l,
              limit,
              std::move(B),
              use_getblock_opt),
          progress_interval(progress_interval),
          progress_cb(progress_cb),
          progress_user_data(progress_user_data),
          next_progress(progress_interval) {}

    bool init_ok() const { return buckets.init_ok(); }

    void OnBatchReplay(uint64_t base_iteration, uint64_t batch_size) override {
        (void)base_iteration;
        (void)batch_size;
        replayed_after_corruption = true;
    }

    void OnIteration(int type, void* data, uint64_t iteration) override {
        if (replayed_after_corruption) {
            return;
        }
        iteration++;
        if (iteration > buckets.wanted_iterations()) {
            return;
        }

        if (progress_cb != nullptr && progress_interval != 0 && iteration >= next_progress) {
            progress_cb(next_progress, progress_user_data);
            next_progress += progress_interval;
        }

	        uint64_t stride = buckets.checkpoint_stride();
	        if (stride != 0 && iteration % stride == 0) {
	            uint64_t pos = iteration / stride;
	            if (pos < buckets.checkpoint_limit()) {
	                form checkpoint;
	                auto started_at = std::chrono::steady_clock::time_point{};
	                const bool do_stats = buckets.stats_ok();
	                if (do_stats) {
	                    started_at = std::chrono::steady_clock::now();
	                }
	                SetForm(type, data, &checkpoint);
	                buckets.process_checkpoint(pos, checkpoint);
	                if (do_stats) {
	                    buckets.record_checkpoint_event_ns(static_cast<uint64_t>(
	                        std::chrono::duration_cast<std::chrono::nanoseconds>(
	                            std::chrono::steady_clock::now() - started_at)
	                            .count()));
	                }
	            }
	        }

        if (iteration == buckets.wanted_iterations()) {
            SetForm(type, data, &result);
            has_result = true;
        }
	    }

	    void process_checkpoint(uint64_t i, const form& checkpoint, bool record_stats = true) {
	        buckets.process_checkpoint(i, checkpoint, record_stats);
	    }

    bool ok() const { return has_result && !replayed_after_corruption; }

	    const form& y() const { return result; }

	    form finalize_proof() const { return buckets.finalize_proof(); }

	    bool stats_ok() const { return buckets.stats_ok(); }

	    LastStreamingStats stats() const { return buckets.stats(); }

	  private:
	    StreamingWesolowskiBuckets buckets;
    uint64_t progress_interval;
    ChiavdfProgressCallback progress_cb;
    void* progress_user_data;
    uint64_t next_progress;

    form result;
    bool has_result = false;
    bool replayed_after_corruption = false;
};

struct BatchJobState {
    size_t index;
    uint64_t wanted_iter;
    uint32_t k;
    uint32_t l;
    uint64_t kl;
    uint64_t limit;
    form y_ref;
    StreamingWesolowskiBuckets buckets;
    uint64_t next_checkpoint_t;
    uint64_t next_event_t;
    bool done = false;

    BatchJobState(
        size_t index,
        uint64_t wanted_iter,
        uint32_t k,
        uint32_t l,
        uint64_t limit,
        form y_ref,
        StreamingWesolowskiBuckets buckets)
        : index(index),
          wanted_iter(wanted_iter),
          k(k),
          l(l),
          kl(static_cast<uint64_t>(k) * static_cast<uint64_t>(l)),
          limit(limit),
          y_ref(std::move(y_ref)),
          buckets(std::move(buckets)),
          next_checkpoint_t(
              (limit <= 1) ? std::numeric_limits<uint64_t>::max()
                           : (static_cast<uint64_t>(k) * static_cast<uint64_t>(l))),
          next_event_t(std::min(wanted_iter, next_checkpoint_t)) {}
};

struct BatchFinalizeLatch {
    void add_task() {
        std::lock_guard<std::mutex> lk(mutex);
        remaining++;
    }

    void task_done() {
        std::lock_guard<std::mutex> lk(mutex);
        if (remaining > 0) {
            remaining--;
        }
        if (remaining == 0) {
            cv.notify_all();
        }
    }

    void wait() {
        std::unique_lock<std::mutex> lk(mutex);
        cv.wait(lk, [this]() { return remaining == 0; });
    }

  private:
    std::mutex mutex;
    std::condition_variable cv;
    size_t remaining = 0;
};

struct BatchFinalizeTask {
    size_t idx;
    int d_bits;
    ChiavdfByteArray* out_arrays;
    form y_ref;
    StreamingWesolowskiBuckets buckets;
    std::shared_ptr<BatchFinalizeLatch> latch;

    BatchFinalizeTask(
        size_t idx,
        int d_bits,
        ChiavdfByteArray* out_arrays,
        form y_ref,
        StreamingWesolowskiBuckets buckets,
        std::shared_ptr<BatchFinalizeLatch> latch)
        : idx(idx),
          d_bits(d_bits),
          out_arrays(out_arrays),
          y_ref(std::move(y_ref)),
          buckets(std::move(buckets)),
          latch(std::move(latch)) {}
};

class GlobalBatchFinalizerPool final {
  public:
    static GlobalBatchFinalizerPool& instance() {
        static GlobalBatchFinalizerPool pool;
        return pool;
    }

    void enqueue(BatchFinalizeTask task) {
        {
            std::lock_guard<std::mutex> lk(mutex);
            queue.push(std::move(task));
        }
        cv.notify_one();
    }

  private:
    GlobalBatchFinalizerPool() { start_workers(); }

    ~GlobalBatchFinalizerPool() {
        {
            std::lock_guard<std::mutex> lk(mutex);
            shutdown = true;
        }
        cv.notify_all();
        for (auto& t : workers) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

    GlobalBatchFinalizerPool(const GlobalBatchFinalizerPool&) = delete;
    GlobalBatchFinalizerPool& operator=(const GlobalBatchFinalizerPool&) = delete;

    void start_workers() {
        size_t count = std::thread::hardware_concurrency();
        if (count == 0) {
            count = 1;
        }
        // Keep this intentionally small: the caller already runs many compute
        // threads (Rust `-p` workers), and each extra C++ worker carries large
        // thread-local GMP scratch state (NUCOMP/NUDUPL).
        count = std::max<size_t>(1, count / 4);
        count = std::min<size_t>(count, 8);

        workers.reserve(count);
        for (size_t i = 0; i < count; i++) {
            workers.emplace_back([this]() { worker_loop(); });
        }
    }

    std::optional<BatchFinalizeTask> take_task() {
        std::unique_lock<std::mutex> lk(mutex);
        cv.wait(lk, [this]() { return shutdown || !queue.empty(); });
        if (queue.empty()) {
            return std::nullopt;
        }
        BatchFinalizeTask task = std::move(queue.front());
        queue.pop();
        return std::make_optional<BatchFinalizeTask>(std::move(task));
    }

    void worker_loop() {
        PulmarkReducer reducer;
        while (true) {
            auto task_opt = take_task();
            if (!task_opt.has_value()) {
                return;
            }
            BatchFinalizeTask task = std::move(*task_opt);

            struct LatchGuard {
                std::shared_ptr<BatchFinalizeLatch> latch;
                ~LatchGuard() {
                    if (latch) {
                        latch->task_done();
                    }
                }
            } guard{task.latch};

            try {
                form proof_form = task.buckets.finalize_proof_with_reducer(reducer);
                std::vector<unsigned char> y_serialized = SerializeForm(task.y_ref, task.d_bits);
                std::vector<unsigned char> proof_serialized = SerializeForm(proof_form, task.d_bits);
                if (y_serialized.empty() || proof_serialized.empty()) {
                    task.out_arrays[task.idx] = empty_result();
                    continue;
                }

                const size_t total = y_serialized.size() + proof_serialized.size();
                uint8_t* out = new uint8_t[total];
                std::copy(y_serialized.begin(), y_serialized.end(), out);
                std::copy(proof_serialized.begin(), proof_serialized.end(), out + y_serialized.size());
                task.out_arrays[task.idx] = ChiavdfByteArray{out, total};
            } catch (...) {
                task.out_arrays[task.idx] = empty_result();
            }
        }
    }

    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable cv;
    std::queue<BatchFinalizeTask> queue;
    bool shutdown = false;
};

class BatchOneWesolowskiCallback final : public WesolowskiCallback {
  public:
    BatchOneWesolowskiCallback(
        integer& D,
        const integer& shared_D,
        const integer& shared_L,
        int d_bits,
        ChiavdfByteArray* out_arrays,
        size_t job_count,
        std::atomic<bool>& stopped,
        std::vector<BatchJobState> jobs,
        uint64_t progress_interval,
        ChiavdfProgressCallback progress_cb,
        void* progress_user_data)
        : WesolowskiCallback(D),
          shared_D(shared_D),
          shared_L(shared_L),
          d_bits(d_bits),
          out_arrays(out_arrays),
          job_count(job_count),
          stopped(stopped),
          jobs(std::move(jobs)),
          progress_interval(progress_interval),
          progress_cb(progress_cb),
	          progress_user_data(progress_user_data),
	          next_progress(progress_interval),
          finalizer_latch(std::make_shared<BatchFinalizeLatch>()) {
    }

	    void initialize(const form& x0) {
	        for (size_t job_pos = 0; job_pos < jobs.size(); job_pos++) {
	            auto& job = jobs[job_pos];
	            job.buckets.process_checkpoint(/*i=*/0, x0, /*record_stats=*/false);
	            schedule_job(job_pos);
	        }
	        refresh_next_event();
	    }

    void OnBatchReplay(uint64_t base_iteration, uint64_t batch_size) override {
        (void)base_iteration;
        (void)batch_size;
        // Streaming bucket updates are irreversible, so fail closed if replay is needed.
        fatal_error = true;
        stopped.store(true);
    }

    void OnIteration(int type, void* data, uint64_t iteration) override {
        if (fatal_error) {
            return;
        }
        iteration++;
        if (progress_cb != nullptr && progress_interval != 0 && iteration >= next_progress) {
            progress_cb(next_progress, progress_user_data);
            next_progress += progress_interval;
        }
        if (iteration != next_event) {
            return;
        }

        form checkpoint;
        SetForm(type, data, &checkpoint);

        while (!event_queue.empty()) {
            const JobEvent next = event_queue.top();
            if (next.t != iteration) {
                break;
            }
            event_queue.pop();

            BatchJobState& job = jobs[next.job_pos];
            if (job.done || job.next_event_t != iteration) {
                continue;
            }

            if (job.next_checkpoint_t == iteration) {
                uint64_t i = iteration / job.kl;
                if (i < job.limit) {
                    job.buckets.process_checkpoint(i, checkpoint);
                }

                const uint64_t next_i = i + 1;
                if (next_i < job.limit) {
                    job.next_checkpoint_t = next_i * job.kl;
                } else {
                    job.next_checkpoint_t = std::numeric_limits<uint64_t>::max();
                }
            }

            if (job.wanted_iter == iteration) {
                if (!(checkpoint == job.y_ref)) {
                    fatal_error = true;
                    stopped.store(true);
                    return;
                }
                spawn_finalize_job(job);
            }

            if (!job.done) {
                schedule_job(next.job_pos);
            }
        }

        refresh_next_event();
    }

    bool ok() const { return !fatal_error; }

    void join_finalizers() {
        finalizer_latch->wait();
    }

  private:
    struct JobEvent {
        uint64_t t;
        size_t job_pos;
    };

    struct JobEventGreater {
        bool operator()(const JobEvent& a, const JobEvent& b) const noexcept { return a.t > b.t; }
    };

    void schedule_job(size_t job_pos) {
        BatchJobState& job = jobs[job_pos];
        if (job.done) {
            job.next_event_t = std::numeric_limits<uint64_t>::max();
            return;
        }
        job.next_event_t = std::min(job.wanted_iter, job.next_checkpoint_t);
        event_queue.push(JobEvent{job.next_event_t, job_pos});
    }

    void refresh_next_event() {
        while (!event_queue.empty()) {
            const JobEvent next = event_queue.top();
            const BatchJobState& job = jobs[next.job_pos];
            if (job.done || job.next_event_t != next.t) {
                event_queue.pop();
                continue;
            }
            next_event = next.t;
            return;
        }
        next_event = std::numeric_limits<uint64_t>::max();
        stopped.store(true);
    }

    void spawn_finalize_job(BatchJobState& job) {
        job.done = true;
        job.next_checkpoint_t = std::numeric_limits<uint64_t>::max();
        job.next_event_t = std::numeric_limits<uint64_t>::max();

        finalizer_latch->add_task();
        GlobalBatchFinalizerPool::instance().enqueue(BatchFinalizeTask(
            job.index,
            d_bits,
            out_arrays,
            std::move(job.y_ref),
            std::move(job.buckets),
            finalizer_latch));
    }

    const integer& shared_D;
    const integer& shared_L;
    int d_bits;
    ChiavdfByteArray* out_arrays;
    size_t job_count;
    std::atomic<bool>& stopped;
    std::vector<BatchJobState> jobs;
    std::shared_ptr<BatchFinalizeLatch> finalizer_latch;
    std::priority_queue<JobEvent, std::vector<JobEvent>, JobEventGreater> event_queue;
    uint64_t next_event = std::numeric_limits<uint64_t>::max();
    uint64_t progress_interval;
    ChiavdfProgressCallback progress_cb;
    void* progress_user_data;
    uint64_t next_progress;
    bool fatal_error = false;
};

ChiavdfByteArray chiavdf_prove_one_weso_fast_streaming_impl(
    const uint8_t* challenge_hash,
    size_t challenge_size,
    const uint8_t* x_s,
    size_t x_s_size,
    const uint8_t* y_ref_s,
    size_t y_ref_s_size,
    size_t discriminant_size_bits,
    uint64_t num_iterations,
    uint64_t progress_interval,
    ChiavdfProgressCallback progress_cb,
    void* progress_user_data,
    bool use_getblock_opt) {
    std::call_once(init_once, init_chiavdf_fast);

    last_streaming_stats = LastStreamingStats{};

    if (challenge_hash == nullptr || challenge_size == 0 || x_s == nullptr || x_s_size == 0 ||
        y_ref_s == nullptr || y_ref_s_size == 0) {
        return empty_result();
    }
    if (num_iterations == 0) {
        return empty_result();
    }

    std::vector<uint8_t> challenge_hash_bytes(challenge_hash, challenge_hash + challenge_size);
    integer D = CreateDiscriminant(challenge_hash_bytes, static_cast<int>(discriminant_size_bits));
    integer L = root(-D, 4);

    form x = DeserializeForm(D, x_s, x_s_size);
    form y_ref = DeserializeForm(D, y_ref_s, y_ref_s_size);

    uint32_t k;
    uint32_t l;
    bool tuned = false;
    const uint64_t budget =
        bucket_memory_budget_bytes.load(std::memory_order_relaxed);
    if (num_iterations >= (1 << 16)) {
        tuned = tune_streaming_parameters(num_iterations, discriminant_size_bits, budget, l, k);
    }
    if (!tuned) {
        if (num_iterations >= (1 << 16)) {
            ApproximateParameters(num_iterations, l, k);
        } else {
            k = 10;
            l = 1;
        }
    }
    if (k == 0) {
        k = 1;
    }
    if (l == 0) {
        l = 1;
    }
    uint64_t ignored_bucket_span = 0;
    if (!try_pow2_u64_shift(k, ignored_bucket_span)) {
        return empty_result();
    }

    last_streaming_parameters.k = k;
    last_streaming_parameters.l = l;
    last_streaming_parameters.tuned = tuned;
    last_streaming_parameters.set = true;

    uint64_t kl = static_cast<uint64_t>(k) * static_cast<uint64_t>(l);
    uint64_t limit = num_iterations / kl;
    if (num_iterations % kl) {
        limit++;
    }

    integer B = GetB(D, x, y_ref);

    std::atomic<bool> stopped(false);
    StreamingOneWesolowskiCallback weso(
        D,
        num_iterations,
        k,
        l,
        limit,
        std::move(B),
        use_getblock_opt,
        progress_interval,
        progress_cb,
        progress_user_data);

    if (!weso.init_ok()) {
        return empty_result();
    }

    weso.process_checkpoint(/*i=*/0, x, /*record_stats=*/false);

    FastStorage* fast_storage = nullptr;
    repeated_square(num_iterations, x, D, L, &weso, fast_storage, stopped);

    if (!weso.ok()) {
        return empty_result();
    }
    if (!(weso.y() == y_ref)) {
        return empty_result();
    }

    form proof_form = weso.finalize_proof();

    if (weso.stats_ok()) {
        last_streaming_stats = weso.stats();
    }

    int d_bits = D.num_bits();
    std::vector<unsigned char> y_serialized = SerializeForm(y_ref, d_bits);
    std::vector<unsigned char> proof_serialized = SerializeForm(proof_form, d_bits);

    if (y_serialized.empty() || proof_serialized.empty()) {
        return empty_result();
    }

    const size_t total = y_serialized.size() + proof_serialized.size();
    uint8_t* out = new uint8_t[total];
    std::copy(y_serialized.begin(), y_serialized.end(), out);
    std::copy(proof_serialized.begin(), proof_serialized.end(), out + y_serialized.size());
    return ChiavdfByteArray{out, total};
}
} // namespace

extern "C" ChiavdfByteArray chiavdf_prove_one_weso_fast(
    const uint8_t* challenge_hash,
    size_t challenge_size,
    const uint8_t* x_s,
    size_t x_s_size,
    size_t discriminant_size_bits,
    uint64_t num_iterations) {
    return chiavdf_prove_one_weso_fast_with_progress(
        challenge_hash,
        challenge_size,
        x_s,
        x_s_size,
        discriminant_size_bits,
        num_iterations,
        /*progress_interval=*/0,
        /*progress_cb=*/nullptr,
        /*progress_user_data=*/nullptr);
}

extern "C" ChiavdfByteArray chiavdf_prove_one_weso_fast_with_progress(
    const uint8_t* challenge_hash,
    size_t challenge_size,
    const uint8_t* x_s,
    size_t x_s_size,
    size_t discriminant_size_bits,
    uint64_t num_iterations,
    uint64_t progress_interval,
    ChiavdfProgressCallback progress_cb,
    void* progress_user_data) {
    try {
        std::call_once(init_once, init_chiavdf_fast);

        if (challenge_hash == nullptr || challenge_size == 0 || x_s == nullptr || x_s_size == 0) {
            return empty_result();
        }
        if (num_iterations == 0) {
            return empty_result();
        }

        std::vector<uint8_t> challenge_hash_bytes(challenge_hash, challenge_hash + challenge_size);
        integer D = CreateDiscriminant(challenge_hash_bytes, static_cast<int>(discriminant_size_bits));
        integer L = root(-D, 4);

        form x = DeserializeForm(D, x_s, x_s_size);

        std::atomic<bool> stopped(false);
        ProgressOneWesolowskiCallback weso(
            D,
            x,
            num_iterations,
            progress_interval,
            progress_cb,
            progress_user_data);

        // Run the fast repeated-squaring engine to `num_iterations`.
        // The callback stores all intermediates needed for the proof.
        FastStorage* fast_storage = nullptr;
        repeated_square(num_iterations, x, D, L, &weso, fast_storage, stopped);

        // Now generate the compact proof from the stored intermediates.
        Proof proof = ProveOneWesolowski(num_iterations, D, x, &weso, stopped);
        if (proof.y.empty() || proof.proof.empty()) {
            return empty_result();
        }

        const size_t total = proof.y.size() + proof.proof.size();
        uint8_t* out = new uint8_t[total];
        std::copy(proof.y.begin(), proof.y.end(), out);
        std::copy(proof.proof.begin(), proof.proof.end(), out + proof.y.size());
        return ChiavdfByteArray{out, total};
    } catch (...) {
        return empty_result();
    }
}

extern "C" ChiavdfByteArray chiavdf_prove_one_weso_fast_streaming(
    const uint8_t* challenge_hash,
    size_t challenge_size,
    const uint8_t* x_s,
    size_t x_s_size,
    const uint8_t* y_ref_s,
    size_t y_ref_s_size,
    size_t discriminant_size_bits,
    uint64_t num_iterations) {
    return chiavdf_prove_one_weso_fast_streaming_with_progress(
        challenge_hash,
        challenge_size,
        x_s,
        x_s_size,
        y_ref_s,
        y_ref_s_size,
        discriminant_size_bits,
        num_iterations,
        /*progress_interval=*/0,
        /*progress_cb=*/nullptr,
        /*progress_user_data=*/nullptr);
}

extern "C" ChiavdfByteArray chiavdf_prove_one_weso_fast_streaming_with_progress(
    const uint8_t* challenge_hash,
    size_t challenge_size,
    const uint8_t* x_s,
    size_t x_s_size,
    const uint8_t* y_ref_s,
    size_t y_ref_s_size,
    size_t discriminant_size_bits,
    uint64_t num_iterations,
    uint64_t progress_interval,
    ChiavdfProgressCallback progress_cb,
    void* progress_user_data) {
    try {
        return chiavdf_prove_one_weso_fast_streaming_impl(
            challenge_hash,
            challenge_size,
            x_s,
            x_s_size,
            y_ref_s,
            y_ref_s_size,
            discriminant_size_bits,
            num_iterations,
            progress_interval,
            progress_cb,
            progress_user_data,
            /*use_getblock_opt=*/false);
    } catch (...) {
        return empty_result();
    }
}

extern "C" ChiavdfByteArray chiavdf_prove_one_weso_fast_streaming_getblock_opt(
    const uint8_t* challenge_hash,
    size_t challenge_size,
    const uint8_t* x_s,
    size_t x_s_size,
    const uint8_t* y_ref_s,
    size_t y_ref_s_size,
    size_t discriminant_size_bits,
    uint64_t num_iterations) {
    return chiavdf_prove_one_weso_fast_streaming_getblock_opt_with_progress(
        challenge_hash,
        challenge_size,
        x_s,
        x_s_size,
        y_ref_s,
        y_ref_s_size,
        discriminant_size_bits,
        num_iterations,
        /*progress_interval=*/0,
        /*progress_cb=*/nullptr,
        /*progress_user_data=*/nullptr);
}

extern "C" ChiavdfByteArray chiavdf_prove_one_weso_fast_streaming_getblock_opt_with_progress(
    const uint8_t* challenge_hash,
    size_t challenge_size,
    const uint8_t* x_s,
    size_t x_s_size,
    const uint8_t* y_ref_s,
    size_t y_ref_s_size,
    size_t discriminant_size_bits,
    uint64_t num_iterations,
    uint64_t progress_interval,
    ChiavdfProgressCallback progress_cb,
    void* progress_user_data) {
    try {
        return chiavdf_prove_one_weso_fast_streaming_impl(
            challenge_hash,
            challenge_size,
            x_s,
            x_s_size,
            y_ref_s,
            y_ref_s_size,
            discriminant_size_bits,
            num_iterations,
            progress_interval,
            progress_cb,
            progress_user_data,
            /*use_getblock_opt=*/true);
    } catch (...) {
        return empty_result();
    }
}

extern "C" void chiavdf_set_bucket_memory_budget_bytes(uint64_t bytes) {
    bucket_memory_budget_bytes.store(bytes, std::memory_order_relaxed);
}

extern "C" void chiavdf_set_enable_streaming_stats(bool enable) {
    streaming_stats_enabled.store(enable, std::memory_order_relaxed);
    last_streaming_stats = LastStreamingStats{};
}

extern "C" bool chiavdf_get_last_streaming_parameters(uint32_t* out_k, uint32_t* out_l, bool* out_tuned) {
    if (out_k == nullptr || out_l == nullptr || out_tuned == nullptr) {
        return false;
    }
    if (!last_streaming_parameters.set) {
        return false;
    }
    *out_k = last_streaming_parameters.k;
    *out_l = last_streaming_parameters.l;
    *out_tuned = last_streaming_parameters.tuned;
    return true;
}

extern "C" bool chiavdf_get_last_streaming_stats(
    uint64_t* out_checkpoint_total_ns,
    uint64_t* out_checkpoint_event_total_ns,
    uint64_t* out_finalize_total_ns,
    uint64_t* out_checkpoint_calls,
    uint64_t* out_bucket_updates) {
    if (out_checkpoint_total_ns == nullptr || out_checkpoint_event_total_ns == nullptr ||
        out_finalize_total_ns == nullptr || out_checkpoint_calls == nullptr ||
        out_bucket_updates == nullptr) {
        return false;
    }
    if (!last_streaming_stats.set) {
        return false;
    }
    *out_checkpoint_total_ns = last_streaming_stats.checkpoint_total_ns;
    *out_checkpoint_event_total_ns = last_streaming_stats.checkpoint_event_total_ns;
    *out_finalize_total_ns = last_streaming_stats.finalize_total_ns;
    *out_checkpoint_calls = last_streaming_stats.checkpoint_calls;
    *out_bucket_updates = last_streaming_stats.bucket_updates;
    return true;
}

extern "C" ChiavdfByteArray* chiavdf_prove_one_weso_fast_streaming_getblock_opt_batch(
    const uint8_t* challenge_hash,
    size_t challenge_size,
    const uint8_t* x_s,
    size_t x_s_size,
    size_t discriminant_size_bits,
    const ChiavdfBatchJob* jobs,
    size_t job_count) {
    return chiavdf_prove_one_weso_fast_streaming_getblock_opt_batch_with_progress(
        challenge_hash,
        challenge_size,
        x_s,
        x_s_size,
        discriminant_size_bits,
        jobs,
        job_count,
        /*progress_interval=*/0,
        /*progress_cb=*/nullptr,
        /*progress_user_data=*/nullptr);
}

extern "C" ChiavdfByteArray* chiavdf_prove_one_weso_fast_streaming_getblock_opt_batch_with_progress(
    const uint8_t* challenge_hash,
    size_t challenge_size,
    const uint8_t* x_s,
    size_t x_s_size,
    size_t discriminant_size_bits,
    const ChiavdfBatchJob* jobs,
    size_t job_count,
    uint64_t progress_interval,
    ChiavdfProgressCallback progress_cb,
    void* progress_user_data) {
    ChiavdfByteArray* out_arrays = nullptr;
    std::unique_ptr<BatchOneWesolowskiCallback> weso;
    bool finalizers_joined = false;
    integer D;
    integer L;
    std::atomic<bool> stopped(false);
    try {
        std::call_once(init_once, init_chiavdf_fast);

        if (challenge_hash == nullptr || challenge_size == 0 || x_s == nullptr || x_s_size == 0 ||
            jobs == nullptr || job_count == 0 || discriminant_size_bits == 0) {
            return nullptr;
        }

        for (size_t idx = 0; idx < job_count; idx++) {
            if (jobs[idx].y_ref_s == nullptr || jobs[idx].y_ref_s_size == 0 ||
                jobs[idx].num_iterations == 0) {
                return nullptr;
            }
        }

        std::vector<uint8_t> challenge_hash_bytes(challenge_hash, challenge_hash + challenge_size);
        D = CreateDiscriminant(challenge_hash_bytes, static_cast<int>(discriminant_size_bits));
        L = root(-D, 4);

        form x0 = DeserializeForm(D, x_s, x_s_size);

        int d_bits = D.num_bits();

        out_arrays = new ChiavdfByteArray[job_count]();

        uint64_t t_max = 0;
        std::vector<BatchJobState> job_states;
        job_states.reserve(job_count);

        const uint64_t budget = bucket_memory_budget_bytes.load(std::memory_order_relaxed);
        const uint64_t per_job_budget = (budget == 0 || job_count == 0)
                                            ? budget
                                            : (budget / static_cast<uint64_t>(job_count));

        for (size_t idx = 0; idx < job_count; idx++) {
            uint64_t num_iterations = jobs[idx].num_iterations;
            t_max = std::max(t_max, num_iterations);

            form y_ref = DeserializeForm(D, jobs[idx].y_ref_s, jobs[idx].y_ref_s_size);

            uint32_t k;
            uint32_t l;
            bool tuned = false;
            if (num_iterations >= (1 << 16)) {
                tuned = tune_streaming_parameters(
                    num_iterations,
                    discriminant_size_bits,
                    per_job_budget,
                    l,
                    k);
            }
            if (!tuned) {
                if (num_iterations >= (1 << 16)) {
                    ApproximateParameters(num_iterations, l, k);
                } else {
                    k = 10;
                    l = 1;
                }
            }
            if (k == 0) {
                k = 1;
            }
            if (l == 0) {
                l = 1;
            }

            uint64_t kl = static_cast<uint64_t>(k) * static_cast<uint64_t>(l);
            uint64_t limit = num_iterations / kl;
            if (num_iterations % kl) {
                limit++;
            }

            integer B = GetB(D, x0, y_ref);

            StreamingWesolowskiBuckets buckets(
                D,
                L,
                num_iterations,
                k,
                l,
                limit,
                std::move(B),
                /*use_getblock_opt=*/true);

            if (!buckets.init_ok()) {
                chiavdf_free_byte_array_batch(out_arrays, job_count);
                return nullptr;
            }

            job_states.emplace_back(
                idx,
                num_iterations,
                k,
                l,
                limit,
                std::move(y_ref),
                std::move(buckets));
        }

        weso = std::make_unique<BatchOneWesolowskiCallback>(
            D,
            D,
            L,
            d_bits,
            out_arrays,
            job_count,
            stopped,
            std::move(job_states),
            progress_interval,
            progress_cb,
            progress_user_data);
        weso->initialize(x0);

        FastStorage* fast_storage = nullptr;
        repeated_square(t_max, x0, D, L, weso.get(), fast_storage, stopped);

        weso->join_finalizers();
        finalizers_joined = true;

        if (!weso->ok()) {
            chiavdf_free_byte_array_batch(out_arrays, job_count);
            return nullptr;
        }

        return out_arrays;
    } catch (...) {
        if (weso != nullptr && !finalizers_joined) {
            try {
                weso->join_finalizers();
            } catch (...) {
            }
        }
        chiavdf_free_byte_array_batch(out_arrays, job_count);
        return nullptr;
    }
}

extern "C" void chiavdf_free_byte_array_batch(ChiavdfByteArray* arrays, size_t count) {
    if (arrays == nullptr) {
        return;
    }
    for (size_t idx = 0; idx < count; idx++) {
        delete[] arrays[idx].data;
        arrays[idx] = empty_result();
    }
    delete[] arrays;
}

extern "C" void chiavdf_free_byte_array(ChiavdfByteArray array) { delete[] array.data; }
