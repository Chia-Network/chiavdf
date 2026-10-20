#include "verifier.h"
#include "prover_slow.h"
#include "alloc.hpp"
#include "prover_base.hpp"
#include "prover_parallel.hpp"

void VdfBaseInit(void)
{
    init_gmp();
    fesetround(FE_TOWARDZERO);
}

bool dummy_form_check_valid(form &f, integer &d)
{
    return f.check_valid(d);
}

form dummy_get_proof(ParallelProver &p)
{
    p.GenerateProof();
    return p.GetProof();
}

// Hardware proof threads call this from another translation unit. A const
// member pointer has internal linkage and is dropped; an exported one keeps
// the out-of-line copy that an optimized build would otherwise inline away.
extern void (ParallelProver::*hw_generate_proof_anchor)();
void (ParallelProver::*hw_generate_proof_anchor)() = &ParallelProver::GenerateProof;
