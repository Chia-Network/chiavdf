#include "vdf_base.hpp"
#include "alloc.hpp"

#include <cfenv>

void VdfBaseInit(void)
{
    init_gmp();
    fesetround(FE_TOWARDZERO);
}
