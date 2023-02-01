#include "hw_proof.hpp"
#include "hw_util.hpp"
#include "bqfc.h"
//#include "vdf.h"

#include <algorithm>
#include <cstdlib>
#include <unistd.h>

int verify_vdf_value(struct vdf_state *vdf, struct vdf_value *val)
{
    mpz_mul(vdf->a2, val->b, val->b);
    mpz_sub(vdf->a2, vdf->a2, vdf->d);
    /* Verify that c could be computed as c = (b^2 - d) / (4 * a) */
    if (!mpz_divisible_p(vdf->a2, val->a) || mpz_scan1(vdf->a2, 0) < mpz_scan1(val->a, 0) + 2) {
        vdf->n_bad++;
        LOG_INFO("VDF %d: Warning: Bad VDF value at iters=%lu n_bad=%u",
                vdf->idx, val->iters, vdf->n_bad);
        gmp_fprintf(stderr, " a = %#Zx\n b = %#Zx\n d = %#Zx\n",
                val->a, val->b, vdf->d);
        return -1;
    }
    return 0;
}

void hw_proof_add_value(struct vdf_state *vdf, struct vdf_value *val)
{
    if (!val->iters || vdf->last_val.iters == val->iters) {
        LOG_INFO("VDF %d: Skipping iters=%lu", vdf->idx, val->iters);
        return;
    }

    // b = b (mod 2*a)
    mpz_mul_2exp(vdf->a2, val->a, 1);
    mpz_mod(val->b, val->b, vdf->a2);

    if (verify_vdf_value(vdf, val)) {
        return;
    }
    hw_proof_handle_value(vdf, val);
}

void hw_proof_get_form(form *f, struct vdf_state *vdf, struct vdf_value *val)
{
    integer a, b, d;

    mpz_swap(a.impl, val->a);
    mpz_swap(b.impl, val->b);
    mpz_swap(d.impl, vdf->d);

    *f = form::from_abd(a, b, d);

    mpz_swap(a.impl, val->a);
    mpz_swap(b.impl, val->b);
    mpz_swap(d.impl, vdf->d);
}

timepoint_t hw_proof_get_cur_time(void)
{
    return std::chrono::high_resolution_clock::now();
}

uint64_t hw_proof_get_elapsed_us(timepoint_t &t1)
{
    auto t2 = hw_proof_get_cur_time();
    return std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
}

