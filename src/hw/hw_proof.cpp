#include "hw_proof.hpp"
//#include "vdf.h"

#include <cstdlib>
#include <unistd.h>

void init_vdf_value(struct vdf_value *val)
{
    val->iters = 0;
    mpz_inits(val->a, val->b, NULL);
}

void clear_vdf_value(struct vdf_value *val)
{
    mpz_clears(val->a, val->b, NULL);
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

void hw_proof_calc_values(struct vdf_state *vdf, struct vdf_value *val, uint64_t next_iters, uint32_t n_steps, int thr_idx)
{
    integer a(val->a), b(val->b), d(vdf->d), l(vdf->l);
    //const integer a = {val->a}, b = {val->b}, d = {vdf->d}, l = {vdf->l};
    form f = form::from_abd(a, b, d);
    uint64_t end_iters = next_iters + vdf->interval * n_steps;
    uint64_t iters = val->iters;
    PulmarkReducer reducer;

    fprintf(stderr, " VDF %d: computing %lu iters (%lu -> %lu, %u steps)\n",
            vdf->idx, end_iters - iters, iters, end_iters, n_steps);

    do {
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
                fprintf(stderr, " VDF %d: bad form at iters=%lu\n", vdf->idx, iters);
                abort();
            }

            vdf->done_values++;
            next_iters += vdf->interval;
        }

    } while (iters < end_iters);

    fprintf(stderr, " VDF %d: aux thread %d done\n", vdf->idx, thr_idx);
    vdf->aux_threads_busy &= ~(1U << thr_idx);
}

void hw_proof_add_work(struct vdf_state *vdf, struct vdf_value *val, uint64_t next_iters, uint32_t n_steps)
{
    auto *work = new struct vdf_work;
    work->start_val = vdf->last_val;
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
            struct vdf_value *val;

            if (vdf->wq.empty()) {
                break;
            }

            work = vdf->wq.front();
            vdf->wq.pop_front();
            //struct vdf_value *val = &vdf->raw_values[work.raw_idx];
            val = &work->start_val;

            vdf->aux_threads_busy |= 1U << i;
            std::thread(hw_proof_calc_values, vdf, val, work->start_iters, work->n_steps, i).detach();
        }
    }

    if (!vdf->wq.empty()) {
        fprintf(stderr, "VDF %d: Warning: too much work for VDF aux threads\n", vdf->idx);
    }
}

void hw_proof_wait_values(struct vdf_state *vdf)
{
    while (!vdf->wq.empty()) {
        usleep(100000);
        hw_proof_process_work(vdf);
    }

    while (vdf->aux_threads_busy) {
        usleep(100000);
    }
}

void hw_proof_add_value(struct vdf_state *vdf, struct vdf_value *val)
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
            fprintf(stderr, "VDF %d: Fail at iters=%lu end_iters=%lu\n",
                    vdf->idx, last_iters, end_iters);
            abort();
        }
        //iters += interval;
        hw_proof_add_work(vdf, val, start_iters, n_steps);
    } else {
        clear_vdf_value(&vdf->last_val);
    }

    //vdf->raw_values.push_back(*val);
    vdf->last_val = *val;
    vdf->cur_iters = val->iters;

    if (vdf->cur_iters >= vdf->target_iters) {
        vdf->completed = true;
    }

    hw_proof_process_work(vdf);
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

    hw_proof_wait_values(vdf);
    fprintf(stderr, "VDF %d: got all values; proof_iters=%lu pos=%lu\n",
            vdf->idx, vdf->proof_iters, pos);

    y = vdf->values[pos];
    if (!y.check_valid(d)) {
        fprintf(stderr, "VDF %d: invalid form at pos=%lu\n", vdf->idx, pos);
        abort();
    }
    while (iters < vdf->proof_iters) {
        nudupl_form(y, y, d, l);
        reducer.reduce(y);
        iters++;
    }
    if (!y.check_valid(d)) {
        fprintf(stderr, "VDF %d: invalid y\n", vdf->idx);
        abort();
    }

    for (size_t i = 0; i <= vdf->target_iters / vdf->interval; i++) {
        if (!vdf->values[i].check_valid(d)) {
            fprintf(stderr, "VDF %d: invalid form at pos=%zu\n", vdf->idx, i);
            abort();
        }
    }
    //Segment seg(0, vdf->proof_iters, vdf->values[0], y);
    //HwProver prover(seg, d, vdf);

    //prover.start();
    proof = GenerateWesolowski(y, vdf->values[0], d, reducer, vdf->values, vdf->proof_iters, k, proof_l);
    fprintf(stderr, "VDF %d: Proof done\n", vdf->idx);

    bool is_valid = false;
    VerifyWesolowskiProof(d, vdf->values[0], y, proof, vdf->proof_iters, is_valid);
    fprintf(stderr, "VDF %d: Proof %s\n", vdf->idx, is_valid ? "valid" : "NOT VALID");
    if (!is_valid) {
        abort();
    }
    //if (prover.IsFinished()) {
        //fprintf(stderr, "VDF %d: Proof done!\n", vdf->idx);
    //} else {
        //fprintf(stderr, "VDF %d: Could not finish proof\n", vdf->idx);
    //}
}

void init_vdf_state(struct vdf_state *vdf, const char *d_str, uint64_t n_iters, uint8_t idx)
{
    //struct vdf_value initial;
    vdf->proof_iters = n_iters;
    vdf->cur_iters = 0;
    vdf->done_values = 1;
    vdf->interval = HW_VDF_VALUE_INTERVAL;
    vdf->target_iters = (n_iters + vdf->interval - 1) / vdf->interval * vdf->interval;
    vdf->idx = idx;
    vdf->completed = false;
    vdf->aux_threads_busy = 0;

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
    mpz_set_ui(vdf->last_val.a, 2);
    mpz_set_ui(vdf->last_val.b, 1);
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
