#ifndef HW_PROOF_H
#define HW_PROOF_H

#include "vdf_base.hpp"

#include <atomic>
#include <deque>
#include <thread>

#define HW_VDF_VALUE_INTERVAL 4096
#define HW_VDF_MAX_AUX_THREADS 4
#define HW_VDF_MAX_WQ 100

struct vdf_value {
    uint64_t iters;
    mpz_t a, b;
};

struct vdf_work {
    size_t raw_idx;
    uint64_t start_iters;
    uint32_t n_steps;
};

struct vdf_state {
    uint64_t target_iters;
    uint64_t cur_iters;
    std::atomic<uint64_t> done_values;
    std::vector<struct vdf_value> raw_values;
    std::vector<struct vdf_value> values;
    std::vector<struct vdf_value> proofs;
    mpz_t d, l, a2;
    std::thread aux_threads[HW_VDF_MAX_AUX_THREADS];
    std::deque<struct vdf_work> wq;
    uint32_t interval;
    std::atomic<uint8_t> aux_threads_busy;
    uint8_t idx;
    bool completed;
};

void hw_proof_calc_values(struct vdf_state *vdf, struct vdf_value *val, uint64_t next_iters, uint32_t n_steps, int thr_idx);
void hw_proof_add_value(struct vdf_state *vdf, struct vdf_value *val);
void init_vdf_state(struct vdf_state *vdf, const char *d_str, uint64_t n_iters, uint8_t idx);
void clear_vdf_state(struct vdf_state *vdf);

#endif /* HW_PROOF_H */
