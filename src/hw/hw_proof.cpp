#include "hw_proof.hpp"
#include "bqfc.h"
//#include "vdf.h"

#include <stdexcept>
static const uint32_t g_skip_thres = 10;
static const size_t g_values_mult = 1UL << 12;

void hw_proof_get_form(form *f, struct vdf_state *vdf, struct vdf_value *val);
bool hw_proof_should_queue(struct vdf_state *vdf, uint64_t iters);

static bool hw_proof_overlaps_gap(const struct vdf_proof &proof, uint32_t interval,
        uint64_t from_iters, uint64_t to_iters)
{
    if (interval == 0 || proof.seg_iters == 0 || proof.seg_iters > proof.iters)
        return false;
    uint64_t start = proof.iters - proof.seg_iters;
    uint64_t first = start / interval;
    uint64_t last = proof.iters / interval;
    uint64_t gap_first = from_iters / interval;
    uint64_t gap_last = to_iters / interval;
    return first <= gap_last && gap_first <= last;
}

// One store. Callers that name a proof take aux_job_mtx first; GetForm stores directly.
static void hw_proof_cancel_slot(struct vdf_state *vdf, uint8_t slot)
{
    if (slot < HW_VDF_MAX_AUX_THREADS)
        vdf->aux_cancel[slot].store(1, std::memory_order_release);
}

// Stop every proof job still bound to a segment that has been marked failed.
// Caller holds proofs_resize_mtx. Fills reuse proof_idx 0 and are not proofs.
static void hw_proof_cancel_failed(struct vdf_state *vdf)
{
    std::lock_guard<std::mutex> lock(vdf->aux_job_mtx);
    for (uint8_t slot = 0; slot < vdf->max_aux_threads; slot++) {
        if (vdf->aux_jobs[slot].work != nullptr)
            continue;
        size_t idx = vdf->aux_jobs[slot].proof_idx;
        if (idx < vdf->proofs.size() && (vdf->proofs[idx].flags & HW_VDF_PROOF_FLAG_FAILED))
            hw_proof_cancel_slot(vdf, slot);
    }
}

// A fill that stops short never publishes this grid range. Proofs that read
// it fail, and waiters on those positions are released.
static void hw_proof_abandon_gap(struct vdf_state *vdf, uint64_t from_iters, uint64_t to_iters)
{
    if (from_iters > to_iters || vdf->interval == 0)
        return;
    {
        std::lock_guard<std::mutex> lock(vdf->proofs_resize_mtx);
        for (size_t i = 0; i < vdf->proofs.size(); i++) {
            struct vdf_proof &proof = vdf->proofs[i];
            if (proof.flags == 0xff || (proof.flags & HW_VDF_PROOF_FLAG_DONE))
                continue;
            if (!hw_proof_overlaps_gap(proof, vdf->interval, from_iters, to_iters))
                continue;
            proof.flags &= ~HW_VDF_PROOF_FLAG_DONE;
            proof.flags |= HW_VDF_PROOF_FLAG_FAILED;
        }
        hw_proof_cancel_failed(vdf);
        size_t dst = 0;
        for (size_t src = 0; src < vdf->queued_proofs.size(); src++) {
            uint16_t idx = vdf->queued_proofs[src];
            if (idx < vdf->proofs.size() && (vdf->proofs[idx].flags & HW_VDF_PROOF_FLAG_FAILED))
                continue;
            vdf->queued_proofs[dst++] = idx;
        }
        vdf->queued_proofs.resize(dst);
    }
    if (vdf->abandoned_values && vdf->valid_values) {
        size_t first = (size_t)(from_iters / vdf->interval);
        size_t last = (size_t)(to_iters / vdf->interval);
        size_t npos = vdf->value_block_count * g_values_mult;
        std::lock_guard<std::mutex> lock(vdf->valid_values_mtx);
        for (size_t pos = first; pos <= last && pos < npos; pos++) {
            uint8_t bit = (uint8_t)(1u << (pos % 8));
            if (vdf->valid_values[pos / 8].load(std::memory_order_relaxed) & bit)
                continue;
            vdf->abandoned_values[pos / 8].fetch_or(bit, std::memory_order_release);
        }
    }
    vdf->valid_values_cv.notify_all();
}

// One exit for an aux job. Unpublished proofs stay unpublished. A failed
// segment is marked so retrieval does not wait on its chain. A value job
// that does not finish abandons the grid range it still owed. A direct
// caller passes a slot outside the worker array and only updates the proof.
static void hw_proof_finish_job(struct vdf_state *vdf, uint8_t slot, bool proving,
        size_t proof_idx, bool published, bool track_gap = false,
        uint64_t gap_from = 0, uint64_t gap_to = 0)
{
    if (proving && !published && proof_idx != SIZE_MAX) {
        std::lock_guard<std::mutex> lock(vdf->proofs_resize_mtx);
        if (proof_idx < vdf->proofs.size()) {
            vdf->proofs[proof_idx].flags &= ~HW_VDF_PROOF_FLAG_DONE;
            vdf->proofs[proof_idx].flags |= HW_VDF_PROOF_FLAG_FAILED;
        }
        hw_proof_cancel_failed(vdf);
    }
    if (!proving && !published && track_gap)
        hw_proof_abandon_gap(vdf, gap_from, gap_to);
    if (slot < HW_VDF_MAX_AUX_THREADS) {
        if (proving)
            vdf->n_proof_threads -= PARALLEL_PROVER_N_THREADS;
        vdf->aux_threads_busy &= ~(1UL << slot);
    }
}

