#include <x86intrin.h>

#include "include.h"

bool use_divide_table=false;
int gcd_base_bits=50;
int gcd_128_max_iter=3;
std::string asmprefix="cel_";
bool enable_all_instructions=false;

#include "parameters.h"

#define COMPILE_ASM

#ifdef TEST_ASM
    #undef TEST_ASM
#endif

#include "bit_manipulation.h"
#include "double_utility.h"
#include "integer.h"

#include "gpu_integer.h"
#include "gpu_integer_divide.h"

#include "gcd_base_continued_fractions.h"
#include "gcd_base_divide_table.h"
#include "gcd_128.h"
#include "gcd_unsigned.h"

#include "asm_types.h"
#include "asm_vm.h"

#include "asm_base.h"
#include "asm_gcd_base_continued_fractions.h"
#include "asm_gcd_base_divide_table.h"
#include "asm_gcd_128.h"
#include "asm_gcd_unsigned.h"
#include "asm_avx512_ifma.h"

#include "asm_main.h"

int main(int argc, char** argv) {
    set_rounding_mode();

    string filename="asm_compiled.s";
    
    bool compile_avx512=false;

    if((argc==2)&&(strcmp(argv[1],"avx2")==0))
    {
       use_divide_table=true;
       gcd_base_bits=63;
       gcd_128_max_iter=2;
       asmprefix="avx2_";
       enable_all_instructions=true;
       filename="avx2_asm_compiled.s";
    }

    if((argc==2)&&(strcmp(argv[1],"avx512")==0))
    {
        enable_all_instructions=true;
        asmprefix="avx512_";
        filename="avx512_asm_compiled.s";
        compile_avx512=true;
    }

    if (compile_avx512) {
        asm_code::compile_asm_avx512(filename);
    } else {
        asm_code::compile_asm(filename);
    }
}
