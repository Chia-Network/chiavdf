#include "hw_proof.hpp"

#include <algorithm>
#include <cstring>

class ProofCmp {
public:
    ProofCmp(std::vector<struct vdf_proof> &p)
    {
        proofs = p.data();
    }

    bool operator() (uint16_t a, uint16_t b)
    {
        return proofs[a].iters < proofs[b].iters;
    }

private:
    struct vdf_proof *proofs;
};

uint16_t hw_queue_proof(struct vdf_state *vdf, uint64_t seg_iters, uint16_t prev, uint8_t flags)
{
    uint16_t pos;
    struct vdf_proof proof{};

    proof.iters = seg_iters;
    if (prev != HW_VDF_PROOF_NONE) {
        proof.iters += vdf->proofs[prev].iters;
    }
    proof.seg_iters = seg_iters;
    proof.flags = flags;
    proof.prev = prev;
    proof.ref_cnt = 1;
    pos = vdf->proofs.size();

    vdf->proofs.push_back(proof);

    vdf->queued_proofs.push_back(pos);

    return pos;
}

uint8_t hw_proof_cnt_segments(struct vdf_state *vdf, uint16_t idx)
{
    uint8_t cnt = 0;
    do {
        cnt++;
        idx = vdf->proofs[idx].prev;
    } while (idx != HW_VDF_PROOF_NONE);
    return cnt;
}

void hw_proof_inc_ref(struct vdf_state *vdf, uint16_t idx)
{
    do {
        vdf->proofs[idx].ref_cnt++;
        idx = vdf->proofs[idx].prev;
    } while (idx != HW_VDF_PROOF_NONE);
}

void hw_proof_dec_ref(struct vdf_state *vdf, std::vector<uint16_t> &idxs)
{
    std::vector<uint16_t> proofs_to_del;
    int len = idxs.size();
    int last_proof_idx = vdf->proofs.size() - 1;

    for (int i = 0; i < len; i++) {
        uint16_t idx = idxs[i];
        struct vdf_proof *proof;
        do {
            proof = &vdf->proofs[idx];
            if (proof->flags & HW_VDF_PROOF_FLAG_STARTED) {
                break;
            }
            proof->ref_cnt--;
            if (!proof->ref_cnt) {
                proofs_to_del.push_back(idx);
            }
            idx = proof->prev;
        } while (idx != HW_VDF_PROOF_NONE);
    }

    std::sort(proofs_to_del.begin(), proofs_to_del.end());

    for (int i = proofs_to_del.size() - 1; i >= 0; i--) {
        uint16_t idx = proofs_to_del[i];
        if (idx == last_proof_idx) {
            last_proof_idx--;
        }
        memset(&vdf->proofs[idx], 0xff, sizeof(vdf->proofs[idx]));

        for (int j = 0; j < (int)vdf->queued_proofs.size(); j++) {
            if (vdf->queued_proofs[j] == idx) {
                vdf->queued_proofs.erase(vdf->queued_proofs.begin() + j);
            }
        }
    }
    LOG_INFO("VDF %d: Removed %d queued proofs; total proofs reduced by %zu",
            vdf->idx, proofs_to_del.size(), vdf->proofs.size() - last_proof_idx - 1);
    vdf->proofs.resize(last_proof_idx + 1);
    LOG_DEBUG("VDF %d: Proofs %zu, queued %zu", vdf->idx, vdf->proofs.size(), vdf->queued_proofs.size());
}

bool hw_proof_should_queue(struct vdf_state *vdf, uint64_t iters)
{
    uint16_t last_queued_idx = vdf->queued_proofs.back();
    return iters < vdf->proofs[last_queued_idx].iters;
}

