#include "hw_proof.hpp"
#include "bqfc.h"
//#include "vdf.h"

#include <algorithm>
#include <cstdlib>
#include <unistd.h>

static const uint32_t g_chkp_thres = 1000000;
static const uint32_t g_skip_thres = 10;

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
    if (detail) {
        uint64_t done_values = vdf->done_values;
        LOG_INFO("VDF %d: Avg iters per intermediate: %lu",
                vdf->idx, sw_iters / done_values);
        if (vdf->n_bad > 0) {
            LOG_INFO("VDF %d: Bad VDF values observed: %u", vdf->idx, vdf->n_bad);
        }
    }
    LOG_INFO("");
}

static const size_t g_values_mult = 1UL << 12;

form *hw_proof_value_at(struct vdf_state *vdf, size_t pos)
{
    size_t idx = pos / g_values_mult;
    size_t old_size = vdf->values.size();

    if (idx + 1 >= old_size) {
        size_t new_size = idx + 2;
        vdf->values.resize(new_size);
        for (size_t i = old_size; i < new_size; i++) {
            vdf->values[i] = new form[g_values_mult];
        }
        LOG_INFO("VDF %d: Allocating intermediate values, total %zu * %zu",
                vdf->idx, new_size, g_values_mult);
    }
    return &vdf->values[idx][pos % g_values_mult];
}

form *hw_proof_last_good_form(struct vdf_state *vdf, size_t *out_pos)
{
    size_t pos = vdf->cur_iters / vdf->interval;

    while (!(vdf->valid_values[pos / 8] & (1 << (pos % 8)))) {
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
    vdf->valid_values[pos / 8] |= 1 << (pos % 8);
    vdf->valid_values_mtx.unlock();
    vdf->done_values++;
}

void hw_proof_calc_values(struct vdf_state *vdf, struct vdf_work *work, int thr_idx)
{
    struct vdf_value *val = &work->start_val;
    uint64_t next_iters = work->start_iters;
    uint32_t n_steps = work->n_steps;

    integer a(val->a), b(val->b);
    //const integer a = {val->a}, b = {val->b}, d = {vdf->d}, l = {vdf->l};
    form f = form::from_abd(a, b, vdf->D);
    uint64_t end_iters = next_iters + vdf->interval * n_steps;
    uint64_t iters = val->iters;
    PulmarkReducer reducer;
    timepoint_t t1;
    uint64_t init_iters = iters;

    LOG_DEBUG(" VDF %d: computing %lu iters (%lu -> %lu, %u steps) in aux thread %d",
            vdf->idx, end_iters - iters, iters, end_iters, n_steps, thr_idx);

    clear_vdf_value(val);
    delete work;
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
                abort();
            }

            *hw_proof_value_at(vdf, pos) = f;
            hw_proof_add_intermediate(vdf, NULL, pos);

            next_iters += vdf->interval;
        }

    } while (iters < end_iters);

    vdf->done_iters += iters - init_iters;
    vdf->elapsed_us += vdf_get_elapsed_us(t1);
    LOG_DEBUG(" VDF %d: aux thread %d done", vdf->idx, thr_idx);
    vdf->aux_threads_busy &= ~(1UL << thr_idx);
}

class ProofCmp {
public:
    ProofCmp(std::vector<struct vdf_proof> &p)
    {
        proofs = p.data();
    }

    bool operator() (uint16_t a, uint16_t b)
    {
        return proofs[a].iters < proofs[b].iters;
    }

private:
    struct vdf_proof *proofs;
};

uint16_t hw_queue_proof(struct vdf_state *vdf, uint64_t seg_iters, uint16_t prev, uint8_t flags)
{
    uint16_t pos;
    struct vdf_proof proof;

    proof.iters = seg_iters;
    if (prev != HW_VDF_PROOF_NONE) {
        proof.iters += vdf->proofs[prev].iters;
    }
    proof.seg_iters = seg_iters;
    proof.flags = flags;
    proof.prev = prev;
    proof.ref_cnt = 1;
    pos = vdf->proofs.size();

    vdf->proofs_resize_mtx.lock();
    vdf->proofs.push_back(proof);
    vdf->proofs_resize_mtx.unlock();

    vdf->queued_proofs.push_back(pos);

    return pos;
}

uint8_t hw_proof_cnt_segments(struct vdf_state *vdf, uint16_t idx)
{
    uint8_t cnt = 0;
    do {
        cnt++;
        idx = vdf->proofs[idx].prev;
    } while (idx != HW_VDF_PROOF_NONE);
    return cnt;
}

void hw_proof_inc_ref(struct vdf_state *vdf, uint16_t idx)
{
    do {
        vdf->proofs[idx].ref_cnt++;
        idx = vdf->proofs[idx].prev;
    } while (idx != HW_VDF_PROOF_NONE);
}