struct hw_job_exit {
    struct vdf_state *vdf;
    uint8_t slot;
    bool proving;
    size_t proof_idx;
    bool published = false;
    bool track_gap = false;
    uint64_t gap_from = 0;
    uint64_t gap_to = 0;

    hw_job_exit(struct vdf_state *vdf, uint8_t slot, bool proving, size_t proof_idx)
        : vdf(vdf), slot(slot), proving(proving), proof_idx(proof_idx) {}
    hw_job_exit(const hw_job_exit &) = delete;
    hw_job_exit &operator=(const hw_job_exit &) = delete;
    ~hw_job_exit() {
        hw_proof_finish_job(vdf, slot, proving, proof_idx, published, track_gap, gap_from, gap_to);
    }
};

void report_bad_vdf_value(struct vdf_state *vdf, struct vdf_value *val)
{
    vdf->n_bad++;
    LOG_INFO("VDF %d: Warning: Bad VDF value at iters=%lu n_bad=%u",
            vdf->idx, val->iters, vdf->n_bad);
}

int verify_vdf_value(struct vdf_state *vdf, struct vdf_value *val)
{
    mpz_mul(vdf->a2.impl, val->b, val->b);
    mpz_sub(vdf->a2.impl, vdf->a2.impl, vdf->D.impl);
    /* Verify that c could be computed as c = (b^2 - d) / (4 * a) */
    if (!mpz_divisible_p(vdf->a2.impl, val->a) || mpz_scan1(vdf->a2.impl, 0) < mpz_scan1(val->a, 0) + 2) {
        report_bad_vdf_value(vdf, val);
        return -1;
    }
    return 0;
}

int hw_proof_add_value(struct vdf_state *vdf, struct vdf_value *val)
{
    val->iters += vdf->iters_offset;
    if (val->iters == vdf->iters_offset || val->iters == vdf->last_val.iters) {
        vdf->n_skipped++;
        LOG_INFO("VDF %d: Skipping iters=%lu n_skipped=%u",
                vdf->idx, val->iters, vdf->n_skipped);
        if (vdf->n_skipped > g_skip_thres) {
            vdf->n_skipped = 0;
            return -1;
        }
        return 1;
    }
    vdf->n_skipped = 0;

    if (mpz_sgn(val->a) == 0) {
        report_bad_vdf_value(vdf, val);
        return -1;
    }
    // b = b (mod 2*a)
    mpz_mul_2exp(vdf->a2.impl, val->a, 1);
    mpz_mod(val->b, val->b, vdf->a2.impl);

    if (verify_vdf_value(vdf, val)) {
        return -1;
    }
    if (vdf->retain_asic_values) {
        form value;
        hw_proof_get_form(&value, vdf, val);
        std::lock_guard<std::mutex> lock(vdf->retained_values_mtx);
        vdf->retained_values.push_back({vdf->retained_generation, val->iters, std::move(value)});
        if (vdf->retained_values.size() > 64) vdf->retained_values.pop_front();
    }
    hw_proof_handle_value(vdf, val);
    return 0;
}

void hw_proof_get_form(form *f, struct vdf_state *vdf, struct vdf_value *val)
{
    integer a, b;

    mpz_swap(a.impl, val->a);
    mpz_swap(b.impl, val->b);

    *f = form::from_abd(a, b, vdf->D);

    mpz_swap(a.impl, val->a);
    mpz_swap(b.impl, val->b);
}

bool hw_proof_copy_retained_value(struct vdf_state *vdf, uint64_t target, uint64_t floor,
                                  form *value, uint64_t *iters)
{
    std::lock_guard<std::mutex> lock(vdf->retained_values_mtx);
    for (auto sample = vdf->retained_values.rbegin(); sample != vdf->retained_values.rend(); ++sample) {
        if (sample->generation == vdf->retained_generation && sample->iters <= target && sample->iters > floor) {
            *value = sample->value;
            *iters = sample->iters;
            return true;
        }
    }
    return false;
}

void hw_proof_reset_retained_values(struct vdf_state *vdf)
{
    std::lock_guard<std::mutex> lock(vdf->retained_values_mtx);
    ++vdf->retained_generation;
    vdf->retained_values.clear();
}