// Checkpoint length that keeps every new segment on the value interval and the
// stored checkpoint count at or below HW_VDF_MAX_CHKP_SEGMENTS. A span that
// cannot be covered at the requested threshold uses a larger chunk.
static uint64_t hw_proof_segment_chunk(uint64_t span, uint32_t threshold,
        uint32_t interval, uint8_t room)
{
    uint64_t aligned = (uint64_t)threshold / interval * interval;
    if (aligned < interval) aligned = interval;
    if (room == 0 || span <= threshold) return span;

    uint64_t max_pieces = (uint64_t)room + 1;
    if ((span + aligned - 1) / aligned <= max_pieces) return aligned;

    uint64_t raised = (span + max_pieces - 1) / max_pieces;
    raised = (raised + interval - 1) / interval * interval;
    return raised < interval ? interval : raised;
}

static uint64_t hw_proof_queue_segments(struct vdf_state *vdf, uint16_t *prev, uint64_t iters)
{
    uint8_t base = *prev == HW_VDF_PROOF_NONE ? 0 : hw_proof_cnt_segments(vdf, *prev);
    uint8_t room = base >= HW_VDF_MAX_CHKP_SEGMENTS ? 0
        : (uint8_t)(HW_VDF_MAX_CHKP_SEGMENTS - base);
    uint64_t chunk = hw_proof_segment_chunk(iters, vdf->checkpoint_threshold, vdf->interval, room);

    while (room > 0 && iters > chunk) {
        *prev = hw_queue_proof(vdf, chunk, *prev, 0);
        iters -= chunk;
        room--;
    }
    return iters;
}

// The tail after prev. A zero span already ends on that checkpoint.
static void hw_proof_queue_request(struct vdf_state *vdf, uint16_t prev, uint64_t span)
{
    span = hw_proof_queue_segments(vdf, &prev, span);
    if (span != 0) {
        hw_queue_proof(vdf, span, prev, HW_VDF_PROOF_FLAG_IS_REQ);
        return;
    }
    if (prev != HW_VDF_PROOF_NONE) {
        auto &checkpoint = vdf->proofs[prev];
        checkpoint.flags |= HW_VDF_PROOF_FLAG_EXACT_REQ;
        // A completed checkpoint will not publish again to enqueue this request.
        if ((checkpoint.flags & HW_VDF_PROOF_FLAG_DONE)
                && std::find(vdf->done_proofs.begin(), vdf->done_proofs.end(), prev)
                    == vdf->done_proofs.end())
            vdf->done_proofs.push_back(prev);
    }
}

// Called under proofs_resize_mtx. Recovery can invalidate an ancestor after its
// descendant was computed, so eligibility covers the whole checkpoint chain.
static uint8_t hw_reusable_checkpoint_depth(const struct vdf_state *vdf, uint16_t idx)
{
    uint8_t depth = 0;
    while (idx != HW_VDF_PROOF_NONE) {
        if (idx >= vdf->proofs.size()
                || (vdf->proofs[idx].flags & HW_VDF_PROOF_FLAG_FAILED)
                || ++depth > HW_VDF_MAX_CHKP_SEGMENTS)
            return 0;
        idx = vdf->proofs[idx].prev;
    }
    return depth;
}

// Estimate the critical path in iteration units for equal-sized independent
// segments on the configured proof slots. A full checkpoint chain has only one
// tail; an earlier prefix can be faster because it leaves room for parallel work.
static uint64_t hw_proof_tail_cost(const struct vdf_state *vdf,
        uint64_t span, uint8_t depth)
{
    if (!span) return 0;
    uint8_t room = HW_VDF_MAX_CHKP_SEGMENTS - depth;
    uint64_t chunk = hw_proof_segment_chunk(span, vdf->checkpoint_threshold,
            vdf->interval, room);
    uint64_t segment_threads = std::max<uint64_t>(1, vdf->segment_threads);
    uint64_t workers = std::max<uint64_t>(1, std::min<uint64_t>(vdf->max_aux_threads,
            (vdf->max_proof_threads + segment_threads - 1) / segment_threads));
    uint64_t full = span / chunk;
    return full / workers * chunk + (full % workers ? chunk : span % chunk);
}