void hw_proof_dec_ref(struct vdf_state *vdf, std::vector<uint16_t> &idxs)
{
    std::vector<uint16_t> proofs_to_del;
    int len = idxs.size();
    int last_proof_idx = vdf->proofs.size() - 1;

    for (int i = 0; i < len; i++) {
        uint16_t idx = idxs[i];
        struct vdf_proof *proof;
        do {
            proof = &vdf->proofs[idx];
            if (proof->flags & HW_VDF_PROOF_FLAG_STARTED) {
                break;
            }
            proof->ref_cnt--;
            if (!proof->ref_cnt) {
                proofs_to_del.push_back(idx);
            }
            idx = proof->prev;
        } while (idx != HW_VDF_PROOF_NONE);
    }

    std::sort(proofs_to_del.begin(), proofs_to_del.end());

    for (int i = proofs_to_del.size() - 1; i >= 0; i--) {
        uint16_t idx = proofs_to_del[i];
        if (idx == last_proof_idx) {
            last_proof_idx--;
        }
        memset(&vdf->proofs[idx], 0xff, sizeof(vdf->proofs[idx]));

        for (int j = 0; j < (int)vdf->queued_proofs.size(); j++) {
            if (vdf->queued_proofs[j] == idx) {
                vdf->queued_proofs.erase(vdf->queued_proofs.begin() + j);
            }
        }
    }
    LOG_INFO("VDF %d: Removed %d queued proofs; total proofs reduced by %zu",
            vdf->idx, proofs_to_del.size(), vdf->proofs.size() - last_proof_idx - 1);
    vdf->proofs.resize(last_proof_idx + 1);
    LOG_DEBUG("VDF %d: Proofs %zu, queued %zu", vdf->idx, vdf->proofs.size(), vdf->queued_proofs.size());
}

bool hw_proof_should_queue(struct vdf_state *vdf, uint64_t iters)
{
    uint16_t last_queued_idx = vdf->queued_proofs.back();
    return iters < vdf->proofs[last_queued_idx].iters;
}

void hw_proof_process_req(struct vdf_state *vdf)
{
    uint64_t iters;
    uint64_t req_iters;
    uint64_t base_iters = 0;
    uint64_t chkp_iters;
    uint32_t chkp_div = 4, chkp_mul = 3;
    uint8_t max_chkp_segments = 64 - 3;
    int i;
    uint16_t prev = HW_VDF_PROOF_NONE;

    if (vdf->req_proofs.empty()) {
        return;
    }

    req_iters = vdf->req_proofs[0].iters;
    vdf->req_proofs.erase(vdf->req_proofs.begin());
    if (!vdf->queued_proofs.empty() && hw_proof_should_queue(vdf, req_iters)) {
        std::vector<uint16_t> proofs_to_del;
        for (i = 0; i < (int)vdf->queued_proofs.size(); i++) {
            uint16_t idx = vdf->queued_proofs[i];
            bool is_chkp = !(vdf->proofs[idx].flags & HW_VDF_PROOF_FLAG_IS_REQ);
            if (is_chkp || req_iters > vdf->proofs[idx].iters) {
                continue;
            }
            proofs_to_del.push_back(idx);
            hw_request_proof(vdf, vdf->proofs[idx].iters, false);
        }
        hw_proof_dec_ref(vdf, proofs_to_del);
    }

    for (i = vdf->queued_proofs.size() - 1; i >= 0; i--) {
        uint16_t idx = vdf->queued_proofs[i];
        uint8_t n_segments;
        if (vdf->proofs[idx].flags & HW_VDF_PROOF_FLAG_IS_REQ) {
            continue;
        }
        iters = vdf->proofs[idx].iters;
        n_segments = hw_proof_cnt_segments(vdf, idx);
        if (iters <= req_iters && n_segments <= max_chkp_segments) {
            base_iters = iters;
            prev = idx;
            hw_proof_inc_ref(vdf, prev);
            break;
        } else if (iters <= req_iters) {
            LOG_INFO("VDF %d: Max seg triggered at req_iters=%lu", vdf->idx, req_iters);
        }
    }

    iters = req_iters - base_iters;

    if (iters > g_chkp_thres) {
        // Split iters as [75%, 25%]
        chkp_iters = iters * chkp_mul / chkp_div;
        if (iters - chkp_iters > g_chkp_thres) {
            // Split iters as [69%, 23%, 8%]
            uint32_t chkp2_mul[] = { 69, 69 + 23 };
            uint64_t chkp2_iters;

            chkp_iters = iters * chkp2_mul[0] / 100;
            chkp_iters = chkp_iters / vdf->interval * vdf->interval;
            prev = hw_queue_proof(vdf, chkp_iters, prev, 0);

            chkp2_iters = iters * chkp2_mul[1] / 100 - chkp_iters;
            iters -= chkp_iters;
            chkp_iters = chkp2_iters;
        }
        chkp_iters = chkp_iters / vdf->interval * vdf->interval;
        prev = hw_queue_proof(vdf, chkp_iters, prev, 0);
        iters -= chkp_iters;
    }
    hw_queue_proof(vdf, iters, prev, HW_VDF_PROOF_FLAG_IS_REQ);

    {
        ProofCmp cmp(vdf->proofs);
        std::sort(vdf->queued_proofs.begin(), vdf->queued_proofs.end(), cmp);
    }
}

