#ifndef ASM_MAIN_H
#define ASM_MAIN_H

#ifdef GENERATE_ASM_TRACKING_DATA
    #ifndef COMPILE_ASM
        extern "C" uint64 asm_tracking_data[num_asm_tracking_data];
        extern "C" char* asm_tracking_data_comments[num_asm_tracking_data];

        uint64 asm_tracking_data[num_asm_tracking_data];
        char* asm_tracking_data_comments[num_asm_tracking_data];
    #endif
#endif

namespace asm_code {


//all doubles are arrays with 2 entries. the high entry is first followed by the low entry
//so: b, a; u1, u0; v1, v0
//is_lehmer is all 1s or all 0s. ab_threshold is duplicated twice
extern "C" int asm_avx2_func_gcd_base(double* ab, double* u, double* v, uint64* is_lehmer, double* ab_threshold, uint64* no_progress);
extern "C" int asm_cel_func_gcd_base(double* ab, double* u, double* v, uint64* is_lehmer, double* ab_threshold, uint64* no_progress);
#ifdef COMPILE_ASM
void compile_asm_gcd_base() {
    EXPAND_MACROS_SCOPE;

    asm_function c_func( "gcd_base", 6 );
    reg_alloc regs=c_func.regs;

    reg_vector ab=regs.bind_vector(m, "ab");
    reg_vector u=regs.bind_vector(m, "u");
    reg_vector v=regs.bind_vector(m, "v");
    reg_vector is_lehmer=regs.bind_vector(m, "is_lehmer");
    reg_vector ab_threshold=regs.bind_vector(m, "ab_threshold");

    m.bind(c_func.args.at(0), "ab_addr");
    m.bind(c_func.args.at(1), "u_addr");
    m.bind(c_func.args.at(2), "v_addr");
    m.bind(c_func.args.at(3), "is_lehmer_addr");
    m.bind(c_func.args.at(4), "ab_threshold_addr");
    m.bind(c_func.args.at(5), "no_progress_addr");

    APPEND_M(str( "MOVDQU `ab, [`ab_addr]" ));
    APPEND_M(str( "MOVDQU `u, [`u_addr]" ));
    APPEND_M(str( "MOVDQU `v, [`v_addr]" ));
    APPEND_M(str( "MOVDQU `is_lehmer, [`is_lehmer_addr]" ));
    APPEND_M(str( "MOVDQU `ab_threshold, [`ab_threshold_addr]" ));

    string no_progress_label=m.alloc_label();
    string progress_label=m.alloc_label();
    string exit_label=m.alloc_label();

    gcd_base_continued_fraction(regs, ab, u, v, is_lehmer, ab_threshold, no_progress_label);
    APPEND_M(str( "JMP #", progress_label ));
    APPEND_M(str( "#:", no_progress_label ));

    APPEND_M(str( "MOV QWORD PTR [`no_progress_addr], 1" ));
    APPEND_M(str( "JMP #", exit_label ));

    APPEND_M(str( "#:", progress_label ));

    APPEND_M(str( "MOV QWORD PTR [`no_progress_addr], 0" ));

    APPEND_M(str( "#:", exit_label ));

    APPEND_M(str( "MOVDQU [`ab_addr], `ab" ));
    APPEND_M(str( "MOVDQU [`u_addr], `u" ));
    APPEND_M(str( "MOVDQU [`v_addr], `v" ));
    APPEND_M(str( "MOVDQU [`is_lehmer_addr], `is_lehmer" ));
    APPEND_M(str( "MOVDQU [`ab_threshold_addr], `ab_threshold" ));
}
#endif

//104 bytes
struct asm_func_gcd_128_data {
    //4
    uint64 ab_start_0_0;
    uint64 ab_start_0_8;
    uint64 ab_start_1_0;
    uint64 ab_start_1_8;

    //4
    uint64 u_0;
    uint64 u_1;
    uint64 v_0;
    uint64 v_1;