void hw_proof_calc_values(struct vdf_state *vdf, struct vdf_work *work, int thr_idx)
{
    struct vdf_value *val = &work->start_val;
    uint64_t next_iters = work->start_iters;
    uint32_t n_steps = work->n_steps;

    integer a(val->a), b(val->b), d(vdf->d), l(vdf->l);
    //const integer a = {val->a}, b = {val->b}, d = {vdf->d}, l = {vdf->l};
    form f = form::from_abd(a, b, d);
    uint64_t end_iters = next_iters + vdf->interval * n_steps;
    uint64_t iters = val->iters;
    PulmarkReducer reducer;
    timepoint_t t1;
    uint64_t init_iters = iters;

    LOG_INFO(" VDF %d: computing %lu iters (%lu -> %lu, %u steps)",
            vdf->idx, end_iters - iters, iters, end_iters, n_steps);

    clear_vdf_value(val);
    delete work;
    t1 = hw_proof_get_cur_time();
    do {
        if (vdf->stopping) {
            break;
        }
        nudupl_form(f, f, d, l);
        reducer.reduce(f);
        iters++;

        if (iters == next_iters) {
            //mpz_t a2, b2;
            // TODO: vdf_value without 'iters'
            //struct vdf_value val2;

            //val2.iters = iters;
            //mpz_init_set(val2.a, f.a.impl);
            //mpz_init_set(val2.b, f.b.impl);
            vdf->values[iters / vdf->interval] = f;
            if (!f.check_valid(d)) {
                LOG_ERROR(" VDF %d: bad form at iters=%lu", vdf->idx, iters);
                abort();
            }

            vdf->done_values++;
            next_iters += vdf->interval;
        }

    } while (iters < end_iters);

    vdf->done_iters += iters - init_iters;
    vdf->elapsed_us += hw_proof_get_elapsed_us(t1);
    LOG_INFO(" VDF %d: aux thread %d done", vdf->idx, thr_idx);
    vdf->aux_threads_busy &= ~(1U << thr_idx);
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
    uint8_t busy = vdf->aux_threads_busy;

    for (int i = 0; i < HW_VDF_MAX_AUX_THREADS; i++) {
        if (!(busy & (1U << i))) {
            struct vdf_work *work;

            if (vdf->wq.empty()) {
                break;
            }

            work = vdf->wq.front();
            vdf->wq.pop_front();
            //struct vdf_value *val = &vdf->raw_values[work.raw_idx];

            vdf->aux_threads_busy |= 1U << i;
            std::thread(hw_proof_calc_values, vdf, work, i).detach();
        }
    }

    if (!vdf->wq.empty()) {
        LOG_INFO("VDF %d: Warning: too much work for VDF aux threads", vdf->idx);
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

    // TODO: locking for 'values'
    //vdf->values.resize(end_iters / interval + 1);
    if (val->iters == end_iters) {
        //vdf->values[iters / interval] = *val;
        hw_proof_get_form(&vdf->values[val->iters / interval], vdf, val);
        vdf->done_values++;
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

    if (vdf->cur_iters >= vdf->target_iters) {
        uint64_t elapsed_us = hw_proof_get_elapsed_us(vdf->start_time);
        uint64_t sw_elapsed_us = vdf->elapsed_us;
        uint64_t sw_iters = vdf->done_iters;
        uint64_t ips, sw_ips;

        elapsed_us = elapsed_us ? elapsed_us : 1;
        sw_elapsed_us = sw_elapsed_us ? sw_elapsed_us : 1;
        ips = vdf->cur_iters * 1000000 / elapsed_us;
        sw_ips = sw_iters * 1000000 / sw_elapsed_us;

        LOG_INFO("\nVDF %d: %lu HW iters done in %lus, HW speed: %lu ips",
                vdf->idx, vdf->cur_iters, elapsed_us / 1000000, ips);
        LOG_INFO("VDF %d: %lu SW iters done in %lus, SW speed: %lu ips\n",
                vdf->idx, sw_iters, sw_elapsed_us / 1000000, sw_ips);
        vdf->completed = true;
    }

    hw_proof_process_work(vdf);
}

void hw_proof_stop(struct vdf_state *vdf)
{
    vdf->stopping = true;
    hw_proof_wait_values(vdf, false);
}

#if 0
class HwProver : public Prover {
  public:
    HwProver(Segment segm, integer D, struct vdf_state *vdf)
        : Prover(segm, D)
    {
        k = 8;
        l = 512;
        this->vdf = vdf;
    }

    form* GetForm(uint64_t iteration) {
        size_t pos = iteration / vdf->interval;

        return &vdf->values[pos];
    }

    void start() {
        GenerateProof();
    }

    void stop() {
    }

    bool PerformExtraStep() {
        return true;
    }

    void OnFinish() {
        is_finished = true;
    }

  private:
    struct vdf_state *vdf;
};
#endif

void hw_get_proof(struct vdf_state *vdf)
{
    form y, proof;
    size_t pos = vdf->proof_iters / vdf->interval;
    uint64_t iters = pos * vdf->interval;
    integer d(vdf->d), l(vdf->l);
    PulmarkReducer reducer;
    uint64_t k = 8, proof_l = vdf->interval / k;

    hw_proof_wait_values(vdf, true);
    LOG_INFO("VDF %d: got all values; proof_iters=%lu pos=%lu",
            vdf->idx, vdf->proof_iters, pos);

    y = vdf->values[pos];
    if (!y.check_valid(d)) {
        LOG_ERROR("VDF %d: invalid form at pos=%lu", vdf->idx, pos);
        abort();
    }
    while (iters < vdf->proof_iters) {
        nudupl_form(y, y, d, l);
        reducer.reduce(y);
        iters++;
    }
    if (!y.check_valid(d)) {
        LOG_ERROR("VDF %d: invalid y", vdf->idx);
        abort();
    }

    for (size_t i = 0; i <= vdf->target_iters / vdf->interval; i++) {
        if (!vdf->values[i].check_valid(d)) {
            LOG_ERROR("VDF %d: invalid form at pos=%zu", vdf->idx, i);
            abort();
        }
    }
    //Segment seg(0, vdf->proof_iters, vdf->values[0], y);
    //HwProver prover(seg, d, vdf);

    //prover.start();
    proof = GenerateWesolowski(y, vdf->values[0], d, reducer, vdf->values, vdf->proof_iters, k, proof_l);
    LOG_INFO("VDF %d: Proof done", vdf->idx);

    bool is_valid = false;
    VerifyWesolowskiProof(d, vdf->values[0], y, proof, vdf->proof_iters, is_valid);
    LOG_INFO("VDF %d: Proof %s", vdf->idx, is_valid ? "valid" : "NOT VALID");
    if (!is_valid) {
        abort();
    }
    //if (prover.IsFinished()) {
        //fprintf(stderr, "VDF %d: Proof done!\n", vdf->idx);
    //} else {
        //fprintf(stderr, "VDF %d: Could not finish proof\n", vdf->idx);
    //}
}

void init_vdf_state(struct vdf_state *vdf, const char *d_str, const uint8_t *init_form, uint64_t n_iters, uint8_t idx)
{
    //int ret;
    //struct vdf_value initial;
    vdf->proof_iters = n_iters;
    vdf->cur_iters = 0;
    vdf->done_values = 1;
    vdf->done_iters = 0;
    vdf->elapsed_us = 0;
    vdf->start_time = hw_proof_get_cur_time();
    vdf->interval = HW_VDF_VALUE_INTERVAL;
    vdf->target_iters = (n_iters + vdf->interval - 1) / vdf->interval * vdf->interval;
    vdf->idx = idx;
    vdf->completed = false;
    vdf->stopping = false;
    vdf->aux_threads_busy = 0;
    vdf->n_bad = 0;

    mpz_init_set_str(vdf->d, d_str, 0);
    mpz_init_set(vdf->l, vdf->d);
    mpz_neg(vdf->l, vdf->l);
    mpz_root(vdf->l, vdf->l, 4);
    mpz_init(vdf->a2);

    vdf->values.resize(vdf->target_iters / vdf->interval + 1);

    //mpz_init_set_ui(initial.a, 2);
    //mpz_init_set_ui(initial.b, 1);
    //initial.iters = 0;
    init_vdf_value(&vdf->last_val);
    // TODO: verify validity of initial form
    bqfc_deserialize(vdf->last_val.a, vdf->last_val.b, vdf->d, init_form,
            BQFC_FORM_SIZE, BQFC_MAX_D_BITS);
    hw_proof_get_form(&vdf->values[0], vdf, &vdf->last_val);
    //vdf->raw_values.push_back(initial);
}

void clear_vdf_state(struct vdf_state *vdf)
{
    mpz_clears(vdf->d, vdf->l, vdf->a2, NULL);
    //for (size_t i = 0; i < vdf->raw_values.size(); i++) {
        //mpz_clears(vdf->raw_values[i].a, vdf->raw_values[i].b, NULL);
    //}
    mpz_clears(vdf->last_val.a, vdf->last_val.b, NULL);
}
