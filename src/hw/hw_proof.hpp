#ifndef HW_PROOF_H
#define HW_PROOF_H

#include "vdf_base.hpp"
#include "hw_interface.hpp"

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

#define HW_VDF_VALUE_INTERVAL 4000
#define HW_VDF_MAX_AUX_THREADS 4
#define HW_VDF_MAX_WQ 100

struct vdf_work {
    //size_t raw_idx;
    struct vdf_value start_val;
    uint64_t start_iters;
    uint32_t n_steps;
};

typedef std::chrono::time_point<std::chrono::high_resolution_clock> timepoint_t;

struct vdf_state {
    uint64_t proof_iters;
    uint64_t target_iters;
    uint64_t cur_iters;
    std::atomic<uint64_t> done_values;
    std::atomic<uint64_t> done_iters;
    std::atomic<uint64_t> elapsed_us;
    timepoint_t start_time;
    //std::vector<struct vdf_value> raw_values;
    struct vdf_value last_val;
    std::vector<form> values;
    //std::vector<struct vdf_value> proofs;
    mpz_t d, l, a2;
    std::thread aux_threads[HW_VDF_MAX_AUX_THREADS];
    std::deque<struct vdf_work *> wq;
    //std::mutex wq_mtx;
    uint32_t interval;
    std::atomic<uint8_t> aux_threads_busy;
    uint8_t idx;
    bool completed;
};

void hw_proof_calc_values(struct vdf_state *vdf, struct vdf_value *val, uint64_t next_iters, uint32_t n_steps, int thr_idx);
void hw_proof_add_value(struct vdf_state *vdf, struct vdf_value *val);
void hw_proof_handle_value(struct vdf_state *vdf, struct vdf_value *val);
void hw_get_proof(struct vdf_state *vdf);
void init_vdf_state(struct vdf_state *vdf, const char *d_str, uint64_t n_iters, uint8_t idx);
void clear_vdf_state(struct vdf_state *vdf);

#endif /* HW_PROOF_H */
