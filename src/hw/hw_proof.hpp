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
#define HW_VDF_VALUE_INTERVAL_DIVISORS { 2, 4, 5, 8, 10, 16, 20 }
#define HW_VDF_CHKP_INTERVAL 1000000
#define HW_VDF_MAX_AUX_THREADS 4
#define HW_VDF_MAX_PROOF_THREADS 3
#define HW_VDF_WQ_WARN_MULT 2
#define HW_VDF_B_SIZE 33

#define HW_VDF_PROOF_FLAG_DONE 1
#define HW_VDF_PROOF_FLAG_IS_REQ 2
#define HW_VDF_PROOF_NONE ((uint16_t)0xffff)

struct vdf_work {
    //size_t raw_idx;
    struct vdf_value start_val;
    uint64_t start_iters;
    uint32_t n_steps;
};

struct vdf_proof_req {
    uint64_t iters;
    bool is_chkp;
};

struct vdf_proof {
    uint64_t iters;
    uint64_t seg_iters;
    uint8_t y[BQFC_FORM_SIZE];
    uint8_t proof[BQFC_FORM_SIZE];
    uint8_t B[HW_VDF_B_SIZE];

    uint8_t flags;
    uint16_t prev;
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
    std::deque<struct vdf_proof_req> req_proofs;
    std::vector<struct vdf_proof> proofs;
    std::mutex proofs_resize_mtx;
    std::vector<uint16_t> chkp_proofs; /* sorted checkpoint proofs */
    std::vector<uint16_t> queued_proofs; /* sorted queued proofs */
    std::vector<uint16_t> done_proofs; /* requested and not sent */
    mpz_t d, l, a2;
    std::thread aux_threads[HW_VDF_MAX_AUX_THREADS];
    std::deque<struct vdf_work *> wq;
    uint32_t wq_warn_thres[2];
    //std::mutex wq_mtx;
    uint32_t interval;
    uint32_t chkp_interval;
    uint32_t n_bad;
    uint32_t log_cnt;
    std::atomic<uint8_t> aux_threads_busy;
    std::atomic<uint8_t> n_proof_threads;
    uint8_t idx;
    bool completed;
    bool stopping;
    bool init_done;
};

void hw_proof_add_value(struct vdf_state *vdf, struct vdf_value *val);
void hw_proof_handle_value(struct vdf_state *vdf, struct vdf_value *val);
void hw_stop_proof(struct vdf_state *vdf);
void hw_request_proof(struct vdf_state *vdf, uint64_t iters, bool is_chkp);
void hw_compute_proof(struct vdf_state *vdf, size_t proof_idx, struct vdf_proof *out_proof, uint8_t thr_idx);
int hw_retrieve_proof(struct vdf_state *vdf, struct vdf_proof **proof);
void init_vdf_state(struct vdf_state *vdf, const char *d_str, const uint8_t *init_form, uint64_t n_iters, uint8_t idx);
void clear_vdf_state(struct vdf_state *vdf);

#endif // HW_PROOF_H
