#include "verifier.h"
#include "prover_base.hpp"
#include "prover_parallel.hpp"
#if defined(CHIAVDF_PAIR_KERNELS)
#include "prover_pair.hpp"
#endif

bool hw_dummy_form_check_valid(form& f, integer& d)
{
    return f.check_valid(d);
}

form hw_dummy_get_proof(ParallelProver& p)
{
    p.GenerateProof();
    return p.GetProof();
}

void hw_dummy_verify_wesolowski(integer& D, form x, form y, form proof, uint64_t iters, bool& is_valid)
{
    VerifyWesolowskiProof(D, x, y, proof, iters, is_valid);
}

integer hw_dummy_get_B(const integer& D, form& x, form& y)
{
    return GetB(D, x, y);
}
