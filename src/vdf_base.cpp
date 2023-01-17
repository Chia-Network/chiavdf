#include "verifier.h"
#include "prover_slow.h"
#include "alloc.hpp"

void VdfBaseInit(void)
{
    init_gmp();
    fesetround(FE_TOWARDZERO);
}

bool dummy_form_check_valid(form &f, integer &d)
{
    return f.check_valid(d);
}
