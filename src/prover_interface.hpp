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

void nucomp_form(form &a, form const& b, form const& c, integer const& D, integer const& L);

class ParallelProver : public Prover {
  private:
    static void ProofThread(ParallelProver* prover, uint8_t thr_idx, uint32_t start, uint32_t len);
    void SquareFormN(form& f, uint64_t cnt, PulmarkReducer& reducer);
    void ProvePart(uint8_t thr_idx, uint32_t start, uint32_t len);

  public:
    ParallelProver(Segment segm, integer D);
    void GenerateProof();

  protected:
    // a = b * c and d = e * f, two products with no dependency between them.
    // Workers route independent bucket products and the two chains of the
    // combine sweep through here, so a subclass can run them together
    // (HwProver uses paired kernels). d, e and f must not alias a, b or c.
    virtual void ComposeTwo(form& a, const form& b, const form& c,
                            form& d, const form& e, const form& f);

    integer B;
    integer L;
    form id;
    form x_vals[PARALLEL_PROVER_N_THREADS];
};

inline void ParallelProver::ComposeTwo(form& a, const form& b, const form& c,
                                       form& d, const form& e, const form& f)
{
    nucomp_form(a, b, c, D, L);
    nucomp_form(d, e, f, D, L);
}

#endif // PROVER_INTERFACE_H