void hw_proof_process_req(struct vdf_state *vdf)
{
    uint64_t iters;
    uint64_t req_iters;
    uint64_t base_iters = 0;
    int i;
    uint16_t prev = HW_VDF_PROOF_NONE;

    if (vdf->req_proofs.empty()) {
        return;
    }

    req_iters = vdf->req_proofs[0].iters;
    vdf->req_proofs.erase(vdf->req_proofs.begin());
    if (!vdf->queued_proofs.empty() && hw_proof_should_queue(vdf, req_iters)) {
        std::vector<uint16_t> proofs_to_del;
        for (i = 0; i < (int)vdf->queued_proofs.size(); i++) {
            uint16_t idx = vdf->queued_proofs[i];
            bool is_chkp = !(vdf->proofs[idx].flags & HW_VDF_PROOF_FLAG_IS_REQ);
            if (is_chkp || req_iters > vdf->proofs[idx].iters) {
                continue;
            }
            proofs_to_del.push_back(idx);
            hw_request_proof(vdf, vdf->proofs[idx].iters, false);
        }
        hw_proof_dec_ref(vdf, proofs_to_del);
    }

    for (i = vdf->queued_proofs.size() - 1; i >= 0; i--) {
        uint16_t idx = vdf->queued_proofs[i];
        if (vdf->proofs[idx].flags & HW_VDF_PROOF_FLAG_IS_REQ) {
            continue;
        }
        iters = vdf->proofs[idx].iters;
        if (iters <= req_iters && hw_reusable_checkpoint_depth(vdf, idx)) {
            base_iters = iters;
            prev = idx;
            break;
        }
    }

    // Dispatch removes checkpoints from queued_proofs. Reuse their owned proof
    // objects while they are running or completed, within this native session.
    uint8_t depth = prev == HW_VDF_PROOF_NONE ? 0 : hw_reusable_checkpoint_depth(vdf, prev);
    uint64_t best_cost = hw_proof_tail_cost(vdf, req_iters - base_iters, depth);
    for (size_t pos = vdf->proofs.size(); pos-- > 0; ) {
        const auto &checkpoint = vdf->proofs[pos];
        if (checkpoint.iters > req_iters
                || (checkpoint.flags & HW_VDF_PROOF_FLAG_IS_REQ)
                || !(checkpoint.flags & (HW_VDF_PROOF_FLAG_STARTED | HW_VDF_PROOF_FLAG_DONE)))
            continue;
        uint8_t checkpoint_depth = hw_reusable_checkpoint_depth(vdf, static_cast<uint16_t>(pos));
        if (!checkpoint_depth) continue;
        uint64_t cost = hw_proof_tail_cost(vdf, req_iters - checkpoint.iters, checkpoint_depth);
        if (cost > best_cost || (cost == best_cost && checkpoint.iters <= base_iters))
            continue;
        best_cost = cost;
        base_iters = checkpoint.iters;
        prev = static_cast<uint16_t>(pos);
    }
    if (prev != HW_VDF_PROOF_NONE)
        hw_proof_inc_ref(vdf, prev);

    hw_proof_queue_request(vdf, prev, req_iters - base_iters);

    {
        ProofCmp cmp(vdf->proofs);
        std::sort(vdf->queued_proofs.begin(), vdf->queued_proofs.end(), cmp);
    }
}

bool hw_proof_req_cmp(struct vdf_proof_req &a, struct vdf_proof_req &b)
{
    return a.iters < b.iters;
}

void hw_request_proof(struct vdf_state *vdf, uint64_t iters, bool is_chkp)
{
    vdf->req_proofs.push_back({iters, is_chkp});
    std::sort(vdf->req_proofs.begin(), vdf->req_proofs.end(), hw_proof_req_cmp);
}