    //5
    uint64 parity; //1 if odd, else 0
    uint64 is_lehmer; //1 if true, else 0
    uint64 ab_threshold_0;
    uint64 ab_threshold_8;
    uint64 no_progress;
};

extern "C" int asm_avx2_func_gcd_128(asm_func_gcd_128_data* data);
extern "C" int asm_cel_func_gcd_128(asm_func_gcd_128_data* data);
#ifdef COMPILE_ASM
void compile_asm_gcd_128() {
    EXPAND_MACROS_SCOPE_PUBLIC;

    asm_function c_func( "gcd_128", 1 );
    reg_alloc regs_parent=c_func.regs;

    reg_spill spill_data_addr=regs_parent.bind_spill(m, "spill_data_addr");
    reg_spill spill_data=regs_parent.bind_spill(m, "spill_data", sizeof(asm_func_gcd_128_data), 8);

    assert(sizeof(asm_func_gcd_128_data)%8==0);

    {
        EXPAND_MACROS_SCOPE;
        reg_alloc regs=regs_parent;

        m.bind(c_func.args.at(0), "data_addr");

        reg_scalar tmp=regs.bind_scalar(m, "tmp");

        APPEND_M(str( "MOV `spill_data_addr, `data_addr" ));

        for (int x=0;x<sizeof(asm_func_gcd_128_data)/8;++x) {
            APPEND_M(str( "MOV `tmp, [`data_addr+#]", to_hex(x*8) ));
            APPEND_M(str( "MOV #, `tmp", (spill_data+8*x).name() ));
        }
    }

    regs_parent.add(c_func.args.at(0));
    c_func.args.clear();

    string no_progress_label=m.alloc_label();
    string progress_label=m.alloc_label();
    string exit_label=m.alloc_label();

    gcd_128(
        regs_parent,
        {spill_data, spill_data+16}, {spill_data+32, spill_data+40}, {spill_data+48, spill_data+56},
        spill_data+64, spill_data+72, spill_data+80, no_progress_label
    );

    {
        EXPAND_MACROS_SCOPE;
        reg_alloc regs=regs_parent;

        reg_scalar tmp=regs.bind_scalar(m, "tmp");
        reg_scalar data_addr=regs.bind_scalar(m, "data_addr");

        APPEND_M(str( "JMP #", progress_label ));
        APPEND_M(str( "#:", no_progress_label ));

        APPEND_M(str( "MOV `tmp, 1" ));
        APPEND_M(str( "JMP #", exit_label ));

        APPEND_M(str( "#:", progress_label ));

        APPEND_M(str( "MOV `tmp, 0" ));

        APPEND_M(str( "#:", exit_label ));

        APPEND_M(str( "MOV #, `tmp", (spill_data+96).name() ));

        APPEND_M(str( "MOV `data_addr, `spill_data_addr" ));

        for (int x=0;x<sizeof(asm_func_gcd_128_data)/8;++x) {
            APPEND_M(str( "MOV `tmp, #", (spill_data+8*x).name() ));
            APPEND_M(str( "MOV [`data_addr+#], `tmp", to_hex(x*8) ));
        }
    }
}
#endif

struct asm_func_gcd_unsigned_data {
    uint64* a;
    uint64* b;
    uint64* a_2;
    uint64* b_2;
    uint64* threshold;