bool hw_proof_req_cmp(struct vdf_proof_req &a, struct vdf_proof_req &b)
{
    return a.iters < b.iters;
}

void hw_request_proof(struct vdf_state *vdf, uint64_t iters, bool is_chkp)
{
    vdf->req_proofs.push_back({iters, is_chkp});
    std::sort(vdf->req_proofs.begin(), vdf->req_proofs.end(), hw_proof_req_cmp);
}

void hw_proof_add_work(struct vdf_state *vdf, uint64_t next_iters, uint32_t n_steps)
{
    auto *work = new struct vdf_work;

    copy_vdf_value(&work->start_val, &vdf->last_val);
    work->start_iters = next_iters;
    work->n_steps = n_steps;

    vdf->wq.push_back(work);
}

void hw_proof_process_work(struct vdf_state *vdf)
{
    uint64_t busy = vdf->aux_threads_busy;
    uint32_t qlen;

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
            std::thread(hw_compute_proof, vdf, idx, proof, i).detach();
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
            std::thread(hw_proof_calc_values, vdf, work, i).detach();
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
        while (!vdf->wq.empty()) {
            usleep(100000);
            hw_proof_process_work(vdf);
        }
    }

    while (vdf->aux_threads_busy) {
        usleep(10000);
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
    while (!(vdf->valid_values[pos / 8] & (1 << (pos % 8)))) {
        usleep(100000);
        if (vdf->stopping) {
            return -1;
        }
    }
    return 0;
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
            abort();
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

void hw_stop_proof(struct vdf_state *vdf)
{
    vdf->stopping = true;
    hw_proof_print_stats(vdf, vdf_get_elapsed_us(vdf->start_time), true);
    hw_proof_wait_values(vdf, false);
}

class HwProver : public ParallelProver {
  public:
    HwProver(Segment segm, integer D, struct vdf_state *vdf)
        : ParallelProver(segm, D)
    {
        this->vdf = vdf;
        k = FindK(segm.length);
        l = vdf->interval / k;
        pos_offset = segm.start / vdf->interval;
    }

    form* GetForm(uint64_t pos) {
        pos += pos_offset;
        if (hw_proof_wait_value(vdf, pos)) {
            // Provide arbitrary value when stopping - proof won't be computed
            return &vdf->values[0][0];
        }
        return hw_proof_value_at(vdf, pos);
    }

    void start() {
        GenerateProof();
    }

    void stop() {
    }

    bool PerformExtraStep() {
        return !vdf->stopping;
    }

    void OnFinish() {
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
    uint32_t pos_offset;
};

void hw_compute_proof(struct vdf_state *vdf, size_t proof_idx, struct vdf_proof *out_proof, uint8_t thr_idx)
{
    form x, y, proof_val;
    uint64_t proof_iters, start_iters, iters;
    size_t pos, start_pos;
    PulmarkReducer reducer;
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
    if (!y.check_valid(vdf->D)) {
        LOG_ERROR("VDF %d: invalid form at pos=%lu", vdf->idx, pos);
        abort();
    }
    while (iters < proof_iters) {
        nudupl_form(y, y, vdf->D, vdf->L);
        reducer.reduce(y);
        iters++;
    }
    if (!y.check_valid(vdf->D)) {
        LOG_ERROR("VDF %d: invalid y", vdf->idx);
        abort();
    }


    {
        Segment seg(start_iters, proof_iters - start_iters, x, y);
        HwProver prover(seg, vdf->D, vdf);

        if (!is_chkp && seg.length > g_chkp_thres) {
            LOG_INFO("VDF %d: Warning: too long final proof segment length=%lu",
                    vdf->idx, seg.length);
        }

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
                abort();
            }

            d_bits = mpz_sizeinbase(vdf->D.impl, 2);

            vdf->proofs_resize_mtx.lock();
            if (proof_idx != SIZE_MAX) {
                out_proof = &vdf->proofs[proof_idx];
            }

            bqfc_serialize(out_proof->y, y.a.impl, y.b.impl, d_bits);
            bqfc_serialize(out_proof->proof, proof_val.a.impl, proof_val.b.impl, d_bits);

            if (out_proof->flags & HW_VDF_PROOF_FLAG_IS_REQ) {
                vdf->done_proofs.push_back((uint16_t)proof_idx);
            } else {
                integer B = GetB(vdf->D, x, y);

                mpz_export(out_proof->B, NULL, 1, 1, 0, 0, B.impl);
            }
            out_proof->flags |= HW_VDF_PROOF_FLAG_DONE;
            vdf->proofs_resize_mtx.unlock();
        } else {
            LOG_INFO("VDF %d: Proof stopped", vdf->idx);
        }
    }

out:
    if (thr_idx < vdf->max_aux_threads) {
        vdf->aux_threads_busy &= ~(1UL << thr_idx);
        vdf->n_proof_threads -= PARALLEL_PROVER_N_THREADS;
    }
}

