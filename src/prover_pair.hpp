#ifndef PROVER_PAIR_H
#define PROVER_PAIR_H

// PairParallelProver: ParallelProver whose workers interleave independent
// compositions two at a time (src/nudupl_pair.h). It produces exactly the
// same proof as ParallelProver:
// - Bucket accumulation: consecutive products into different buckets are
//   independent and run as a pair. Products into the same bucket keep their
//   order, so every bucket's unreduced product is unchanged.
// - The descending combine sweep is software-pipelined: R_b = R_{b+1}*ys[b]
//   runs beside W_{b+1} = W_{b+2}*R_{b+1}. Each chain performs the same
//   compositions in the same order as before.
//
// The member definitions are not inline: include this header in exactly one
// translation unit of a program (vdf_hw_symbol_anchors.cpp in the hardware
// client).

#include "prover.hpp"
#include "nudupl_pair.h"

#include <thread>

PairParallelProver::PairParallelProver(Segment segm, integer D) : Prover(segm, D) {}

void PairParallelProver::GenerateProof() {
    PulmarkReducer reducer;

    this->B = GetB(D, segm.x, segm.y);
    this->L = root(-D, 4);
    this->id = form::identity(D);

    uint32_t l0 = l / 2;
    uint32_t l1 = l - l0;
    std::thread proof_thr(&PairParallelProver::ProvePart, this, 0, l, l0);
    ProvePart(1, l1, l1);

    proof_thr.join();
    if (!PerformExtraStep()) {
        return;
    }
    nucomp_form(proof, x_vals[0], x_vals[1], D, L);
    reducer.reduce(proof);
    OnFinish();
}

void PairParallelProver::ProvePart(uint8_t thr_idx, uint32_t start, uint32_t len) {
    PulmarkReducer reducer;

    form x = id;
    int64_t end = start - len;
    int64_t j;

    ProofBlockCache blocks(num_iterations, k, l, B);
    for (j = start - 1; j >= end; j--) {
        x = FastPowFormNucomp(x, D, integer(1 << k), L, reducer);

        std::vector<form> ys((1 << k));
        for (uint64_t i = 0; i < (1UL << k); i++)
            ys[i] = id;

        uint64_t limit = num_iterations / (k * l);
        if (num_iterations % (k * l))
            limit++;
        // At most one product waits for a partner with a different bucket.
        uint64_t pending_b = 0, pending_i = 0;
        for (uint64_t i = 0; i < limit; i++) {
            if (num_iterations >= k * (i * l + j + 1)) {
                uint64_t b = blocks.Get(i, j);
                if (!PerformExtraStep()) return;
                // A zero digit has exponent zero and never contributes.
                if (!b) continue;
                if (!pending_b) {
                    pending_b = b;
                    pending_i = i;
                } else if (b != pending_b) {
                    form t0 = GetForm(pending_i), t1 = GetForm(i);
                    nucomp_form_pair(ys[pending_b], ys[pending_b], t0, ys[b], ys[b], t1, D, L);
                    pending_b = 0;
                } else {
                    form t0 = GetForm(pending_i);
                    nucomp_form(ys[pending_b], ys[pending_b], t0, D, L);
                    pending_i = i;
                }
            }
        }
        if (pending_b) {
            form t0 = GetForm(pending_i);
            nucomp_form(ys[pending_b], ys[pending_b], t0, D, L);
        }

        // prod(ys[b]^b) = prod_b(prod_{c>=b} ys[c]), as in ParallelProver,
        // with step b of the running chain beside step b + 1 of the
        // weighted chain.
        uint64_t last_bucket = (1UL << k) - 1;
        form running = ys[last_bucket];
        form weighted = running;
        if (last_bucket > 1) {
            if (!PerformExtraStep()) return;
            // b = last_bucket - 1: weighted needs the new running value.
            nucomp_form(running, running, ys[last_bucket - 1], D, L);
            form prev = running;
            for (uint64_t b = last_bucket - 2; b > 0; --b) {
                if (!PerformExtraStep()) return;
                // weighted *= R_{b+1} (prev) while running becomes R_b.
                nucomp_form_pair(weighted, weighted, prev, running, running, ys[b], D, L);
                prev = running;
            }
            nucomp_form(weighted, weighted, prev, D, L);
        }
        nucomp_form(x, x, weighted, D, L);
    }

    for (uint64_t i = 0; i < uint64_t(end) * k; i++) {
        nudupl_form(x, x, D, L);
        reducer.reduce(x);
    }
    x_vals[thr_idx] = x;
}

#endif // PROVER_PAIR_H
