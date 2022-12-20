#include "hw_proof.hpp"
//#include "vdf.h"

#include <cstdlib>

void hw_proof_calc_values(struct vdf_state *vdf, struct vdf_value *val, uint64_t next_iters, uint32_t n_steps, int thr_idx)
{
    integer a(val->a), b(val->b), d(vdf->d), l(vdf->l);
    //const integer a = {val->a}, b = {val->b}, d = {vdf->d}, l = {vdf->l};
    form f = form::from_abd(a, b, d);
    uint64_t end_iters = next_iters + vdf->interval * n_steps;
    uint64_t iters = val->iters;
    PulmarkReducer reducer;

    do {
        nudupl_form(f, f, d, l);
        reducer.reduce(f);
        iters++;

        if (iters == next_iters) {
            //mpz_t a2, b2;
            // TODO: vdf_value without 'iters'
            struct vdf_value val2;

            val2.iters = iters;
            mpz_init_set(val2.a, f.a.impl);
            mpz_init_set(val2.b, f.b.impl);
            vdf->values[iters / vdf->interval] = val2;

            vdf->done_values++;
            next_iters += vdf->interval;
        }

    } while (iters < end_iters);

    fprintf(stderr, " VDF %d: aux thread %d done\n", vdf->idx, thr_idx);
    vdf->aux_threads_busy &= ~(1U << thr_idx);
}

void hw_proof_add_work(struct vdf_state *vdf, struct vdf_value *val, uint64_t next_iters, uint32_t n_steps)
{
    struct vdf_work work = {vdf->raw_values.size() - 1, next_iters, n_steps};

    vdf->wq.push_back(work);
}

void hw_proof_process_work(struct vdf_state *vdf)
{
    uint8_t busy = vdf->aux_threads_busy;

    for (int i = 0; i < HW_VDF_MAX_AUX_THREADS; i++) {
        if (!(busy & (1U << i))) {
            if (vdf->wq.empty()) {
                break;
            }

            struct vdf_work work = vdf->wq.back();
            struct vdf_value *val = &vdf->raw_values[work.raw_idx];

            vdf->aux_threads_busy |= 1U << i;
            std::thread(hw_proof_calc_values, vdf, val, work.start_iters, work.n_steps, i).detach();
        }
    }

    if (!vdf->wq.empty()) {
        fprintf(stderr, "VDF %d: Warning: too much work for VDF aux threads\n", vdf->idx);
    }
}

void hw_proof_add_value(struct vdf_state *vdf, struct vdf_value *val)
{
    uint64_t interval = vdf->interval;
    uint64_t iters = vdf->raw_values.back().iters;
    //uint64_t start_iters = iters;
    //uint64_t end_iters = val->iters;
    //uint64_t mask = interval - 1;
    //start_iters = (start_iters + mask) & ~mask;
    //end_iters &= ~mask;
    uint64_t start_iters = (iters + interval - 1) / interval * interval;
    uint64_t end_iters = val->iters / interval * interval;

    // TODO: locking for 'values'
    vdf->values.resize(end_iters / interval + 1);
    if (iters == end_iters) {
        vdf->values[iters / interval] = *val;
        vdf->done_values++;
        end_iters -= interval;
    }
    //iters = start_iters;

    if (start_iters <= end_iters) {
        uint32_t n_steps = (end_iters - start_iters) / interval + 1;
        fprintf(stderr, " VDF %d: computing target iters=%lu delta=%lu\n",
                vdf->idx, end_iters, end_iters - iters);
        if (start_iters > vdf->target_iters) {
            fprintf(stderr, "VDF %d: Fail at iters=%lu end_iters=%lu\n",
                    vdf->idx, iters, end_iters);
            abort();
        }
        //iters += interval;
        hw_proof_add_work(vdf, val, start_iters, n_steps);
    }

    vdf->raw_values.push_back(*val);
    vdf->cur_iters = val->iters;

    if (vdf->cur_iters >= vdf->target_iters) {
        vdf->completed = true;
    }
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
        return NULL;
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

void init_vdf_state(struct vdf_state *vdf, const char *d_str, uint64_t n_iters, uint8_t idx)
{
    struct vdf_value initial;
    vdf->target_iters = n_iters;
    vdf->cur_iters = 0;
    vdf->done_values = 0;
    vdf->interval = HW_VDF_VALUE_INTERVAL;
    vdf->idx = idx;
    vdf->completed = false;

    mpz_init_set_str(vdf->d, d_str, 0);
    mpz_init_set(vdf->l, vdf->d);
    mpz_neg(vdf->l, vdf->l);
    mpz_root(vdf->l, vdf->l, 4);
    mpz_init(vdf->a2);

    mpz_init_set_ui(initial.a, 2);
    mpz_init_set_ui(initial.b, 1);
    initial.iters = 0;
    vdf->raw_values.push_back(initial);
}

void clear_vdf_state(struct vdf_state *vdf)
{
    mpz_clears(vdf->d, vdf->l, vdf->a2, NULL);
    for (size_t i = 0; i < vdf->raw_values.size(); i++) {
        mpz_clears(vdf->raw_values[i].a, vdf->raw_values[i].b, NULL);
    }
}
