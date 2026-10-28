#ifndef PROVER_IMPL_H
#define PROVER_IMPL_H

#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include "proof_block_cache.hpp"

inline Prover::Prover(Segment segm, integer D) {
    this->segm = segm;
    this->D = D;
    this->num_iterations = segm.length;
    is_finished = false;
}

inline bool Prover::IsFinished() {
    return is_finished;
}

inline form Prover::GetProof() {
    return proof;
}

inline uint64_t Prover::GetBlock(uint64_t i, uint64_t k, uint64_t T, integer& B) {
    integer res = FastPow(2, T - k * (i + 1), B);
    mpz_mul_2exp(res.impl, res.impl, k);
    res = res / B;
    auto res_vector = res.to_vector();
    return res_vector.empty() ? 0 : res_vector[0];
}

inline void Prover::GenerateProof() {
    PulmarkReducer reducer;

    integer B = GetB(D, segm.x, segm.y);
    integer L = root(-D, 4);
    form id;
    try {
        id = form::identity(D);
    } catch (std::exception& e) {
        std::cout << "Warning: Could not create identity: " << e.what() << "\n";
        std::cout << "Discriminant: " << D.to_string() << "\n";
        std::cout << "Segment start:" << segm.start << "\n";
        std::cout << "Segment length:" << segm.length << "\n";
        std::cout << std::flush;

        return;
    }
    uint64_t k1 = k / 2;
    uint64_t k0 = k - k1;
    form x = id;

    ProofBlockCache blocks(num_iterations, k, l, B);
    for (int64_t j = l - 1; j >= 0; j--) {
        x = FastPowFormNucomp(x, D, integer(1 << k), L, reducer);

        std::vector<form> ys((1 << k));
        for (uint64_t i = 0; i < (1UL << k); i++)
            ys[i] = id;

        uint64_t limit = num_iterations / (k * l);
        if (num_iterations % (k * l))
            limit++;
        for (uint64_t i = 0; i < limit; i++) {
            if (num_iterations >= k * (i * l + j + 1)) {
                uint64_t b = blocks.Get(i, j);
                if (b >= (1UL << k)) {
                    throw std::runtime_error("GenerateProof block index out of bounds");
                }
                if (!PerformExtraStep()) return;
                form tmp = GetForm(i);
                nucomp_form(ys[b], ys[b], tmp, D, L);
            }
        }

        for (uint64_t b1 = 0; b1 < (1UL << k1); b1++) {
            form z = id;
            for (uint64_t b0 = 0; b0 < (1UL << k0); b0++) {
                if (!PerformExtraStep()) return;
                nucomp_form(z, z, ys[b1 * (1 << k0) + b0], D, L);
            }
            z = FastPowFormNucomp(z, D, integer(b1 * (1 << k0)), L, reducer);
            nucomp_form(x, x, z, D, L);
        }

        for (uint64_t b0 = 0; b0 < (1UL << k0); b0++) {
            form z = id;
            for (uint64_t b1 = 0; b1 < (1UL << k1); b1++) {
                if (!PerformExtraStep()) return;
                nucomp_form(z, z, ys[b1 * (1 << k0) + b0], D, L);
            }
            z = FastPowFormNucomp(z, D, integer(b0), L, reducer);
            nucomp_form(x, x, z, D, L);
        }
    }
    reducer.reduce(x);
    proof = x;
    OnFinish();
}

inline ParallelProver::ParallelProver(Segment segm, integer D,
                                      uint8_t segment_threads) : Prover(segm, D) {
    this->segment_threads = ClampSegmentThreads(segment_threads);
}

inline void ParallelProver::ProofThread(ParallelProver* prover, uint8_t thr_idx, uint32_t start, uint32_t len) {
    prover->ProvePart(thr_idx, start, len);
}

inline void ParallelProver::SquareFormN(form& f, uint64_t cnt, PulmarkReducer& reducer)
{
    for (uint64_t i = 0; i < cnt; i++) {
        nudupl_form(f, f, D, L);
        reducer.reduce(f);
    }
}

inline void ParallelProver::ProvePart(uint8_t thr_idx, uint32_t start, uint32_t len) {
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
        // Products into different buckets are independent, so they go
        // through ComposeTwo in pairs. At most one product waits for a
        // partner, and products into one bucket keep their order.
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
                    ComposeTwo(ys[pending_b], ys[pending_b], t0, ys[b], ys[b], t1);
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

        // prod(ys[b]^b) = prod_b(prod_{c>=b} ys[c]). Bucket zero has
        // weight zero, so a descending sweep needs no small exponentiations.
        // The running product R_b = R_{b+1} * ys[b] and the weighted product
        // W_b = W_{b+1} * R_b are pipelined: R_b is formed beside W_{b+1}.
        // Each chain performs the same compositions in the same order.
        uint64_t last_bucket = (1UL << k) - 1;
        form running = ys[last_bucket];
        form weighted = running;
        if (last_bucket > 1) {
            if (!PerformExtraStep()) return;
            nucomp_form(running, running, ys[last_bucket - 1], D, L);
            form previous = running;
            for (uint64_t b = last_bucket - 2; b > 0; --b) {
                if (!PerformExtraStep()) return;
                ComposeTwo(weighted, weighted, previous, running, running, ys[b]);
                previous = running;
            }
            nucomp_form(weighted, weighted, previous, D, L);
        }
        nucomp_form(x, x, weighted, D, L);
    }

    SquareFormN(x, end * k, reducer);
    x_vals[thr_idx] = x;
}

inline void ParallelProver::GenerateProof() {
    PulmarkReducer reducer;

    this->B = GetB(D, segm.x, segm.y);
    this->L = root(-D, 4);
    this->id = form::identity(D);

    uint32_t n = segment_threads;
    if (n > l)
        n = l;
    if (n < 1)
        n = 1;
    uint32_t base = l / n;
    uint32_t rem = l % n;
    uint32_t starts[PROVER_MAX_SEGMENT_THREADS];
    uint32_t lens[PROVER_MAX_SEGMENT_THREADS];
    uint32_t offset = 0;
    for (uint32_t i = 0; i < n; i++) {
        lens[i] = base + (i >= n - rem ? 1 : 0);
        starts[i] = l - offset;
        offset += lens[i];
    }

    // Ranges 0..n-2 run on new threads, the bottom range on the caller. The
    // guard joins every started thread when this scope exits, including when
    // thread creation or the caller's own ProvePart throws.
    std::vector<std::thread> proof_thrs;
    struct JoinAll {
        std::vector<std::thread>& threads;
        ~JoinAll() {
            for (auto& thr : threads)
                if (thr.joinable())
                    thr.join();
        }
    } join_all{proof_thrs};

    proof_thrs.reserve(n - 1);
    for (uint32_t i = 0; i + 1 < n; i++)
        proof_thrs.emplace_back(ParallelProver::ProofThread, this, i,
                                starts[i], lens[i]);
    ProvePart(n - 1, starts[n - 1], lens[n - 1]);

    for (auto& thr : proof_thrs)
        thr.join();
    if (!PerformExtraStep()) {
        return;
    }
    if (n == 1) {
        proof = x_vals[0];
    } else {
        nucomp_form(proof, x_vals[0], x_vals[1], D, L);
        for (uint32_t i = 2; i < n; i++)
            nucomp_form(proof, proof, x_vals[i], D, L);
    }
    reducer.reduce(proof);
    OnFinish();
}

#endif // PROVER_IMPL_H
