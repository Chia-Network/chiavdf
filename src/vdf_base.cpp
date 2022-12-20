#include "verifier.h"
#include "alloc.hpp"

void VdfBaseInit(void)
{
    init_gmp();
    fesetround(FE_TOWARDZERO);
}
