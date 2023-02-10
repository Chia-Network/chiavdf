#ifndef HW_PROOF_H
#define HW_PROOF_H

#include "vdf_base.hpp"
#include "hw_interface.hpp"
#include "hw_util.hpp"
#include "bqfc.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

#define HW_VDF_VALUE_INTERVAL 4000
#define HW_VDF_MAX_AUX_THREADS 4
#define HW_VDF_MAX_PROOF_THREADS 2
#define HW_VDF_MAX_WQ 100

struct vdf_work {
    //size_t raw_idx;
    struct vdf_value start_val;
    uint64_t start_iters;
    uint32_t n_steps;
};

struct vdf_proof {
    uint64_t iters;
    uint8_t y[BQFC_FORM_SIZE];
    uint8_t proof[BQFC_FORM_SIZE];
};

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
    std::vector<uint8_t> valid_values;
    std::mutex valid_values_mtx;
    std::vector<uint64_t> req_proofs;
    std::vector<struct vdf_proof *> proofs;
    mpz_t d, l, a2;
    std::thread aux_threads[HW_VDF_MAX_AUX_THREADS];
    std::deque<struct vdf_work *> wq;
    //std::mutex wq_mtx;
    uint32_t interval;
    uint32_t n_bad;
    uint32_t log_cnt;
    std::atomic<uint8_t> aux_threads_busy;
    uint8_t idx;
    bool completed;
    bool stopping;
    bool init_done;
};

void hw_proof_add_value(struct vdf_state *vdf, struct vdf_value *val);
void hw_proof_handle_value(struct vdf_state *vdf, struct vdf_value *val);
void hw_stop_proof(struct vdf_state *vdf);
void hw_request_proof(struct vdf_state *vdf, uint64_t iters);
void hw_compute_proof(struct vdf_state *vdf, uint64_t proof_iters, struct vdf_proof *proof, uint8_t thr_idx);
int hw_retrieve_proof(struct vdf_state *vdf, struct vdf_proof **proof);
void init_vdf_state(struct vdf_state *vdf, const char *d_str, const uint8_t *init_form, uint64_t n_iters, uint8_t idx);
void clear_vdf_state(struct vdf_state *vdf);

#endif // HW_PROOF_H