static void hw_proof_drop_retained_sample(struct vdf_state *vdf, uint64_t iters)
{
    std::lock_guard<std::mutex> lock(vdf->retained_values_mtx);
    for (size_t i = vdf->retained_values.size(); i-- > 0; ) {
        const vdf_retained_value &sample = vdf->retained_values[i];
        if (sample.generation == vdf->retained_generation && sample.iters == iters) {
            vdf->retained_values.erase(vdf->retained_values.begin() + i);
            return;
        }
    }
}

static void hw_square_form(form *f, struct vdf_state *vdf, uint64_t steps)
{
    PulmarkReducer reducer;
    for (uint64_t i = 0; i < steps; i++) {
        nudupl_form(*f, *f, vdf->D, vdf->L);
        reducer.reduce(*f);
    }
}

void hw_proof_print_stats(struct vdf_state *vdf, uint64_t elapsed_us, bool detail)
{
    uint64_t sw_elapsed_us = vdf->elapsed_us;
    uint64_t sw_iters = vdf->done_iters;
    uint64_t ips, sw_ips;

    elapsed_us = elapsed_us ? elapsed_us : 1;
    sw_elapsed_us = sw_elapsed_us ? sw_elapsed_us : 1;
    ips = vdf->cur_iters * 1000000 / elapsed_us;
    sw_ips = sw_iters * 1000000 / sw_elapsed_us;

    LOG_INFO("");
    LOG_INFO("VDF %d: %lu HW iters done in %lus, HW speed: %lu ips",
            vdf->idx, vdf->cur_iters, elapsed_us / 1000000, ips);
    LOG_INFO("VDF %d: %lu SW iters done in %lus, SW speed: %lu ips",
            vdf->idx, sw_iters, sw_elapsed_us / 1000000, sw_ips);
    if (detail && vdf->done_values) {
        uint64_t done_values = vdf->done_values;
        LOG_INFO("VDF %d: Avg iters per intermediate: %lu",
                vdf->idx, sw_iters / done_values);
        if (vdf->n_bad > 0) {
            LOG_INFO("VDF %d: Bad VDF values observed: %u", vdf->idx, vdf->n_bad);
        }
    }
    LOG_INFO("");
}

form *hw_proof_value_at(struct vdf_state *vdf, size_t pos)
{
    size_t idx = pos / g_values_mult;
    if (idx >= vdf->value_block_count) throw std::out_of_range("Intermediate exceeds VDF target");
    // The directory is fixed for this challenge; allocated blocks live until stop.
    form *block = vdf->value_blocks[idx].load(std::memory_order_acquire);
    if (!block) {
        std::lock_guard<std::mutex> lock(vdf->values_resize_mtx);
        block = vdf->value_blocks[idx].load(std::memory_order_relaxed);
        if (!block) {
            block = new form[g_values_mult];
            vdf->value_blocks[idx].store(block, std::memory_order_release);
            LOG_INFO("VDF %d: Allocating intermediate block %zu", vdf->idx, idx);
        }
    }
    return &block[pos % g_values_mult];
}

form *hw_proof_last_good_form(struct vdf_state *vdf, size_t *out_pos)
{
    size_t pos = vdf->cur_iters / vdf->interval;

    while (!(vdf->valid_values[pos / 8].load(std::memory_order_acquire) & (1 << (pos % 8)))) {
        pos--;
    }
    *out_pos = pos;
    return hw_proof_value_at(vdf, pos);
}

void hw_proof_add_intermediate(struct vdf_state *vdf, struct vdf_value *val, size_t pos)
{
    if (val) {
        hw_proof_get_form(hw_proof_value_at(vdf, pos), vdf, val);
    }
    vdf->valid_values_mtx.lock();
    vdf->valid_values[pos / 8].fetch_or(1 << (pos % 8), std::memory_order_release);
    vdf->valid_values_mtx.unlock();
    vdf->valid_values_cv.notify_all();
    vdf->done_values++;
}

void hw_proof_calc_values(struct vdf_state *vdf, struct vdf_work *work, int thr_idx)
{
    hw_job_exit job(vdf, (uint8_t)thr_idx, false, SIZE_MAX);
    struct vdf_value *val = &work->start_val;
    uint64_t next_iters = work->start_iters;
    uint32_t n_steps = work->n_steps;

    integer a(val->a), b(val->b);
    //const integer a = {val->a}, b = {val->b}, d = {vdf->d}, l = {vdf->l};
    uint64_t end_iters = next_iters + vdf->interval * n_steps;
    uint64_t iters = val->iters;
    job.track_gap = true;
    job.gap_from = next_iters;
    job.gap_to = end_iters;
    clear_vdf_value(val);
    delete work;
    form f = form::from_abd(a, b, vdf->D);
    PulmarkReducer reducer;
    timepoint_t t1;
    uint64_t init_iters = iters;

    LOG_DEBUG(" VDF %d: computing %lu iters (%lu -> %lu, %u steps) in aux thread %d",
            vdf->idx, end_iters - iters, iters, end_iters, n_steps, thr_idx);
    t1 = vdf_get_cur_time();
    do {
        if (vdf->stopping) {
            break;
        }
        nudupl_form(f, f, vdf->D, vdf->L);
        reducer.reduce(f);
        iters++;

        if (iters == next_iters) {
            size_t pos = iters / vdf->interval;

            if (!f.check_valid(vdf->D)) {
                LOG_ERROR(" VDF %d: bad form at iters=%lu", vdf->idx, iters);
                return;
            }

            *hw_proof_value_at(vdf, pos) = f;
            hw_proof_add_intermediate(vdf, NULL, pos);

            next_iters += vdf->interval;
            job.gap_from = next_iters;
        }

    } while (iters < end_iters);

    vdf->done_iters += iters - init_iters;
    vdf->elapsed_us += vdf_get_elapsed_us(t1);
    LOG_DEBUG(" VDF %d: aux thread %d done", vdf->idx, thr_idx);
    job.published = true;
    job.track_gap = false;
}

