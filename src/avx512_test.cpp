#include "include.h"

#define ENABLE_TRACK_CYCLES

#include "bit_manipulation.h"
#include "double_utility.h"
#include "generic.h"
#include "parameters.h"
#include "asm_main.h"
#include "integer.h"
#include "vdf_new.h"
#include "nucomp.h"
#include "picosha2.h"
#include "proof_common.h"

#include "threading.h"
#include "avx512_integer.h"
#include "vdf_fast.h"
#include "create_discriminant.h"

#include <cstdlib>

#include <bitset>

template<class intnx, class avx512_intnx, class intjx, class avx512_intjx> void do_benchmark(string name, int num_bits) {
    const int num_iterations_outer=1000;
    const int num_iterations_inner=1000;

    name += "_" + to_string(num_bits);

    integer a_int=rand_integer(num_bits);
    integer b_int=rand_integer(num_bits);

    intnx a;
    mpz_set(a._(), a_int.impl);

    intnx b;
    mpz_set(b._(), b_int.impl);

    intnx c;
    intjx d;

    avx512_intnx a_avx512;
    avx512_intnx b_avx512;
    avx512_intnx c_avx512;
    avx512_intjx d_avx512;

    #define DO_BENCH(x) for (int i=0;i<num_iterations_inner;++i) { x; }

    for (int i=0;i<num_iterations_outer;++i) {
        { TRACK_CYCLES_NAMED(name + "_nothing"          ); DO_BENCH(                                                                                        )}
        { TRACK_CYCLES_NAMED(name + "_to_avx512"        ); DO_BENCH(    a_avx512=a;                                                                         )}
        { TRACK_CYCLES_NAMED(name + "_to_avx512"        ); DO_BENCH(    b_avx512=b;                                                                         )}
        { TRACK_CYCLES_NAMED(name + "_add_avx512"       ); DO_BENCH(    c_avx512.set_add(a_avx512, b_avx512);                                               )}
        { TRACK_CYCLES_NAMED(name + "_add_to_gmp"       ); DO_BENCH(    c_avx512.assign(c);                                                                 )}
        { TRACK_CYCLES_NAMED(name + "_add_gmp"          ); DO_BENCH(    c       .set_add(a       , b       );                                               )}
        { TRACK_CYCLES_NAMED(name + "_mul_avx512"       ); DO_BENCH(    d_avx512.set_mul(a_avx512, b_avx512);                                               )}
        { TRACK_CYCLES_NAMED(name + "_mul_avx512_gmp"   ); DO_BENCH(    a_avx512=a; b_avx512=b; d_avx512.set_mul(a_avx512, b_avx512); d_avx512.assign(d);   )}
        { TRACK_CYCLES_NAMED(name + "_mul_gmp"          ); DO_BENCH(    d       .set_mul(a       , b       );                                               )}
        { TRACK_CYCLES_NAMED(name + "_mul_to_gmp"       ); DO_BENCH(    d_avx512.assign(d);                                                                 )}
    }

    #undef DO_BENCH
}

/*template<class intnx, class avx512_intnx> void load_int(const char* num_bits, avx512_intnx& out) {
    int num_bits_int=from_string<int>(num_bits);
    integer test_int=rand_integer((num_bits_int<0)? -num_bits_int : num_bits_int);
    //integer test_int=integer(1)<<((num_bits_int<0)? -num_bits_int : num_bits_int);
    if (num_bits_int<0) {
        test_int=-test_int;
    }
    intnx test;
    mpz_set(test._(), test_int.impl);
    out=test;
} */

int main(int argc, char **argv) {
    assert(is_vdf_test); //assertions should be disabled in VDF_MODE==0
    init_gmp();
    set_rounding_mode();

    enable_avx512_ifma=false;

    if (argc==2 && string(argv[1]) == "avx512") {
        integer a_int=rand_integer(512);

        int1x a;
        mpz_set(a._(), a_int.impl);

        avx512_int1x a_avx512;

        while (1) { a_avx512=a; }
    }

    if (argc==2 && string(argv[1]) == "gmp") {
        while (1);
    }


    do_benchmark<int1x, avx512_int1x, int2x, avx512_int2x>( "benchmark", 512 );
    do_benchmark<int2x, avx512_int2x, int4x, avx512_int4x>( "benchmark", 1024 );

    TRACK_CYCLES_OUTPUT_STATS

    /*assert(argc>=3);

    avx512_int4x test_1_avx512;
    load_int<int4x>(argv[1], test_1_avx512);

    avx512_int4x test_2_avx512;
    load_int<int4x>(argv[2], test_2_avx512);

    avx512_int4x test_3_avx512;
    test_3_avx512.set_mul(test_1_avx512, test_2_avx512);

    for (int x=0;x<test_3_avx512.num_limbs;++x) {
        bitset<64> v(test_3_avx512.data()[x]);
        cout << dec << x << ": " << hex << v << "\n";
    }*/
}
