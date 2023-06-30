#ifndef PROVER_PARALLEL_H
#define PROVER_PARALLEL_H

#include <atomic>

#include "proof_common.h"
#include "util.h"

#define PARALLEL_PROVER_N_THREADS 2

class ParallelProver : public Prover {
  private:
    static void ProofThread(ParallelProver *prover, uint8_t thr_idx, uint32_t start, uint32_t len) {
        prover->ProvePart(thr_idx, start, len);
    }

    void SquareFormN(form &f, uint64_t cnt, PulmarkReducer &reducer)
    {
        for (uint64_t i = 0; i < cnt; i++) {
            nudupl_form(f, f, D, L);
            reducer.reduce(f);
        }
    }

    void ProvePart(uint8_t thr_idx, uint32_t start, uint32_t len) {
        PulmarkReducer reducer;

        uint64_t k1 = k / 2;
        uint64_t k0 = k - k1;
        form x = id;
        int64_t end = start - len;
        int64_t j;

        for (j = start - 1; j >= end; j--) {
            x = FastPowFormNucomp(x, D, integer(1 << k), L, reducer);

            std::vector<form> ys((1 << k));
            for (uint64_t i = 0; i < (1UL << k); i++)
                ys[i] = id;

            form *tmp;
            uint64_t limit = num_iterations / (k * l);
            if (num_iterations % (k * l))
                limit++;
            for (uint64_t i = 0; i < limit; i++) {
                if (num_iterations >= k * (i * l + j + 1)) {
                    uint64_t b = GetBlock(i*l + j, k, num_iterations, B);
                    if (!PerformExtraStep()) return;
                    tmp = GetForm(i);
                    nucomp_form(ys[b], ys[b], *tmp, D, L);
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

        SquareFormN(x, end * k, reducer);
        x_vals[thr_idx] = x;
    }
  public:
    ParallelProver(Segment segm, integer D) : Prover(segm, D) {}
    void GenerateProof();

  protected:
    integer B;
    integer L;
    form id;
    form x_vals[PARALLEL_PROVER_N_THREADS];
};

void ParallelProver::GenerateProof() {
    PulmarkReducer reducer;

    this->B = GetB(D, segm.x, segm.y);
    this->L = root(-D, 4);
    this->id = form::identity(D);

    uint32_t l0 = l / 2;
    uint32_t l1 = l - l0;
    std::thread proof_thr(ParallelProver::ProofThread, this, 0, l, l0);
    ProvePart(1, l1, l1);

    proof_thr.join();
    if (!PerformExtraStep()) {
        return;
    }
    nucomp_form(proof, x_vals[0], x_vals[1], D, L);
    reducer.reduce(proof);
    OnFinish();
}

#endif // PROVER_PARALLEL_H
