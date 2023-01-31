#include "verifier.h"
#include "prover_slow.h"
#include "alloc.hpp"
#include "prover_base.hpp"

void VdfBaseInit(void)
{
    init_gmp();
    fesetround(FE_TOWARDZERO);
}

bool dummy_form_check_valid(form &f, integer &d)
{
    return f.check_valid(d);
}

form dummy_get_proof(Prover &p)
{
	p.GenerateProof();
	return p.GetProof();
}
