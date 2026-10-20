#ifndef HW_PROOF_H
#define HW_PROOF_H

#include "vdf_base.hpp"
#include "hw_interface.hpp"
#include "hw_util.hpp"
#include "bqfc.h"

#include <atomic>
#include <array>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#define HW_VDF_VALUE_INTERVAL 4000
#define HW_VDF_VALUE_INTERVAL_DIVISORS { 2, 4, 5, 8, 10, 16, 20 }
#define HW_VDF_CHKP_INTERVAL 1000000
// Smallest threshold that can split one request into an aligned checkpoint and a remainder.
#define HW_VDF_MIN_CHKP_THRESHOLD (2 * HW_VDF_VALUE_INTERVAL)
// Timelord proofs store the checkpoint count in one byte. The protocol accepts
// at most this many checkpoints in one frame.
#define HW_VDF_MAX_CHKP_SEGMENTS (64 - 3)
#define HW_VDF_MAX_AUX_THREADS 64
#define HW_VDF_DEFAULT_MAX_AUX_THREADS 4
#define HW_VDF_WQ_WARN_MULT 2
#define HW_VDF_B_SIZE 33

#define HW_VDF_PROOF_FLAG_DONE 1
#define HW_VDF_PROOF_FLAG_IS_REQ 2
#define HW_VDF_PROOF_FLAG_STARTED 4
// Queued checkpoint whose end is the requested iteration. It is delivered as
// that request and still exports B for any longer chain that points at it.
#define HW_VDF_PROOF_FLAG_EXACT_REQ 8
// Unpublished. A chain that includes this segment is dropped, not waited on.
#define HW_VDF_PROOF_FLAG_FAILED 16
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
    uint16_t ref_cnt;
};

// Owned wire-order snapshot: final first, then checkpoints from newest to oldest.
struct vdf_proof_delivery {
    vdf_proof final;
    std::vector<vdf_proof> checkpoints;
};

struct vdf_proof_opts {
    uint8_t max_aux_threads;
    uint8_t max_proof_threads;
    uint32_t checkpoint_threshold = HW_VDF_CHKP_INTERVAL;
    bool retain_asic_values = false;
};

struct hw_aux_job {
    struct vdf_work *work;
    size_t proof_idx;
};

// One timelord checkpoint after the proof header.
struct hw_timelord_segment {
    uint8_t iters[sizeof(uint64_t)];
    uint8_t B[HW_VDF_B_SIZE];
    uint8_t proof[BQFC_FORM_SIZE];
};

struct vdf_retained_value {
    uint64_t generation;
    uint64_t iters;
    form value;
};

struct vdf_state {
    uint64_t target_iters;
    uint64_t iters_offset;
    uint64_t cur_iters;
    std::atomic<uint64_t> done_values;
    std::atomic<uint64_t> done_iters;
    std::atomic<uint64_t> elapsed_us;
    timepoint_t start_time;
    //std::vector<struct vdf_value> raw_values;
    struct vdf_value last_val;
    std::mutex values_resize_mtx;
    std::unique_ptr<std::atomic<form *>[]> value_blocks;
    size_t value_block_count = 0;
    std::unique_ptr<std::atomic<uint8_t>[]> valid_values;
    // Grid points a failed fill will not publish. Waiters on these positions return.
    std::unique_ptr<std::atomic<uint8_t>[]> abandoned_values;
    std::mutex valid_values_mtx;
    std::condition_variable valid_values_cv;
    std::deque<struct vdf_proof_req> req_proofs;
    std::vector<struct vdf_proof> proofs;
    std::mutex proofs_resize_mtx;
    std::vector<uint16_t> queued_proofs; /* sorted queued proofs */
    std::vector<uint16_t> done_proofs; /* requested and not sent */
    integer D, L, a2;
    std::deque<struct vdf_work *> wq;
    uint32_t wq_warn_thres[2];
    //std::mutex wq_mtx;
    uint32_t interval;
    uint32_t chkp_interval;
    uint32_t checkpoint_threshold;
    uint32_t n_bad;
    uint32_t n_skipped;
    uint32_t log_cnt;
    std::atomic<uint64_t> aux_threads_busy;
    // Set while a proof on this slot must stop. Cleared when the next job is submitted.
    std::atomic<uint8_t> aux_cancel[HW_VDF_MAX_AUX_THREADS];
    std::array<std::thread, HW_VDF_MAX_AUX_THREADS> aux_threads;
    std::mutex aux_job_mtx;
    std::condition_variable aux_job_cv;
    uint64_t aux_job_pending = 0;
    std::array<hw_aux_job, HW_VDF_MAX_AUX_THREADS> aux_jobs{};
    std::atomic<uint8_t> n_proof_threads;
    uint8_t idx;
    uint8_t max_aux_threads;
    uint8_t max_proof_threads;
    bool completed;
    std::atomic<bool> stopping;
    bool init_done = false;
    bool retain_asic_values = false;
    std::mutex retained_values_mtx;
    std::deque<vdf_retained_value> retained_values;
    uint64_t retained_generation = 0;
};

int hw_proof_add_value(struct vdf_state *vdf, struct vdf_value *val);
form *hw_proof_last_good_form(struct vdf_state *vdf, size_t *out_pos);
void hw_proof_handle_value(struct vdf_state *vdf, struct vdf_value *val);
// Signal cancellation without draining; safe while the owner is finishing work.
void hw_request_stop_proof(struct vdf_state *vdf);
// The sole owner drains and joins workers before state can be cleared.
void hw_stop_proof(struct vdf_state *vdf);
void hw_request_proof(struct vdf_state *vdf, uint64_t iters, bool is_chkp);
uint16_t hw_queue_proof(struct vdf_state *vdf, uint64_t seg_iters, uint16_t prev, uint8_t flags);
void hw_proof_process_req(struct vdf_state *vdf);
// Wire bytes for one proof: 4-byte length, then hex of the header and each checkpoint.
std::vector<uint8_t> hw_proof_timelord_frame(const vdf_proof_delivery &delivery);
void hw_compute_proof(struct vdf_state *vdf, size_t proof_idx, struct vdf_proof *out_proof, uint8_t thr_idx);
int hw_retrieve_proof(struct vdf_state *vdf, struct vdf_proof_delivery *proof);
void hw_proof_wait_values(struct vdf_state *vdf, bool finish_work);
bool hw_proof_copy_retained_value(struct vdf_state *vdf, uint64_t target, uint64_t floor,
                                  form *value, uint64_t *iters);
void hw_proof_reset_retained_values(struct vdf_state *vdf);
void init_vdf_state(struct vdf_state *vdf, struct vdf_proof_opts *params, const char *d_str, const uint8_t *init_form, uint64_t n_iters, uint8_t idx);
void clear_vdf_state(struct vdf_state *vdf);

#endif // HW_PROOF_H