    uint64 uv_counter_start;
    uint64* out_uv_counter_addr;
    uint64* out_uv_addr;
    int64 iter;
    uint64 a_end_index;
};

extern "C" int asm_avx2_func_gcd_unsigned(asm_func_gcd_unsigned_data* data);
extern "C" int asm_cel_func_gcd_unsigned(asm_func_gcd_unsigned_data* data);
#ifdef COMPILE_ASM
void compile_asm_gcd_unsigned() {
    EXPAND_MACROS_SCOPE_PUBLIC;

    const int int_size=gcd_size;
    const int max_iterations=gcd_max_iterations;

    asm_function c_func( "gcd_unsigned", 1 );
    reg_alloc regs_parent=c_func.regs;

    reg_spill spill_data_addr=regs_parent.bind_spill(m, "spill_data_addr");
    reg_spill spill_data=regs_parent.bind_spill(m, "spill_data", sizeof(asm_func_gcd_unsigned_data), 8);

    assert(sizeof(asm_func_gcd_unsigned_data)%8==0);

    {
        EXPAND_MACROS_SCOPE;
        reg_alloc regs=regs_parent;

        m.bind(c_func.args.at(0), "data_addr");

        reg_scalar tmp=regs.bind_scalar(m, "tmp");

        APPEND_M(str( "MOV `spill_data_addr, `data_addr" ));

        for (int x=0;x<sizeof(asm_func_gcd_unsigned_data)/8;++x) {
            APPEND_M(str( "MOV `tmp, [`data_addr+#]", to_hex(x*8) ));
            APPEND_M(str( "MOV #, `tmp", (spill_data+8*x).name() ));
        }
    }

    regs_parent.add(c_func.args.at(0));
    c_func.args.clear();

    gcd_unsigned(
        regs_parent,
        asm_integer(spill_data, int_size), asm_integer(spill_data+8, int_size),
        asm_integer(spill_data+16, int_size), asm_integer(spill_data+24, int_size), asm_integer(spill_data+32, int_size),
        spill_data+40, spill_data+48, spill_data+56,
        spill_data+64, spill_data+72, max_iterations
    );

    {
        EXPAND_MACROS_SCOPE;
        reg_alloc regs=regs_parent;

        reg_scalar tmp=regs.bind_scalar(m, "tmp");
        reg_scalar data_addr=regs.bind_scalar(m, "data_addr");

        APPEND_M(str( "MOV `data_addr, `spill_data_addr" ));

        for (int x=0;x<sizeof(asm_func_gcd_unsigned_data)/8;++x) {
            APPEND_M(str( "MOV `tmp, #", (spill_data+8*x).name() ));
            APPEND_M(str( "MOV [`data_addr+#], `tmp", to_hex(x*8) ));
        }
    }
}
#endif

//
//

template<int in_max_num_limbs, int out_num_limbs> uint64 asm_avx512_func_to_avx512_integer(
    uint64 in_sign, const uint64* in_data, uint64* out_data
);

#define declare_asm_avx512_func_to_avx512_integer(in_max_num_limbs, out_num_limbs)\
extern "C" uint64 asm_avx512_func_to_avx512_integer_ ## in_max_num_limbs ## _ ## out_num_limbs(\
    uint64 in_num_limbs, const uint64* in_data, uint64* out_data\
);\
template<> uint64 asm_avx512_func_to_avx512_integer<in_max_num_limbs, out_num_limbs>(\
    uint64 in_num_limbs, const uint64* in_data, uint64* out_data\
) {\
    return asm_avx512_func_to_avx512_integer_ ## in_max_num_limbs ## _ ## out_num_limbs(in_num_limbs, in_data, out_data);\
}

#define for_each_asm_avx512_func_to_avx512_integer(func)\
func( 9, 12);\
func(17, 21);\
func(25, 31);\
func(33, 41);

#ifndef COMPILE_ASM
for_each_asm_avx512_func_to_avx512_integer(declare_asm_avx512_func_to_avx512_integer)
#endif

#ifdef COMPILE_ASM
void compile_asm_avx512_to_avx512_integer(int in_max_num_limbs, int out_num_limbs) {
    EXPAND_MACROS_SCOPE;

    asm_function c_func(str( "to_avx512_integer_#_#", in_max_num_limbs, out_num_limbs ), 3, 9, false, false);
    reg_alloc regs=c_func.regs;

    avx512::gmp_integer in;
    in.num_limbs=c_func.args.at(0);
    in.max_num_limbs=in_max_num_limbs;
    in.data=c_func.args.at(1);

    avx512::avx512_integer out;
    out.num_limbs=out_num_limbs;
    out.sign=c_func.return_reg;
    out.data=c_func.args.at(2);

    avx512::to_avx512_integer(regs, in, out);
}
#endif

//
//

template<int in_num_limbs, int out_max_num_limbs> uint64 asm_avx512_func_to_gmp_integer(
    uint64 in_sign, const uint64* in_data, uint64* out_data
);

