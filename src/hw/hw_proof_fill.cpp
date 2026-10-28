#include "hw_proof.hpp"
#include "hw_proof_internal.hpp"
#include "pair_kernels.h"

#include <memory>
#include <optional>
#include <stdexcept>

// Fills square from an ASIC value to the grid points after it and publish
// them. One aux slot runs one fill, or two in lockstep with paired kernels.

namespace {

// One fill. Unless it publishes its whole range, the destructor abandons the
// part it still owes, so proofs that need it fail instead of waiting.
// A bad start form, a bad result or an out-of-range position stops only
// this fill.
class FillLane {
  public:
    FillLane(struct vdf_state *vdf, struct vdf_work *work, int thr_idx)
        : vdf(vdf), thr_idx(thr_idx)
    {
        struct vdf_value *val = &work->start_val;
        integer a(val->a), b(val->b);
        next_iters = work->start_iters;
        end_iters = next_iters + vdf->interval * work->n_steps;
        iters = init_iters = val->iters;
        clear_vdf_value(val);
        LOG_DEBUG(" VDF %d: computing %lu iters (%lu -> %lu, %u steps) in aux thread %d",
                vdf->idx, end_iters - iters, iters, end_iters, work->n_steps, thr_idx);
        try {
            f = form::from_abd(a, b, vdf->D);
            active = true;
        } catch (const std::exception &ex) {
            LOG_ERROR("VDF %d: aux thread %d dropped a fill: %s", vdf->idx, thr_idx, ex.what());
        }
    }

    ~FillLane()
    {
        if (!published)
            hw_proof_abandon_gap(vdf, next_iters, end_iters);
    }

    FillLane(const FillLane &) = delete;
    FillLane &operator=(const FillLane &) = delete;

    // Account for one squaring of f and publish the grid point it reached.
    void advance()
    {
        iters++;
        if (iters == next_iters) {
            size_t pos = iters / vdf->interval;

            if (!f.check_valid(vdf->D)) {
                LOG_ERROR(" VDF %d: bad form at iters=%lu", vdf->idx, iters);
                active = false;
                return;
            }
            try {
                *hw_proof_value_at(vdf, pos) = f;
            } catch (const std::exception &ex) {
                LOG_ERROR("VDF %d: aux thread %d dropped a fill: %s", vdf->idx, thr_idx, ex.what());
                active = false;
                return;
            }
            hw_proof_add_intermediate(vdf, NULL, pos);
            next_iters += vdf->interval;
        }
        if (iters >= end_iters) {
            active = false;
            published = true;
        }
    }

    uint64_t squarings() const { return iters - init_iters; }

    form f;
    bool active = false;

  private:
    struct vdf_state *vdf;
    int thr_idx;
    uint64_t iters, init_iters, next_iters, end_iters;
    bool published = false;
};

// Releases the aux slot when the job ends, after its fills have settled.
class SlotRelease {
  public:
    SlotRelease(struct vdf_state *vdf, uint8_t slot) : vdf(vdf), slot(slot) {}
    ~SlotRelease() { hw_proof_finish_job(vdf, slot, false, SIZE_MAX, true); }
    SlotRelease(const SlotRelease &) = delete;
    SlotRelease &operator=(const SlotRelease &) = delete;

  private:
    struct vdf_state *vdf;
    uint8_t slot;
};

}  // namespace

void hw_proof_calc_values(struct vdf_state *vdf, struct vdf_work *work, int thr_idx)
{
    SlotRelease slot(vdf, (uint8_t)thr_idx);
    std::unique_ptr<struct vdf_work> owned(work);
    // Declared after slot, so both fills settle before the slot is released.
    std::optional<FillLane> lanes[2];
    lanes[0].emplace(vdf, owned.get(), thr_idx);
    if (owned->pair)
        lanes[1].emplace(vdf, owned->pair.get(), thr_idx);
    owned.reset();

    PulmarkReducer reducer;
    timepoint_t t1 = vdf_get_cur_time();
    while (!vdf->stopping) {
        FillLane *x = lanes[0] && lanes[0]->active ? &*lanes[0] : nullptr;
        FillLane *y = lanes[1] && lanes[1]->active ? &*lanes[1] : nullptr;
        if (!x && !y)
            break;
#if defined(CHIAVDF_PAIR_KERNELS)
        if (x && y) {
            nudupl_form_pair(x->f, y->f, vdf->D, vdf->L, true);
            reducer.reduce(x->f);
            reducer.reduce(y->f);
            x->advance();
            y->advance();
            continue;
        }
#endif
        for (FillLane *one : {x, y}) {
            if (!one)
                continue;
            nudupl_form(one->f, one->f, vdf->D, vdf->L);
            reducer.reduce(one->f);
            one->advance();
        }
    }

    for (auto &lane : lanes)
        if (lane)
            vdf->done_iters += lane->squarings();
    vdf->elapsed_us += vdf_get_elapsed_us(t1);
    LOG_DEBUG(" VDF %d: aux thread %d done", vdf->idx, thr_idx);
}

std::unique_ptr<struct vdf_work> hw_proof_take_fill(struct vdf_state *vdf, int free_slots)
{
    std::unique_ptr<struct vdf_work> work(vdf->wq.front());
    vdf->wq.pop_front();
    if (vdf->pair_kernels && vdf->wq.size() > (size_t)(free_slots - 1)) {
        work->pair.reset(vdf->wq.front());
        vdf->wq.pop_front();
    }
    return work;
}

void hw_proof_free_work(std::unique_ptr<struct vdf_work> work)
{
    if (work->pair)
        clear_vdf_value(&work->pair->start_val);
    clear_vdf_value(&work->start_val);
}