void hw_proof_add_work(struct vdf_state *vdf, uint64_t next_iters, uint32_t n_steps)
{
    auto *work = new struct vdf_work;

    copy_vdf_value(&work->start_val, &vdf->last_val);
    work->start_iters = next_iters;
    work->n_steps = n_steps;

    vdf->wq.push_back(work);
}

static bool hw_proof_submit(struct vdf_state *vdf, int slot, struct vdf_work *work, size_t proof_idx)
{
    {
        std::lock_guard<std::mutex> lock(vdf->aux_job_mtx);
        if (vdf->stopping) return false;
        vdf->aux_cancel[slot].store(0, std::memory_order_relaxed);
        vdf->aux_jobs[slot].work = work;
        vdf->aux_jobs[slot].proof_idx = proof_idx;
        vdf->aux_job_pending |= 1ULL << slot;
    }
    vdf->aux_job_cv.notify_all();
    return true;
}

void hw_proof_process_work(struct vdf_state *vdf)
{
    // The event-loop planner and proof publishers share all proof metadata.
    std::lock_guard<std::mutex> lock(vdf->proofs_resize_mtx);
    uint64_t busy = vdf->aux_threads_busy;
    uint32_t qlen;

    if (vdf->stopping) return;

    while (!vdf->req_proofs.empty() && (vdf->queued_proofs.size() < 3 ||
                hw_proof_should_queue(vdf, vdf->req_proofs[0].iters))) {
        hw_proof_process_req(vdf);
    }

    for (int i = 0; i < vdf->max_aux_threads; i++) {
        uint64_t iters;
        struct vdf_proof *proof;
        size_t idx;

        if (vdf->queued_proofs.empty()) {
            break;
        }
        idx = vdf->queued_proofs[0];
        proof = &vdf->proofs[idx];
        iters = proof->iters;
        if (vdf->last_val.iters < iters ||
                vdf->n_proof_threads >= vdf->max_proof_threads) {
            break;
        }

        if (!(busy & (1UL << i))) {
            bool is_chkp = !(proof->flags & HW_VDF_PROOF_FLAG_IS_REQ);

            LOG_INFO("VDF %d: Starting proof thread %d for iters=%lu, length=%lu%s",
                    vdf->idx, i, iters, proof->seg_iters, is_chkp ? " [checkpoint]" : "");
            vdf->queued_proofs.erase(vdf->queued_proofs.begin());
            vdf->aux_threads_busy |= 1UL << i;
            vdf->n_proof_threads += PARALLEL_PROVER_N_THREADS;
            proof->flags |= HW_VDF_PROOF_FLAG_STARTED;
            if (!hw_proof_submit(vdf, i, nullptr, idx)) {
                vdf->aux_threads_busy &= ~(1UL << i);
                vdf->n_proof_threads -= PARALLEL_PROVER_N_THREADS;
                proof->flags &= ~HW_VDF_PROOF_FLAG_STARTED;
                proof->flags |= HW_VDF_PROOF_FLAG_FAILED;
            }
        }
    }

    busy = vdf->aux_threads_busy;

    for (int i = 0; i < vdf->max_aux_threads; i++) {
        if (!(busy & (1UL << i))) {
            struct vdf_work *work;

            if (vdf->wq.empty()) {
                break;
            }

            work = vdf->wq.front();
            vdf->wq.pop_front();
            //struct vdf_value *val = &vdf->raw_values[work.raw_idx];

            vdf->aux_threads_busy |= 1UL << i;
            if (!hw_proof_submit(vdf, i, work, 0)) {
                vdf->aux_threads_busy &= ~(1UL << i);
                clear_vdf_value(&work->start_val);
                delete work;
            }
        }
    }

    qlen = vdf->wq.size();
    if (qlen >= vdf->wq_warn_thres[1]) {
        vdf->wq_warn_thres[0] *= HW_VDF_WQ_WARN_MULT;
        vdf->wq_warn_thres[1] *= HW_VDF_WQ_WARN_MULT;
        LOG_INFO("VDF %d: Warning: too much work for VDF aux threads; qlen=%u",
                vdf->idx, qlen);
    } else if (vdf->wq_warn_thres[0] != 1 && qlen < vdf->wq_warn_thres[0]) {
        vdf->wq_warn_thres[0] /= HW_VDF_WQ_WARN_MULT;
        vdf->wq_warn_thres[1] /= HW_VDF_WQ_WARN_MULT;
        LOG_INFO("VDF %d: Work queue for VDF aux threads reduced; qlen=%u",
                vdf->idx, qlen);
    }
}

