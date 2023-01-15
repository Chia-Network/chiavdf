#include "verifier.h"
#include "prover_slow.h"
#include "alloc.hpp"

void VdfBaseInit(void)
{
    init_gmp();
    fesetround(FE_TOWARDZERO);
}