#define declare_asm_avx512_func_to_gmp_integer(in_num_limbs, out_max_num_limbs)\
extern "C" uint64 asm_avx512_func_to_gmp_integer_ ## in_num_limbs ## _ ## out_max_num_limbs(\
    uint64 in_sign, const uint64* in_data, uint64* out_data\
);\
template<> uint64 asm_avx512_func_to_gmp_integer<in_num_limbs, out_max_num_limbs>(\
    uint64 in_sign, const uint64* in_data, uint64* out_data\
) {\
    return asm_avx512_func_to_gmp_integer_ ## in_num_limbs ## _ ## out_max_num_limbs(in_sign, in_data, out_data);\
}

#define for_each_asm_avx512_func_to_gmp_integer(func)\
func(12,  9);\
func(21, 17);\
func(31, 25);\
func(41, 33);\
func(12, 33);\
func(21, 33);\
func(31, 33);

#ifndef COMPILE_ASM
for_each_asm_avx512_func_to_gmp_integer(declare_asm_avx512_func_to_gmp_integer)
#endif

#ifdef COMPILE_ASM
void compile_asm_avx512_to_gmp_integer(int in_num_limbs, int out_max_num_limbs) {
    EXPAND_MACROS_SCOPE;

    asm_function c_func(str( "to_gmp_integer_#_#", in_num_limbs, out_max_num_limbs ), 3, 9, false, false);
    reg_alloc regs=c_func.regs;

    avx512::avx512_integer in;
    in.num_limbs=in_num_limbs;
    in.sign=c_func.args.at(0);
    in.data=c_func.args.at(1);

    avx512::gmp_integer out;
    out.num_limbs=c_func.return_reg;
    out.max_num_limbs=out_max_num_limbs;
    out.data=c_func.args.at(2);

    avx512::to_gmp_integer(regs, in, out);
}
#endif

//
//

template<int in_a_num_limbs, int in_b_num_limbs, int out_num_limbs> uint64 asm_avx512_func_add(
    uint64 in_a_sign, const uint64* in_a_data, uint64 in_b_sign, const uint64* in_b_data, uint64* out_data
);

#define declare_asm_avx512_func_add(in_a_num_limbs, in_b_num_limbs, out_num_limbs)\
extern "C" uint64 asm_avx512_func_add_ ## in_a_num_limbs ## _ ## in_b_num_limbs ## _ ## out_num_limbs(\
    uint64 in_a_sign, const uint64* in_a_data, uint64 in_b_sign, const uint64* in_b_data, uint64* out_data\
);\
template<> uint64 asm_avx512_func_add<in_a_num_limbs, in_b_num_limbs, out_num_limbs>(\
    uint64 in_a_sign, const uint64* in_a_data, uint64 in_b_sign, const uint64* in_b_data, uint64* out_data\
) {\
    return asm_avx512_func_add_ ## in_a_num_limbs ## _ ## in_b_num_limbs ## _ ## out_num_limbs(\
        in_a_sign, in_a_data, in_b_sign, in_b_data, out_data\
    );\
}

#define for_each_asm_avx512_func_add(func)\
func(12, 12, 12);\
func(21, 21, 21);\
func(31, 31, 31);\
func(41, 41, 41);

#ifndef COMPILE_ASM
for_each_asm_avx512_func_add(declare_asm_avx512_func_add)
#endif

#ifdef COMPILE_ASM
void compile_asm_avx512_add(int in_a_num_limbs, int in_b_num_limbs, int out_num_limbs) {
    EXPAND_MACROS_SCOPE;

    asm_function c_func(str( "add_#_#_#", in_a_num_limbs, in_b_num_limbs, out_num_limbs ), 5, 9, false, false);
    reg_alloc regs=c_func.regs;

    avx512::avx512_integer in_a;
    in_a.num_limbs=in_a_num_limbs;
    in_a.sign=c_func.args.at(0);
    in_a.data=c_func.args.at(1);

    avx512::avx512_integer in_b;
    in_b.num_limbs=in_b_num_limbs;
    in_b.sign=c_func.args.at(2);
    in_b.data=c_func.args.at(3);

    avx512::avx512_integer out;
    out.num_limbs=out_num_limbs;
    out.sign=c_func.return_reg;
    out.data=c_func.args.at(4);

    vector<reg_vector> out_registers;

    for (int i=0;i<avx512::ceil_div(out_num_limbs, 8);++i) {
        out_registers.push_back(regs.get_vector(512));
    }

    avx512::add(regs, in_a, in_b, out, out_registers);
    avx512::apply_carry(regs, out, out_registers);
}
#endif

