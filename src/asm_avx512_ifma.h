namespace asm_code { namespace avx512 {


//GMP integers:
//-the number of limbs is passed with a register. the sign is the sign of the integer. "0" has zero limbs
//-the limbs are stored little endian in an array which is 64-aligned and has enough memory for the max allowed number of limbs
//-any unused values in the array are 0

//AVX-512 integers:
//-the number of limbs is fixed, but doesn't have to be a multiple of the AVX vector size
//-the sign is stored in a register (0 is positive, -1 is negative)
//-the data is stored in memory in little endian order. each limb has 52 bits and any unused bits are 0
//--any unused limbs are 0 and there are 64 zero padding bytes before the start and after the end of the data
//--the start padding is before the data pointer
//-the data can also be stored in registers instead. any unused limbs are 0

int ceil_div(int a, int b) {
    return (a+b-1)/b;
}

struct gmp_integer {
    reg_scalar num_limbs;
    int max_num_limbs=-1; //will write past the end of this if it isn't a multiple of 8

    reg_scalar data; //64-aligned
};

struct avx512_integer {
    int num_limbs=-1;
    reg_scalar sign; //-1 if negative, 0 if positive

    reg_scalar data; //64-aligned; padding before and after

    //this handles out of range indexes by returning 0 for those values
    int get_memory_offset(int index) {
        //there are 8 zero padding values before the first value
        if (index<-8) {
            index=-8;
        }

        //there are also at least 8 zero padding values after the last limb
        if (index>num_limbs) {
            index=num_limbs;
        }

        return index*8;
    }
};

//the results of each shift are or'ed together
struct shift_command {
    //these are indexes into the inputs array
    //any out of range values will read zeros
    vector<int> source_indexes;

    //nonnegative
    vector<int> shift_amounts;

    //this is the same for all 8 commands for each index
    vector<bool> is_right_shift;
};

void apply_shifts(
    reg_alloc regs, vector<reg_vector> inputs, reg_vector output, array<shift_command, 8> commands
) {
    EXPAND_MACROS_SCOPE;

    m.bind(inputs, "inputs");
    m.bind(output, "output");

    reg_vector buffer_0=regs.bind_vector(m, "buffer_0", 512);
    reg_vector buffer_1=regs.bind_vector(m, "buffer_1", 512);

    assert(commands[0].source_indexes.size()>=1);
    assert(commands[0].source_indexes.size()==commands[0].shift_amounts.size());
    assert(commands[0].source_indexes.size()==commands[0].is_right_shift.size());

    bool is_first=true;

    for (int shift_index=0;shift_index<commands[0].source_indexes.size();++shift_index) {
        //each input vector is permutated and the results are blended together
        //the final result has the correct values at each slot, except that unused values are uninitialized
        //the result is then shifted and accumulated into the output with a bitwise or
        //any unused values are shifted by 64 to zero them out

        vector<int> source_inputs;
        vector<array<int, 8>> source_offsets; //-1 if not used

        bool is_right_shift=false;
        array<uint64, 8> shift_amounts;

        //
        //

        //any unused values are already processed and will be uninitialized after blending
        array<bool, 8> processed;
        for (int x=0;x<8;++x) {
            int c_source_index=commands[x].source_indexes.at(shift_index);
            int c_shift_amount=commands[x].shift_amounts.at(shift_index);
            bool c_is_right_shift=commands[x].is_right_shift.at(shift_index);

            assert(c_shift_amount>=0);
            processed[x]=(c_source_index<0 || c_source_index>=inputs.size()*8 || c_shift_amount>=64);

            //any unused inputs will be uninitialized after blending. this will make them have a value of 0
            shift_amounts[x]=(processed[x])? 64 : c_shift_amount;

            assert(x==0 || is_right_shift==c_is_right_shift);
            is_right_shift=c_is_right_shift;
        }

        //the indexes need to be grouped by input vector and an offset needs to be calculated for each permutate operation
        while (true) {
            int input_index=-1;
            for (int x=0;x<8;++x) {
                int c_source_index=commands[x].source_indexes.at(shift_index);
                if (!processed[x]) {
                    input_index=c_source_index/8;
                    break;
                }
            }

            if (input_index==-1) {
                break;
            }

            array<int, 8> c_offsets;

            for (int x=0;x<8;++x) {
                int c_source_index=commands[x].source_indexes.at(shift_index);

                int c_offset=-1;
                if (!processed[x] && c_source_index/8==input_index) {
                    c_offset=c_source_index%8;
                    processed[x]=true;
                }

                c_offsets[x]=c_offset;
            }

            source_inputs.push_back(input_index);
            source_offsets.push_back(c_offsets);
        }

        if (source_inputs.empty()) {
            continue;
        }

        //the VPERMI2Q instruction requires at least two inputs, so a second redundant input is added if necessary
        if (source_inputs.size()==1) {
            source_inputs.push_back(source_inputs.at(0));

            array<int, 8> c_offsets;
            for (int x=0;x<8;++x) {
                c_offsets[x]=-1;
            }

            source_offsets.push_back(c_offsets);
        }

        //this is optimized for 2 or 3 source_inputs
        //the first two inputs are blended together into an intermediate buffer
        {
            array<uint64, 8> perm_indexes;
            for (int x=0;x<8;++x) {
                int offset_0=source_offsets.at(0)[x];
                int offset_1=source_offsets.at(1)[x];

                assert(offset_0==-1 || offset_1==-1);
                if (offset_0==-1 && offset_1==-1) {
                    perm_indexes[x]=0; //uninitialized
                } else {
                    perm_indexes[x]=(offset_0==-1)? offset_1+8 : offset_0;
                }
            }

            APPEND_M(str( "VMOVDQU64 `buffer_0, #", constant_address_avx512_uint64(perm_indexes) ));
            APPEND_M(str( "VPERMI2Q `buffer_0, `inputs_#, `inputs_#", source_inputs.at(0), source_inputs.at(1) ));
        }

        int current_buffer_index=0;

        //if necessary, additional inputs are blended
        //each iteration will change which of the 2 buffers stores the final result
        for (int source_offset_index=2;source_offset_index<source_offsets.size();++source_offset_index) {
            array<uint64, 8> perm_indexes;
            for (int x=0;x<8;++x) {
                int offset=source_offsets.at(source_offset_index)[x];

                if (offset==-1) {
                    perm_indexes[x]=x; //previous value
                } else {
                    perm_indexes[x]=offset+8;
                }
            }

            int next_buffer_index=1-current_buffer_index;

            APPEND_M(str( "VMOVDQU64 `buffer_#, #", next_buffer_index, constant_address_avx512_uint64(perm_indexes) ));
            APPEND_M(str(
                "VPERMI2Q `buffer_#, `buffer_#, `inputs_#", next_buffer_index, current_buffer_index, source_inputs.at(source_offset_index)
            ));
        }

        //finally calculate the shifts and do a bitwise or with the output (on subsequent iterations)
        APPEND_M(str(
            "# #, `buffer_#, #",
            (is_right_shift)? "VPSRLVQ" : "VPSLLVQ",
            (is_first)? "`output" : "`buffer_0",
            current_buffer_index,
            constant_address_avx512_uint64(shift_amounts)
        ));

        if (!is_first) {
            APPEND_M(str( "VPORQ `output, `output, `buffer_0" ));
        }

        is_first=false;
    }

    if (is_first) {
        APPEND_M(str( "VPXORQ `output, `output, `output" ));
    }
}

void to_avx512_integer(
    reg_alloc regs, gmp_integer in, avx512_integer out
) {
    EXPAND_MACROS_SCOPE;

    m.bind(in.num_limbs, "in_num_limbs");
    m.bind(in.data, "in_data");
    m.bind(out.sign, "out_sign");
    m.bind(out.data, "out_data");

    int in_num_vectors=ceil_div(in.max_num_limbs, 8);
    int out_num_vectors=ceil_div(out.num_limbs, 8);

    vector<reg_vector> input_registers;

    for (int input_index=0;input_index<in_num_vectors;++input_index) {
        input_registers.push_back(regs.get_vector(512));
    }

    m.bind(input_registers, "input_registers");

    reg_vector temp_vector=regs.bind_vector(m, "temp_vector", 512);
    reg_vector mask_vector=regs.bind_vector(m, "mask_vector", 512);

    //some of the gmp integer limbs are uninitialized and need to be zero-filled

    //in_num_limbs_vector=abs(in_num_limbs)
    reg_vector in_num_limbs_vector=regs.bind_vector(m, "in_num_limbs_vector", 512);
    APPEND_M(str( "VPBROADCASTQ `in_num_limbs_vector, `in_num_limbs" ));
    APPEND_M(str( "VPABSQ `in_num_limbs_vector, `in_num_limbs_vector" ));

    //out_sign=in_num_limbs>>63 (arithmetic shift)
    APPEND_M(str( "MOV `out_sign, `in_num_limbs" ));
    APPEND_M(str( "SAR `out_sign, 63" ));

    for (int input_index=0;input_index<in_num_vectors;++input_index) {
        array<uint64, 8> input_limb_index;
        for (int x=0;x<8;++x) {
            input_limb_index[x]=8*input_index+x;
        }

        //k1 true if the limb is valid (abs(in_num_limbs) > index)
        APPEND_M(str( "VPCMPUQ k1, `in_num_limbs_vector, #, 6", constant_address_avx512_uint64(input_limb_index) )); // 6: >

        //load the limb values and zero out any invalid limbs
        APPEND_M(str( "VMOVDQU64 `input_registers_# {k1}{z}, [`in_data+#]", input_index, input_index*64 ));
    }

    //zero padding at the start and end of the output
    APPEND_M(str( "VPXORQ `temp_vector, `temp_vector, `temp_vector" ));
    APPEND_M(str( "VMOVDQU64 [`out_data-64], `temp_vector" ));
    APPEND_M(str( "VMOVDQU64 [`out_data+#], `temp_vector", out_num_vectors*64 ));

    APPEND_M(str( "VMOVDQU64 `mask_vector, #", constant_address_avx512_uint64((1ull<<52)-1) ));

    for (int output_index=0;output_index<ceil_div(out.num_limbs, 8);++output_index) {
        array<shift_command, 8> commands;

        for (int output_offset=0;output_offset<8;++output_offset) {
            int output_limb=output_index*8+output_offset;
            int start_bit=output_limb*52;

            int start_limb=start_bit/64;

            //need to discard some of the lower bits of start_limb
            commands[output_offset].source_indexes.push_back(start_limb);
            commands[output_offset].shift_amounts.push_back(start_bit-start_limb*64);
            commands[output_offset].is_right_shift.push_back(true);

            //the bits in the next limb are used to assign the high bits that were lost
            commands[output_offset].source_indexes.push_back(start_limb+1);
            commands[output_offset].shift_amounts.push_back(64-(start_bit-start_limb*64));
            commands[output_offset].is_right_shift.push_back(false);
        }

        apply_shifts(regs, input_registers, temp_vector, commands);
        APPEND_M(str( "VPANDQ `temp_vector, `temp_vector, `mask_vector" ));
        APPEND_M(str( "VMOVDQU64 [`out_data+#], `temp_vector", output_index*64 ));
    }
}

//this requires the upper 12 bits in each limb to be 0
void to_gmp_integer(
    reg_alloc regs, avx512_integer in, gmp_integer out
) {
    EXPAND_MACROS_SCOPE;

    m.bind(in.sign, "in_sign");
    m.bind(in.data, "in_data");
    m.bind(out.num_limbs, "out_num_limbs");
    m.bind(out.data, "out_data");

    int in_num_vectors=ceil_div(in.num_limbs, 8);
    int out_num_vectors=ceil_div(out.max_num_limbs, 8);

    vector<reg_vector> input_registers;

    for (int input_index=0;input_index<in_num_vectors;++input_index) {
        input_registers.push_back(regs.get_vector(512));
    }

    m.bind(input_registers, "input_registers");

    reg_vector temp_vector=regs.bind_vector(m, "temp_vector", 512);
    reg_vector zero_vector=regs.bind_vector(m, "zero_vector", 512);
    reg_vector num_limbs_vector=regs.bind_vector(m, "num_limbs_vector", 512);

    reg_scalar temp_scalar=regs.bind_scalar(m, "temp_scalar");

    for (int input_index=0;input_index<in_num_vectors;++input_index) {
        APPEND_M(str( "VMOVDQU64 `input_registers_#, [`in_data+#]", input_index, input_index*64 ));
    }

    APPEND_M(str( "VPXORQ `zero_vector, `zero_vector, `zero_vector" ));
    APPEND_M(str( "VPXORQ `num_limbs_vector, `num_limbs_vector, `num_limbs_vector" ));

    for (int output_index=0;output_index<ceil_div(out.max_num_limbs, 8);++output_index) {
        array<shift_command, 8> commands;

        for (int output_offset=0;output_offset<8;++output_offset) {
            int output_limb=output_index*8+output_offset;
            int start_bit=output_limb*64;

            int start_limb=start_bit/52;

            //need to discard some of the lower bits of start_limb
            commands[output_offset].source_indexes.push_back(start_limb);
            commands[output_offset].shift_amounts.push_back(start_bit-start_limb*52);
            commands[output_offset].is_right_shift.push_back(true);

            //the bits in the next limb are used to assign the high bits that were lost
            commands[output_offset].source_indexes.push_back(start_limb+1);
            commands[output_offset].shift_amounts.push_back(52-(start_bit-start_limb*52));
            commands[output_offset].is_right_shift.push_back(false);

            //there might still be missing high bits so an additional limb might be required
            commands[output_offset].source_indexes.push_back(start_limb+2);
            commands[output_offset].shift_amounts.push_back(52*2-(start_bit-start_limb*52));
            commands[output_offset].is_right_shift.push_back(false);
        }

        apply_shifts(regs, input_registers, temp_vector, commands);
        APPEND_M(str( "VMOVDQU64 [`out_data+#], `temp_vector", output_index*64 ));

        //k1 true if the limb is nonzero
        APPEND_M(str( "VPCMPUQ k1, `temp_vector, `zero_vector, 4" )); //4: !=

        array<uint64, 8> output_num_limbs;
        for (int x=0;x<8;++x) {
            output_num_limbs[x]=8*output_index+x+1;
        }

        //replace each nonzero limb with its index + 1. zero limbs will remain 0
        APPEND_M(str( "VMOVDQU64 `temp_vector {k1}{z}, #", constant_address_avx512_uint64(output_num_limbs) ));

        //the max of num_limbs_vector is the number of limbs
        APPEND_M(str( "VPMAXUQ `num_limbs_vector, `num_limbs_vector, `temp_vector" ));
    }

    //butterfly reduction
    auto reduction_step=[&](array<uint64, 8> indexes) {
        APPEND_M(str( "VMOVDQU64 `temp_vector, #", constant_address_avx512_uint64(indexes) ));
        APPEND_M(str( "VPERMQ `temp_vector, `temp_vector, `num_limbs_vector" ));
        APPEND_M(str( "VPMAXUQ `num_limbs_vector, `num_limbs_vector, `temp_vector" ));
    };
    reduction_step(array<uint64, 8>{1, 0, 3, 2, 5, 4, 7, 6});
    reduction_step(array<uint64, 8>{2, 2, 0, 0, 6, 6, 4, 4});
    reduction_step(array<uint64, 8>{4, 4, 4, 4, 0, 0, 0, 0});

    APPEND_M(str( "VMOVQ `out_num_limbs, `num_limbs_vector_128" ));

    APPEND_M(str( "MOV `temp_scalar, `out_num_limbs" ));
    APPEND_M(str( "NEG `temp_scalar" ));
    APPEND_M(str( "TEST `in_sign, `in_sign" )); //ZF set if in_sign is 0. need to negate out_num_limbs if ZF is cleared
    APPEND_M(str( "CMOVNZ `out_num_limbs, `temp_scalar" ));
}

//each limb can have signed 53 bit values added to or subtracted from it (i.e one sign bit and 52 data bits)
//there are 11 spare data bits so this can be done about 2048 times without overflow
//this also stores the result
void apply_carry(
    reg_alloc regs, avx512_integer in, vector<reg_vector> in_registers
) {
    EXPAND_MACROS_SCOPE;

    m.bind(in.sign, "in_sign");
    m.bind(in.data, "in_data");
    m.bind(in_registers, "in_registers");

    reg_vector carry_mask=regs.bind_vector(m, "carry_mask", 512);
    reg_vector permutate_indexes=regs.bind_vector(m, "permutate_indexes", 512);
    reg_vector zero_vector=regs.bind_vector(m, "zero_vector", 512);

    reg_vector accumulator=regs.bind_vector(m, "accumulator", 512);
    reg_vector temp_vector=regs.bind_vector(m, "temp_vector", 512);

    reg_scalar temp_scalar=regs.bind_scalar(m, "temp_scalar");

    APPEND_M(str( "VMOVDQU64 `carry_mask, #", constant_address_avx512_uint64(~((1ull<<52)-1)) ));
    APPEND_M(str( "VMOVDQU64 `permutate_indexes, #", constant_address_avx512_uint64({7, 8, 9, 10, 11, 12, 13, 14}) ));
    APPEND_M(str( "VPXORQ `zero_vector, `zero_vector, `zero_vector" ));

    string label_name=m.alloc_label();
    APPEND_M(str( "#:", label_name ));

    //need to go msb first since carries are cleared at the same time the output is written
    for (int i=in_registers.size()-1;i>=0;--i) {
        //for each limb, find the next lowest limb (0 for the first limb)
        string low_register_name=(i==0)? "`zero_vector" : str( "`in_registers_#", i-1 );
        APPEND_M(str( "VMOVDQU64 `temp_vector, `permutate_indexes" ));
        APPEND_M(str( "VPERMI2Q `temp_vector, #, `in_registers_#", low_register_name, i ));

        //calculate the incoming carry (signed)
        APPEND_M(str( "VPSRAQ `temp_vector, `temp_vector, #", 52 ));

        //zero out the carry bits since they have all been consumed
        APPEND_M(str( "VPANDNQ `in_registers_#, `carry_mask, `in_registers_#", i, i ));

        //add the incoming carry to each limb. the limb has already been consumed as an input
        APPEND_M(str( "VPADDQ `in_registers_#, `in_registers_#, `temp_vector", i, i ));

        //store the result
        APPEND_M(str( "VMOVDQU64 [`in_data+#], `in_registers_#", i*64, i ));

        //the accumulator is the bitwise-or of all of the output vectors
        if (i==in_registers.size()-1) {
            APPEND_M(str( "VMOVDQA64 `accumulator, `in_registers_#", i ));
        } else {
            APPEND_M(str( "VPORQ `accumulator, `accumulator, `in_registers_#", i ));
        }
    }

    //zero padding at the start and end of the output
    APPEND_M(str( "VMOVDQU64 [`in_data-64], `zero_vector" ));
    APPEND_M(str( "VMOVDQU64 [`in_data+#], `zero_vector", in_registers.size()*64 ));

    //loop if any carry bit is nonzero
    APPEND_M(str( "VPTESTMQ k1, `accumulator, `carry_mask" )); //k1 true if any of the carry bits are nonzero
    APPEND_M(str( "KMOVQ `temp_scalar, k1" ));
    APPEND_M(str( "TEST `temp_scalar, `temp_scalar" ));
    APPEND_M(str( "JNZ #", label_name ));
}

//need to call apply_carry on the result
void add(reg_alloc regs, avx512_integer in_a, avx512_integer in_b, avx512_integer out, vector<reg_vector> out_registers) {
    EXPAND_MACROS_SCOPE;

    if (in_a.num_limbs<in_b.num_limbs) {
        swap(in_a, in_b);
    }

    int num_a_vectors=ceil_div(in_a.num_limbs, 8);
    int num_b_vectors=ceil_div(in_b.num_limbs, 8);
    int num_out_vectors=ceil_div(out.num_limbs, 8);

    assert(out_registers.size()==num_out_vectors);

    m.bind(in_a.sign, "in_a_sign");
    m.bind(in_a.data, "in_a_data");
    m.bind(in_b.sign, "in_b_sign");
    m.bind(in_b.data, "in_b_data");
    m.bind(out.sign, "out_sign");
    m.bind(out.data, "out_data");
    m.bind(out_registers, "out_registers");

    reg_scalar temp_0=regs.bind_scalar(m, "temp_0");
    reg_scalar temp_1=regs.bind_scalar(m, "temp_1");

    reg_vector A=regs.bind_vector(m, "A", 512);
    reg_vector B=regs.bind_vector(m, "B", 512);
    reg_vector C=regs.bind_vector(m, "C", 512);
    reg_vector temp_vector=regs.bind_vector(m, "temp_vector", 512);

    vector<reg_vector> a_registers;

    for (int i=0;i<num_a_vectors;++i) {
        a_registers.push_back((i<out_registers.size())? out_registers.at(i) : regs.get_vector(512));
    }

    m.bind(a_registers, "a_registers");

    vector<reg_vector> b_registers;

    for (int i=0;i<num_b_vectors;++i) {
        b_registers.push_back(regs.get_vector(512));
    }

    m.bind(b_registers, "b_registers");

    m.bind(C, "pivot_index_vector");

    //assign out_sign, A, B, C, a_registers, and b_registers
    //will calculate one of a+b, a-b, or b-a by calculating (a xor A)+(b xor B)+C
    {
        //this will contain the highest index where the values of a and b are different (0 if they are the same everywhere)
        APPEND_M(str( "VPXORQ `pivot_index_vector, `pivot_index_vector, `pivot_index_vector" ));

        bool assigned_zero=false;

        //`B is zero during this loop
        for (int i=0;i<ceil_div(max(in_a.num_limbs, in_b.num_limbs), 8);++i) {
            //load "a" and "b"
            string a_reg=(i<in_a.num_limbs)? str( "`a_registers_#", i ) : "`B";
            string b_reg=(i<in_b.num_limbs)? str( "`b_registers_#", i ) : "`B";

            if (i<in_a.num_limbs) {
                APPEND_M(str( "VMOVDQU64 #, [`in_a_data+#]", a_reg, in_a.get_memory_offset(i*8) ));
            } else {
                if (!assigned_zero) {
                    APPEND_M(str( "VPXORQ #, #, #", a_reg, a_reg, a_reg ));
                    assigned_zero=true;
                }
            }

            if (i<in_b.num_limbs) {
                APPEND_M(str( "VMOVDQU64 #, [`in_b_data+#]", b_reg, in_b.get_memory_offset(i*8) ));
            } else {
                if (!assigned_zero) {
                    APPEND_M(str( "VPXORQ #, #, #", b_reg, b_reg, b_reg ));
                    assigned_zero=true;
                }
            }

            //k1 true if the values for "a" and "b" are different
            APPEND_M(str( "VPCMPUQ k1, #, #, 4", a_reg, b_reg )); //4: !=

            array<uint64, 8> output_num_limbs;
            for (int x=0;x<8;++x) {
                output_num_limbs[x]=8*i+x;
            }

            //replace each limb where "a" and "b" are different with its index. if "a" and "b" are the same, 0 is used instead
            APPEND_M(str( "VMOVDQU64 `A {k1}{z}, #", constant_address_avx512_uint64(output_num_limbs) ));

            //accumulate the max of the results, which is the highest index where "a" and "b" are different
            APPEND_M(str( "VPMAXUQ `pivot_index_vector, `pivot_index_vector, `A" ));
        }

        //butterfly reduction
        auto reduction_step=[&](array<uint64, 8> indexes) {
            APPEND_M(str( "VMOVDQU64 `A, #", constant_address_avx512_uint64(indexes) ));
            APPEND_M(str( "VPERMQ `A, `A, `pivot_index_vector" ));
            APPEND_M(str( "VPMAXUQ `pivot_index_vector, `pivot_index_vector, `A" ));
        };
        reduction_step(array<uint64, 8>{1, 0, 3, 2, 5, 4, 7, 6});
        reduction_step(array<uint64, 8>{2, 2, 0, 0, 6, 6, 4, 4});
        reduction_step(array<uint64, 8>{4, 4, 4, 4, 0, 0, 0, 0});

        //"a" and "b" can be compared by comparing a[pivot_index] and b[pivot_index] only
        APPEND_M(str( "VMOVQ `temp_0, `pivot_index_vector_128" ));
        APPEND_M(str( "MOV `temp_1, [`in_a_data+`temp_0*8]" ));
        APPEND_M(str( "CMP `temp_1, [`in_b_data+`temp_0*8]" ));

        //the final sign is S and the final data is a^A+b^B+C where ^ is bitwise-xor
        //there is one lookup table with 8 entries
        //the index is a_sign*2+b_sign+3+(|a|>=|b|)? 4 : 0
        //each table entry is 4 64-bit scalars (S, A, B, C). vector loads use a broadcast. each load is register+offset
        //|a|<|b|:
        //-a, -b: S=-, A= 0, B= 0, C=0
        //-a, +b: S=+, A=~0, B= 0, C=1
        //+a, -b: S=-, A=~0, B= 0, C=1
        //+a, +b: S=+, A= 0, B= 0, C=0
        //|a|>=|b|:
        //-a, -b: S=-, A= 0, B= 0, C=0
        //-a, +b: S=-, A= 0, B=~0, C=1
        //+a, -b: S=+, A= 0, B=~0, C=1
        //+a, +b: S=+, A= 0, B= 0, C=0

        static bool outputted_table=false;
        if (!outputted_table) {
            #ifdef CHIAOSX
                APPEND_M(str( ".text " ));
            #else
                APPEND_M(str( ".text 1" ));
            #endif

            string neg_1=to_hex(~uint64(0));

            APPEND_M(str( ".balign 8" ));
            APPEND_M(str( "avx512_add_table:" ));
            APPEND_M(str( ".quad #, 0, 0, 0", neg_1 ));
            APPEND_M(str( ".quad 0, #, 0, 1", neg_1 ));
            APPEND_M(str( ".quad #, #, 0, 1", neg_1, neg_1 ));
            APPEND_M(str( ".quad 0, 0, 0, 0" ));
            APPEND_M(str( ".quad #, 0, 0, 0", neg_1 ));
            APPEND_M(str( ".quad #, 0, #, 1", neg_1, neg_1 ));
            APPEND_M(str( ".quad 0, 0, #, 1", neg_1 ));
            APPEND_M(str( ".quad 0, 0, 0, 0" ));
            APPEND_M(str( ".text" ));

            outputted_table=true;
        }

        //temp_0 has the table index
        APPEND_M(str( "LEA `temp_0, [`in_a_sign*2+`in_b_sign+3]" ));
        APPEND_M(str( "LEA `temp_1, [`temp_0+4]" ));
        APPEND_M(str( "CMOVAE `temp_0, `temp_1" )); //compares a[pivot_index] with b[pivot_index]

        //temp_1 has the address of the table entry
        APPEND_M(str( "SHL `temp_0, 5" )); //multiply by 32 to convert the index to a byte offset
        #ifdef CHIAOSX
            APPEND_M(str( "LEA `temp_1, [RIP+avx512_add_table]" )); //base of the table
            APPEND_M(str( "ADD `temp_1, `temp_0")); //address of the table entry
        #else
            APPEND_M(str( "LEA `temp_1, [avx512_add_table+`temp_0]" ));
        #endif

        APPEND_M(str( "MOV `out_sign, [`temp_1]" ));
        APPEND_M(str( "VPBROADCASTQ `A, [`temp_1+8]" ));
        APPEND_M(str( "VPBROADCASTQ `B, [`temp_1+16]" ));
        APPEND_M(str( "VPBROADCASTQ `C, [`temp_1+24]" ));
    }

    for (int i=0;i<num_out_vectors;++i) {
        //a^A + b^B + C
        if (i>=num_a_vectors) {
            //a and b are 0 so a-b, b-a, and a+b are all 0
            //(num_a_vectors>=num_b_vectors)
            APPEND_M(str( "VPXORQ `out_registers_#, `out_registers_#, `out_registers_#", i, i, i ));
        } else
        if (i>=num_b_vectors) {
            //b=0, so the result is a or -a
            //the result can't be -a because the final result must be nonnegative
            APPEND_M(str( "VPMOVQ `out_registers_#, `out_registers_#, `out_registers_#", i, i, i ));
        } else {
            APPEND_M(str( "VPXORQ `temp_vector, `a_registers_#, `A", i )); //temp_vector=a^A
            APPEND_M(str( "VPADDQ `out_registers_#, `temp_vector, `C", i )); //out=a^A+C ; this also clobbers "a"
            APPEND_M(str( "VPXORQ `temp_vector, `b_registers_#, `B", i )); //temp_vector=b^B
            APPEND_M(str( "VPADDQ `out_registers_#, `out_registers_#, `temp_vector", i, i )); //out=a^A+b^B+C
        }
    }
}

//need to call apply_carry on the result (stored in out_registers_low)
void multiply(reg_alloc regs, avx512_integer in_a, avx512_integer in_b, avx512_integer out, vector<reg_vector> out_registers_low) {
    EXPAND_MACROS_SCOPE;

    int num_a_vectors=ceil_div(in_a.num_limbs, 8);
    int num_b_vectors=ceil_div(in_b.num_limbs, 8);
    int num_out_vectors=ceil_div(out.num_limbs, 8);

    assert(out_registers_low.size()==num_out_vectors);

    m.bind(in_a.sign, "in_a_sign");
    m.bind(in_a.data, "in_a_data");
    m.bind(in_b.sign, "in_b_sign");
    m.bind(in_b.data, "in_b_data");
    m.bind(out.sign, "out_sign");
    m.bind(out.data, "out_data");
    m.bind(out_registers_low, "out_registers_low");

    //out_sign is the xor of the two input signs
    APPEND_M(str( "MOV `out_sign, `in_a_sign" ));
    APPEND_M(str( "XOR `out_sign, `in_b_sign" ));

    vector<reg_vector> out_registers_high;

    for (int i=0;i<num_out_vectors;++i) {
        out_registers_high.push_back(regs.get_vector(512));
    }

    m.bind(out_registers_high, "out_registers_high");

    for (int i=0;i<num_out_vectors;++i) {
        APPEND_M(str( "VPXORQ `out_registers_low_#, `out_registers_low_#, `out_registers_low_#", i, i, i ));
        APPEND_M(str( "VPXORQ `out_registers_high_#, `out_registers_high_#, `out_registers_high_#", i, i, i ));
    }

    reg_vector a=regs.bind_vector(m, "a", 512);
    reg_vector b=regs.bind_vector(m, "b", 512);
    reg_vector zero_vector=regs.bind_vector(m, "zero_vector", 512);

    for (int a_index=0;a_index<in_a.num_limbs;++a_index) {
        APPEND_M(str( "VPBROADCASTQ `a, [`in_a_data+#]", a_index*8 ));

        for (int out_index=0;out_index<out.num_limbs;out_index+=8) {
            int b_index=out_index-a_index;

            if (b_index>=in_b.num_limbs || b_index<=-8) {
                continue;
            }

            APPEND_M(str( "VMOVDQU64 `b, [`in_b_data+#]", b_index*8 ));

            APPEND_M(str( "VPMADD52LUQ `out_registers_low_#, `a, `b", out_index/8 ));
            APPEND_M(str( "VPMADD52HUQ `out_registers_high_#, `a, `b", out_index/8 ));
        }
    }

    APPEND_M(str( "VMOVDQU64 `b, #", constant_address_avx512_uint64({7, 8, 9, 10, 11, 12, 13, 14}) )); //b = permutate_indexes
    APPEND_M(str( "VPXORQ `zero_vector, `zero_vector, `zero_vector" ));

    //out_registers_high is shifted up one limb then added to out_registers_low
    for (int i=num_out_vectors-1;i>=0;--i) {
        //for each limb, find the next lowest limb (0 for the first limb)
        string low_register_name=(i==0)? "`zero_vector" : str( "`out_registers_high_#", i-1 );
        APPEND_M(str( "VMOVDQU64 `a, `b" )); //b = permutate_indexes
        APPEND_M(str( "VPERMI2Q `a, #, `out_registers_high_#", low_register_name, i ));

        //add the shifted high limbs to the low limbs
        APPEND_M(str( "VPADDQ `out_registers_low_#, `out_registers_low_#, `a", i, i ));
    }
}


}}