void hw_proof_wait_values(struct vdf_state *vdf, bool finish_work)
{
    if (finish_work) {
        while (!vdf->wq.empty() && !vdf->stopping) {
            vdf_usleep(100000);
            hw_proof_process_work(vdf);
        }
    }

    while (vdf->aux_threads_busy && !vdf->stopping) {
        vdf_usleep(10000);
    }

    if (!finish_work) {
        for (size_t i = 0; i < vdf->wq.size(); i++) {
            clear_vdf_value(&vdf->wq[i]->start_val);
            delete vdf->wq[i];
        }
        vdf->wq.clear();
    }
}

int hw_proof_wait_value(struct vdf_state *vdf, size_t pos)
{
    const uint8_t bit = 1 << (pos % 8);
    auto abandoned = [=] {
        return vdf->abandoned_values
            && (vdf->abandoned_values[pos / 8].load(std::memory_order_acquire) & bit);
    };
    // Most values are already published by the time a proof consumes them.
    // Acquire pairs with producer publication without locking on that path.
    if (vdf->valid_values[pos / 8].load(std::memory_order_acquire) & bit)
        return vdf->stopping ? -1 : 0;
    if (abandoned())
        return -1;
    std::unique_lock<std::mutex> lock(vdf->valid_values_mtx);
    vdf->valid_values_cv.wait(lock, [=] {
        return vdf->stopping
            || (vdf->valid_values[pos / 8].load(std::memory_order_acquire) & bit)
            || abandoned();
    });
    return (vdf->stopping || abandoned()) ? -1 : 0;
}

void hw_proof_handle_value(struct vdf_state *vdf, struct vdf_value *val)
{
    uint64_t interval = vdf->interval;
    //uint64_t iters = vdf->raw_values.back().iters;
    uint64_t last_iters = vdf->last_val.iters;
    //uint64_t start_iters = iters;
    //uint64_t end_iters = val->iters;
    //uint64_t mask = interval - 1;
    //start_iters = (start_iters + mask) & ~mask;
    //end_iters &= ~mask;
    uint64_t start_iters = last_iters / interval * interval + interval;
    uint64_t end_iters = val->iters / interval * interval;
    uint64_t elapsed_us;
    uint64_t log_interval = 10 * 1000000;
    bool print_stats = false;

    if (val->iters == end_iters) {
        hw_proof_add_intermediate(vdf, val, val->iters / interval);
        if (end_iters) {
            end_iters -= interval;
        }
    }
    //iters = start_iters;

    if (end_iters && start_iters <= end_iters) {
        uint32_t n_steps = (end_iters - start_iters) / interval;
        //fprintf(stderr, "VDF %d: computing target iters=%lu delta=%lu\n",
                //vdf->idx, end_iters, end_iters - iters);
        if (start_iters > vdf->target_iters) {
            LOG_ERROR("VDF %d: Fail at iters=%lu end_iters=%lu",
                    vdf->idx, last_iters, end_iters);
            // The sample is not applied. Proofs already covered by last_val still start.
            hw_proof_process_work(vdf);
            return;
        }
        //iters += interval;
        hw_proof_add_work(vdf, start_iters, n_steps);
    }

    //vdf->raw_values.push_back(*val);
    vdf->cur_iters = val->iters;
    std::swap(vdf->last_val, *val);

    elapsed_us = vdf_get_elapsed_us(vdf->start_time);
    if (elapsed_us / log_interval > vdf->log_cnt) {
        vdf->log_cnt = elapsed_us / log_interval;
        print_stats = true;
    }
    if (print_stats || vdf->cur_iters >= vdf->target_iters) {
        hw_proof_print_stats(vdf, elapsed_us, false);
    }
    if (vdf->cur_iters >= vdf->target_iters) {
        vdf->completed = true;
    }

    hw_proof_process_work(vdf);
}

void hw_request_stop_proof(struct vdf_state *vdf)
{
    {
        std::lock_guard<std::mutex> jobs(vdf->aux_job_mtx);
        std::lock_guard<std::mutex> values(vdf->valid_values_mtx);
        vdf->stopping = true;
    }
    vdf->valid_values_cv.notify_all();
    vdf->aux_job_cv.notify_all();
}