//
//

template<int in_a_num_limbs, int in_b_num_limbs, int out_num_limbs> uint64 asm_avx512_func_multiply(
    uint64 in_a_sign, const uint64* in_a_data, uint64 in_b_sign, const uint64* in_b_data, uint64* out_data
);

#define declare_asm_avx512_func_multiply(in_a_num_limbs, in_b_num_limbs, out_num_limbs)\
extern "C" uint64 asm_avx512_func_multiply_ ## in_a_num_limbs ## _ ## in_b_num_limbs ## _ ## out_num_limbs(\
    uint64 in_a_sign, const uint64* in_a_data, uint64 in_b_sign, const uint64* in_b_data, uint64* out_data\
);\
template<> uint64 asm_avx512_func_multiply<in_a_num_limbs, in_b_num_limbs, out_num_limbs>(\
    uint64 in_a_sign, const uint64* in_a_data, uint64 in_b_sign, const uint64* in_b_data, uint64* out_data\
) {\
    return asm_avx512_func_multiply_ ## in_a_num_limbs ## _ ## in_b_num_limbs ## _ ## out_num_limbs(\
        in_a_sign, in_a_data, in_b_sign, in_b_data, out_data\
    );\
}

#define for_each_asm_avx512_func_multiply(func)\
func(12, 12, 12);\
func(12, 12, 21);\
func(12, 21, 21);\
func(12, 21, 31);\
func(21, 12, 21);\
func(21, 21, 21);\
func(21, 21, 31);\
func(21, 21, 41);\
func(31, 31, 31);\
func(41, 12, 31);\
func(41, 41, 41);\
//

#ifndef COMPILE_ASM
for_each_asm_avx512_func_multiply(declare_asm_avx512_func_multiply)
#endif

#ifdef COMPILE_ASM
void compile_asm_avx512_multiply(int in_a_num_limbs, int in_b_num_limbs, int out_num_limbs) {
    EXPAND_MACROS_SCOPE;

    asm_function c_func(str( "multiply_#_#_#", in_a_num_limbs, in_b_num_limbs, out_num_limbs ), 5, 9, false, false);
    reg_alloc regs=c_func.regs;

    avx512::avx512_integer in_a;
    in_a.num_limbs=in_a_num_limbs;
    in_a.sign=c_func.args.at(0);
    in_a.data=c_func.args.at(1);

    avx512::avx512_integer in_b;
    in_b.num_limbs=in_b_num_limbs;
    in_b.sign=c_func.args.at(2);
    in_b.data=c_func.args.at(3);

    avx512::avx512_integer out;
    out.num_limbs=out_num_limbs;
    out.sign=c_func.return_reg;
    out.data=c_func.args.at(4);

    vector<reg_vector> out_registers;

    for (int i=0;i<avx512::ceil_div(out_num_limbs, 8);++i) {
        out_registers.push_back(regs.get_vector(512));
    }

    avx512::multiply(regs, in_a, in_b, out, out_registers);
    avx512::apply_carry(regs, out, out_registers);
}
#endif

//
//

#ifdef COMPILE_ASM
void compile_asm(std::string filename) {
    compile_asm_gcd_base();
    compile_asm_gcd_128();
    compile_asm_gcd_unsigned();

    ofstream out( filename );
    out << m.format_res_text();
}

void compile_asm_avx512(std::string filename) {
    for_each_asm_avx512_func_to_avx512_integer(compile_asm_avx512_to_avx512_integer)
    for_each_asm_avx512_func_to_gmp_integer(compile_asm_avx512_to_gmp_integer)
    for_each_asm_avx512_func_add(compile_asm_avx512_add)
    for_each_asm_avx512_func_multiply(compile_asm_avx512_multiply)

    ofstream out( filename );
    out << m.format_res_text();
}
#endif


}

// end Headerguard ASM_MAIN_H
#endif