int hw_retrieve_proof(struct vdf_state *vdf, struct vdf_proof **out_proof)
{
#if 0
    if (!vdf->proofs.empty()) {
        for (size_t i = 0; i < vdf->proofs.size(); i++) {
            uint64_t iters = vdf->proofs[i]->iters;
            size_t last_chkp = iters / vdf->chkp_interval;
            if (iters && (!last_chkp || vdf->chkp_proofs[last_chkp - 1]->iters)) {
                *proof = vdf->proofs[i];
                vdf->proofs.erase(vdf->proofs.begin() + i);
                return last_chkp;
            }
        }
    }
#endif
    if (!vdf->done_proofs.empty()) {
        for (size_t i = 0; i < vdf->done_proofs.size(); i++) {
            uint16_t idx = vdf->done_proofs[i];
            struct vdf_proof *proof = &vdf->proofs[idx];
            uint16_t j = proof->prev;
            int cnt = 0;
            bool done = true;

            while (j != HW_VDF_PROOF_NONE) {
                if (!(vdf->proofs[j].flags & HW_VDF_PROOF_FLAG_DONE)) {
                    done = false;
                    break;
                }
                j = vdf->proofs[j].prev;
                cnt++;
            }

            if (done) {
                *out_proof = proof;
                vdf->done_proofs.erase(vdf->done_proofs.begin() + i);
                return cnt;
            }
        }
    }
    return -1;
}

void init_vdf_state(struct vdf_state *vdf, struct vdf_proof_opts *opts, const char *d_str, const uint8_t *init_form, uint64_t n_iters, uint8_t idx)
{
    //int ret;
    //struct vdf_value initial;
    size_t num_values;
    vdf->cur_iters = 0;
    vdf->iters_offset = 0;
    vdf->done_values = 1;
    vdf->done_iters = 0;
    vdf->elapsed_us = 0;
    vdf->start_time = vdf_get_cur_time();
    vdf->interval = HW_VDF_VALUE_INTERVAL;
    vdf->chkp_interval = HW_VDF_CHKP_INTERVAL;
    vdf->target_iters = (n_iters + vdf->interval - 1) / vdf->interval * vdf->interval;
    vdf->idx = idx;
    vdf->completed = false;
    vdf->stopping = false;
    vdf->aux_threads_busy = 0;
    vdf->n_proof_threads = 0;
    vdf->n_bad = 0;
    vdf->n_skipped = 0;
    vdf->log_cnt = 0;
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
    vdf->values.reserve((num_values + g_values_mult - 1) / g_values_mult);
    vdf->valid_values.resize((num_values + 7) / 8, 0);

    //mpz_init_set_ui(initial.a, 2);
    //mpz_init_set_ui(initial.b, 1);
    //initial.iters = 0;
    init_vdf_value(&vdf->last_val);
    // TODO: verify validity of initial form
    bqfc_deserialize(vdf->last_val.a, vdf->last_val.b, vdf->D.impl, init_form,
            BQFC_FORM_SIZE, BQFC_MAX_D_BITS);
    hw_proof_get_form(hw_proof_value_at(vdf, 0), vdf, &vdf->last_val);
    vdf->valid_values[0] = 1 << 0;
    //vdf->raw_values.push_back(initial);
    vdf->init_done = true;
}

void clear_vdf_state(struct vdf_state *vdf)
{
    vdf->proofs.clear();
    vdf->req_proofs.clear();
    vdf->queued_proofs.clear();
    vdf->done_proofs.clear();

    for (size_t i = 0; i < vdf->values.size(); i++) {
        delete[] vdf->values[i];
    }
    vdf->values.clear();
    vdf->valid_values.clear();
    mpz_clears(vdf->last_val.a, vdf->last_val.b, NULL);
    vdf->init_done = false;
}