void hw_stop_proof(struct vdf_state *vdf)
{
    hw_request_stop_proof(vdf);
    for (auto &worker : vdf->aux_threads) {
        if (worker.joinable()) worker.join();
    }
    hw_proof_print_stats(vdf, vdf_get_elapsed_us(vdf->start_time), true);
    hw_proof_wait_values(vdf, false);
}

class HwProver : public ParallelProver {
  public:
    HwProver(Segment segm, integer D, struct vdf_state *vdf, uint8_t slot)
        : ParallelProver(segm, D)
    {
        this->vdf = vdf;
        this->slot = slot;
        k = FindK(segm.length);
        l = vdf->interval / k;
        pos_offset = segm.start / vdf->interval;
    }

    form GetForm(uint64_t pos) override {
        pos += pos_offset;
        if (hw_proof_wait_value(vdf, pos)) {
            hw_proof_cancel_slot(vdf, slot);
            // Provide arbitrary value when stopping; no proof is published.
            return *hw_proof_value_at(vdf, 0);
        }
        return *hw_proof_value_at(vdf, pos);
    }

    void start() override {
        GenerateProof();
    }

    void stop() override {
    }

    bool PerformExtraStep() override {
        return !vdf->stopping && (slot >= HW_VDF_MAX_AUX_THREADS
                || !vdf->aux_cancel[slot].load(std::memory_order_acquire));
    }

    void OnFinish() override {
        is_finished = true;
    }

    bool IsFinished() {
        return is_finished;
    }

    uint32_t FindK(uint64_t iters) {
        uint8_t d = 1;
        const uint8_t divisors[] = HW_VDF_VALUE_INTERVAL_DIVISORS;
        uint64_t interval = vdf->interval;
        uint64_t n_steps = iters + interval * 4, n_steps2;
        size_t i;

        for (i = 0; i < sizeof(divisors) / sizeof(*divisors); i++) {
            d = divisors[i];
            n_steps2 = iters / d + ((interval / d) << (d + 1));

            if (n_steps2 > n_steps) {
                return i ? divisors[i - 1] : 1;
            }
            n_steps = n_steps2;
        }
        return divisors[i - 1];
    }

  private:
    struct vdf_state *vdf;
    uint8_t slot = HW_VDF_MAX_AUX_THREADS;
    uint32_t pos_offset;
};

void hw_compute_proof(struct vdf_state *vdf, size_t proof_idx, struct vdf_proof *out_proof, uint8_t thr_idx)
{
    hw_job_exit job(vdf, thr_idx, true, proof_idx);
    form x, y, proof_val;
    uint64_t proof_iters, start_iters, iters;
    size_t pos, start_pos;
    bool is_chkp;
    timepoint_t start_time = vdf_get_cur_time();

    vdf->proofs_resize_mtx.lock();
    if (proof_idx != SIZE_MAX) {
        out_proof = &vdf->proofs[proof_idx];
    }

    proof_iters = out_proof->iters;
    start_iters = proof_iters - out_proof->seg_iters;
    is_chkp = !(out_proof->flags & HW_VDF_PROOF_FLAG_IS_REQ);
    vdf->proofs_resize_mtx.unlock();

    start_pos = start_iters / vdf->interval;
    pos = proof_iters / vdf->interval;
    iters = pos * vdf->interval;

    if (hw_proof_wait_value(vdf, start_pos) || hw_proof_wait_value(vdf, pos)) {
        LOG_INFO("VDF %d: Proof stopped", vdf->idx);
        goto out;
    }
    x = *hw_proof_value_at(vdf, start_pos);
    y = *hw_proof_value_at(vdf, pos);
    if (vdf->retain_asic_values) {
        form sample;
        uint64_t sample_iters = 0;
        if (hw_proof_copy_retained_value(vdf, proof_iters, iters, &sample, &sample_iters)
                && sample_iters >= iters && sample_iters <= proof_iters) {
            form forward = y;
            hw_square_form(&forward, vdf, sample_iters - iters);
            if (forward == sample) {
                y = std::move(sample);
                iters = sample_iters;
            } else {
                hw_proof_drop_retained_sample(vdf, sample_iters);
            }
        }
    }
    if (!y.check_valid(vdf->D)) {
        LOG_ERROR("VDF %d: invalid form at pos=%lu", vdf->idx, pos);
        goto out;
    }
    hw_square_form(&y, vdf, proof_iters - iters);
    if (!y.check_valid(vdf->D)) {
        LOG_ERROR("VDF %d: invalid y", vdf->idx);
        goto out;
    }


    {
        Segment seg(start_iters, proof_iters - start_iters, x, y);
        HwProver prover(seg, vdf->D, vdf, thr_idx);
        prover.start();

        if (prover.IsFinished()) {
            size_t d_bits;
            uint64_t elapsed_us = vdf_get_elapsed_us(start_time);
            bool is_valid = false;
            proof_val = prover.GetProof();

            LOG_INFO("VDF %d: Proof done for iters=%lu, length=%lu in %.3fs%s",
                    vdf->idx, proof_iters, seg.length,
                    (double)elapsed_us / 1000000, is_chkp ? " [checkpoint]" : "");

            VerifyWesolowskiProof(vdf->D, x, y, proof_val, seg.length, is_valid);
            if (!is_valid) {
                LOG_ERROR("VDF %d: Proof NOT VALID", vdf->idx);
            } else {
                d_bits = mpz_sizeinbase(vdf->D.impl, 2);
                std::lock_guard<std::mutex> publish(vdf->proofs_resize_mtx);
                if (proof_idx != SIZE_MAX)
                    out_proof = &vdf->proofs[proof_idx];
                bqfc_serialize(out_proof->y, y.a.impl, y.b.impl, d_bits);
                bqfc_serialize(out_proof->proof, proof_val.a.impl, proof_val.b.impl, d_bits);
                if (proof_idx != SIZE_MAX && (out_proof->flags & HW_VDF_PROOF_FLAG_FAILED)) {
                    LOG_ERROR("VDF %d: Proof dropped", vdf->idx);
                } else {
                    if (!(out_proof->flags & HW_VDF_PROOF_FLAG_IS_REQ)) {
                        integer B = GetB(vdf->D, x, y);
                        mpz_export(out_proof->B, NULL, 1, 1, 0, 0, B.impl);
                    }
                    if (out_proof->flags & (HW_VDF_PROOF_FLAG_IS_REQ | HW_VDF_PROOF_FLAG_EXACT_REQ))
                        vdf->done_proofs.push_back((uint16_t)proof_idx);
                    out_proof->flags |= HW_VDF_PROOF_FLAG_DONE;
                    job.published = true;
                }
            }
        } else {
            LOG_INFO("VDF %d: Proof stopped", vdf->idx);
        }
    }

out:
    return;
}

