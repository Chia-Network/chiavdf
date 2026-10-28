#ifndef HW_PROOF_INTERNAL_H
#define HW_PROOF_INTERNAL_H

// Shared by hw_proof.cpp, hw_proof_fill.cpp and the hardware-free tests.

#include "hw_proof.hpp"

form *hw_proof_value_at(struct vdf_state *vdf, size_t pos);
int hw_proof_wait_value(struct vdf_state *vdf, size_t pos);

// Build the proof engine for one segment. Used by hw_compute_proof and by the
// hardware-free test so the segment thread wiring can be checked directly.
std::unique_ptr<ParallelProver> hw_make_prover(Segment segm, integer D,
        struct vdf_state *vdf, uint8_t slot);
void hw_proof_add_intermediate(struct vdf_state *vdf, struct vdf_value *val, size_t pos);
void hw_proof_process_work(struct vdf_state *vdf);

// A fill that stops short never publishes this grid range. Proofs that read
// it fail, and waiters on those positions are released.
void hw_proof_abandon_gap(struct vdf_state *vdf, uint64_t from_iters, uint64_t to_iters);

// One exit for an aux job. Unpublished proofs stay unpublished. A failed
// segment is marked so retrieval does not wait on its chain. A value job
// that does not finish abandons the grid range it still owed. A direct
// caller passes a slot outside the worker array and only updates the proof.
void hw_proof_finish_job(struct vdf_state *vdf, uint8_t slot, bool proving,
        size_t proof_idx, bool published, bool track_gap = false,
        uint64_t gap_from = 0, uint64_t gap_to = 0);

// hw_proof_fill.cpp: squaring from ASIC values to grid points.

// Run a fill job (one fill, or two in lockstep when work->pair is set) on
// aux slot thr_idx. Takes ownership of work; releases the slot when done.
void hw_proof_calc_values(struct vdf_state *vdf, struct vdf_work *work, int thr_idx);

// Take the next fill from the queue for one of free_slots idle slots. With
// paired kernels on, a second fill joins it when the queue holds more fills
// than the other free slots could start; a lone fill finishes sooner on its
// own thread.
std::unique_ptr<struct vdf_work> hw_proof_take_fill(struct vdf_state *vdf, int free_slots);

// Clear and delete a fill job that will not run, including its pair.
void hw_proof_free_work(std::unique_ptr<struct vdf_work> work);

#endif // HW_PROOF_INTERNAL_H
