#ifndef PROVER_INTERFACE_H
#define PROVER_INTERFACE_H

#include <atomic>
#include <cstdint>

class PulmarkReducer;

class Prover {
  public:
    Prover(Segment segm, integer D);
    virtual ~Prover() = default;

    virtual form GetForm(uint64_t iteration) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool PerformExtraStep() = 0;
    virtual void OnFinish() = 0;

    bool IsFinished();
    form GetProof();
    uint64_t GetBlock(uint64_t i, uint64_t k, uint64_t T, integer& B);
    void GenerateProof();

  protected:
    Segment segm;
    integer D;
    form proof;
    uint64_t num_iterations;
    uint32_t k;
    uint32_t l;
    std::atomic<bool> is_finished;
};

#define PARALLEL_PROVER_N_THREADS 2

class ParallelProver : public Prover {
  private:
    static void ProofThread(ParallelProver* prover, uint8_t thr_idx, uint32_t start, uint32_t len);
    void SquareFormN(form& f, uint64_t cnt, PulmarkReducer& reducer);
    void ProvePart(uint8_t thr_idx, uint32_t start, uint32_t len);

  public:
    ParallelProver(Segment segm, integer D);
    void GenerateProof();

  protected:
    integer B;
    integer L;
    form id;
    form x_vals[PARALLEL_PROVER_N_THREADS];
};

// Apple Silicon leaves most of its width idle in one GCD chain, so the
// hardware client there interleaves independent compositions two at a time
// (src/nudupl_pair.h). The kernels use unsigned __int128.
#if defined(__APPLE__) && defined(__aarch64__)
#define CHIAVDF_PAIR_KERNELS 1
#endif

// ParallelProver with paired compositions; same proofs (src/prover_pair.hpp).
class PairParallelProver : public Prover {
  public:
    PairParallelProver(Segment segm, integer D);
    void GenerateProof();

  protected:
    integer B;
    integer L;
    form id;
    form x_vals[PARALLEL_PROVER_N_THREADS];

  private:
    void ProvePart(uint8_t thr_idx, uint32_t start, uint32_t len);
};

#endif // PROVER_INTERFACE_H