static void hw_aux_worker(struct vdf_state *vdf, uint8_t slot)
{
    for (;;) {
        struct vdf_work *work = nullptr;
        size_t proof_idx = 0;
        {
            std::unique_lock<std::mutex> lock(vdf->aux_job_mtx);
            vdf->aux_job_cv.wait(lock, [&] {
                return vdf->stopping.load() || (vdf->aux_job_pending & (1ULL << slot));
            });
            if ((vdf->aux_job_pending & (1ULL << slot)) == 0)
                return;
            work = vdf->aux_jobs[slot].work;
            proof_idx = vdf->aux_jobs[slot].proof_idx;
            vdf->aux_job_pending &= ~(1ULL << slot);
        }
        try {
            if (work)
                hw_proof_calc_values(vdf, work, slot);
            else
                hw_compute_proof(vdf, proof_idx, nullptr, slot);
        } catch (const std::exception &ex) {
            LOG_ERROR("VDF %d: aux thread %u dropped a job: %s", vdf->idx, slot, ex.what());
        }
    }
}

enum class hw_chain { ready, waiting, failed };

static hw_chain hw_proof_chain(const struct vdf_state *vdf, uint16_t head, int *checkpoints)
{
    *checkpoints = 0;
    if (vdf->proofs[head].flags & HW_VDF_PROOF_FLAG_FAILED)
        return hw_chain::failed;
    for (uint16_t j = vdf->proofs[head].prev; j != HW_VDF_PROOF_NONE; j = vdf->proofs[j].prev) {
        uint8_t flags = vdf->proofs[j].flags;
        if (flags & HW_VDF_PROOF_FLAG_FAILED)
            return hw_chain::failed;
        if (!(flags & HW_VDF_PROOF_FLAG_DONE))
            return hw_chain::waiting;
        ++*checkpoints;
    }
    return hw_chain::ready;
}

int hw_retrieve_proof(struct vdf_state *vdf, struct vdf_proof_delivery *out_proof)
{
    std::lock_guard<std::mutex> lock(vdf->proofs_resize_mtx);
    for (size_t i = 0; i < vdf->done_proofs.size(); ) {
        uint16_t idx = vdf->done_proofs[i];
        struct vdf_proof *proof = &vdf->proofs[idx];
        int cnt = 0;
        hw_chain chain = hw_proof_chain(vdf, idx, &cnt);
        if (chain == hw_chain::waiting) {
            ++i;
            continue;
        }
        vdf->done_proofs.erase(vdf->done_proofs.begin() + i);
        if (chain == hw_chain::failed)
            continue;
        out_proof->final = *proof;
        out_proof->checkpoints.clear();
        for (uint16_t j = proof->prev; j != HW_VDF_PROOF_NONE; j = vdf->proofs[j].prev)
            out_proof->checkpoints.push_back(vdf->proofs[j]);
        return cnt;
    }
    return -1;
}

void init_vdf_state(struct vdf_state *vdf, struct vdf_proof_opts *opts, const char *d_str, const uint8_t *init_form, uint64_t n_iters, uint8_t idx)
{
    //int ret;
    //struct vdf_value initial;
    size_t num_values;
    vdf->retain_asic_values = opts && opts->retain_asic_values;
    hw_proof_reset_retained_values(vdf);
    vdf->cur_iters = 0;
    vdf->iters_offset = 0;
    vdf->done_values = 1;
    vdf->done_iters = 0;
    vdf->elapsed_us = 0;
    vdf->start_time = vdf_get_cur_time();
    vdf->interval = HW_VDF_VALUE_INTERVAL;
    vdf->chkp_interval = HW_VDF_CHKP_INTERVAL;
    vdf->checkpoint_threshold = opts ? opts->checkpoint_threshold : HW_VDF_CHKP_INTERVAL;
    if (vdf->checkpoint_threshold < HW_VDF_MIN_CHKP_THRESHOLD) {
        throw std::invalid_argument("Checkpoint threshold must be at least twice the value interval");
    }
    vdf->target_iters = (n_iters + vdf->interval - 1) / vdf->interval * vdf->interval;
    vdf->idx = idx;
    vdf->completed = false;
    vdf->stopping = false;
    vdf->aux_job_pending = 0;
    vdf->aux_threads_busy = 0;
    vdf->n_proof_threads = 0;
    vdf->n_bad = 0;
    vdf->n_skipped = 0;
    vdf->log_cnt = 0;
    for (uint8_t slot = 0; slot < HW_VDF_MAX_AUX_THREADS; slot++)
        vdf->aux_cancel[slot].store(0, std::memory_order_relaxed);
    vdf->wq_warn_thres[0] = 1;
    vdf->wq_warn_thres[1] = HW_VDF_WQ_WARN_MULT * HW_VDF_WQ_WARN_MULT;

    vdf->max_aux_threads = HW_VDF_DEFAULT_MAX_AUX_THREADS;
    if (opts && opts->max_aux_threads) {
        vdf->max_aux_threads = opts->max_aux_threads;
    }
    vdf->max_proof_threads = vdf->max_aux_threads - (vdf->max_aux_threads + 7) / 8;
    if (opts && opts->max_proof_threads) {
        vdf->max_proof_threads = opts->max_proof_threads;
    }

    mpz_set_str(vdf->D.impl, d_str, 0);
    mpz_set(vdf->L.impl, vdf->D.impl);
    mpz_neg(vdf->L.impl, vdf->L.impl);
    mpz_root(vdf->L.impl, vdf->L.impl, 4);

    num_values = vdf->target_iters / vdf->interval + 1;
    size_t num_blocks = (num_values + g_values_mult - 1) / g_values_mult;
    vdf->value_block_count = num_blocks;
    vdf->value_blocks = std::make_unique<std::atomic<form *>[]>(num_blocks);
    for (size_t i = 0; i < num_blocks; ++i)
        vdf->value_blocks[i].store(nullptr, std::memory_order_relaxed);
    // One validity bit per allocated slot, including the block slack past num_values.
    size_t bitmap_bytes = num_blocks * (g_values_mult / 8);
    vdf->valid_values = std::make_unique<std::atomic<uint8_t>[]>(bitmap_bytes);
    vdf->abandoned_values = std::make_unique<std::atomic<uint8_t>[]>(bitmap_bytes);
    for (size_t i = 0; i < bitmap_bytes; ++i) {
        vdf->valid_values[i].store(0, std::memory_order_relaxed);
        vdf->abandoned_values[i].store(0, std::memory_order_relaxed);
    }

    //mpz_init_set_ui(initial.a, 2);
    //mpz_init_set_ui(initial.b, 1);
    //initial.iters = 0;
    init_vdf_value(&vdf->last_val);
    // TODO: verify validity of initial form
    bqfc_deserialize(vdf->last_val.a, vdf->last_val.b, vdf->D.impl, init_form,
            BQFC_FORM_SIZE, BQFC_MAX_D_BITS, false);
    hw_proof_get_form(hw_proof_value_at(vdf, 0), vdf, &vdf->last_val);
    vdf->valid_values[0].store(1, std::memory_order_relaxed);
    //vdf->raw_values.push_back(initial);
    vdf->init_done = true;
    for (uint8_t slot = 0; slot < vdf->max_aux_threads; slot++)
        vdf->aux_threads[slot] = std::thread(hw_aux_worker, vdf, slot);
}

void clear_vdf_state(struct vdf_state *vdf)
{
    hw_proof_reset_retained_values(vdf);
    vdf->proofs.clear();
    vdf->req_proofs.clear();
    vdf->queued_proofs.clear();
    vdf->done_proofs.clear();

    for (size_t i = 0; i < vdf->value_block_count; i++)
        delete[] vdf->value_blocks[i].load(std::memory_order_relaxed);
    vdf->value_blocks.reset();
    vdf->value_block_count = 0;
    vdf->valid_values.reset();
    vdf->abandoned_values.reset();
    mpz_clears(vdf->last_val.a, vdf->last_val.b, NULL);
    vdf->init_done = false;
}
