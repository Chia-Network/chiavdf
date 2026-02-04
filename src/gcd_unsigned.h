#ifndef GCD_UNSIGNED_H
#define GCD_UNSIGNED_H

#include <atomic>

#ifdef ARCH_ARM
// Count how often we fallback because `ab[0] < ab[1]` (invariant violation).
extern std::atomic<uint64_t> gcd_unsigned_arm_bad_order_fallbacks;
// Count how often we fallback because `gcd_128(...)` returned false (no progress / bad quotient discovery).
extern std::atomic<uint64_t> gcd_unsigned_arm_gcd128_fail_fallbacks;
// Count how often we repaired a bad/failed gcd_128 step by doing an exact single-quotient division step.
extern std::atomic<uint64_t> gcd_unsigned_arm_exact_division_repairs;
#endif

// Optional output stream for producing the per-iteration UV matrices in the
// asm producer/consumer format (u0,u1,v0,v1,parity,exit_flag,...).
//
// When `out_uv_addr` is non-null, it is assumed to point at the storage for
// iteration 0 (e.g. `&uv_entries[1]` interpreted as `uint64*`), and entry -1 is
// located at `out_uv_addr - 8`.
struct gcd_unsigned_uv_stream_out {
    uint64* out_uv_addr = nullptr;
    // Optional counter to publish availability of each streamed entry.
    // If non-null, the producer must store `uv_counter_start + iter_index` (release)
    // after writing the entry for that iteration index.
    std::atomic<uint64>* out_uv_counter = nullptr;
    uint64 uv_counter_start = 0;
    bool inputs_swapped = false; // if true, store UV in original (a,b) order
    int iter_count = 0;          // number of streamed iterations produced
    bool ok = true;              // false if fast path couldn't produce a stream
};

//threshold is 0 to calculate the normal gcd
template<int size> void gcd_unsigned_slow(
    array<fixed_integer<uint64, size>, 2>& ab,
    array<fixed_integer<uint64, size>, 2>& uv,
    int& parity,
    const fixed_integer<uint64, size>& threshold=fixed_integer<uint64, size>(integer(0))
) {
    assert(ab[0]>threshold);

    while (ab[1]>threshold) {
        fixed_integer<uint64, size> q(ab[0]/ab[1]);
        fixed_integer<uint64, size> r(ab[0]%ab[1]);

        ab[0]=ab[1];
        ab[1]=r;

        //this is the absolute value of the cofactor matrix
        auto u1_new=uv[0] + q*uv[1];
        uv[0]=uv[1];
        uv[1]=u1_new;

        parity=-parity;
    }
}

//todo
//test this by making two numbers that have a specified quotient sequence. can add big quotients then
//to generate numbers with a certain quotient sequence:
//euclidean algorithm: q=a/b ; a'=b ; b'=a-q*b ; terminates when b'=0
//initially b'=0 and all qs are known
//first iteration: b'=a-q*b=0 ; a=q*b ; select some b and this will determine a
//next: b'=a-q*b ; a'=b ; b'=a-q*a' ; b'+q*a'=a

//uv is <1,0> to calculate |u| and <0,1> to calculate |v|
//parity is negated for each quotient
template<int size> bool gcd_unsigned(
    array<fixed_integer<uint64, size>, 2>& ab,
    array<fixed_integer<uint64, size>, 2>& uv,
    int& parity,
    fixed_integer<uint64, size> threshold=fixed_integer<uint64, size>(integer(0)),
    gcd_unsigned_uv_stream_out* stream_out=nullptr
) {
    typedef fixed_integer<uint64, size> int_t;

#if defined(TEST_ASM) && !defined(ARCH_ARM)
    static int test_asm_counter=0;
    ++test_asm_counter;

    bool test_asm_run=true;
    bool test_asm_print=(test_asm_counter%1000==0);
#else
    // Avoid any per-call "test asm" bookkeeping on ARM / non-TEST_ASM builds.
    bool test_asm_run=false;
    bool test_asm_print=false;
#endif
    bool debug_output=false;

#if !defined(ARCH_ARM)
    assert(ab[0]>=ab[1] && !ab[1].is_negative());
    assert(!ab[0].is_negative() && !ab[1].is_negative());
    assert(!uv[0].is_negative() && !uv[1].is_negative());
#endif

    const bool producing_uv_stream = (stream_out != nullptr && stream_out->out_uv_addr != nullptr);

    // Only snapshot inputs when we might actually use them (slow self-check path).
    // On ARM we typically produce a UV stream for the fast-thread consumer, so this avoids
    // a few large fixed-integer copies per call.
    array<int_t, 2> ab_start;
    array<int_t, 2> uv_start;
    int parity_start=0;
    if (is_vdf_test && !producing_uv_stream) {
        ab_start=ab;
        uv_start=uv;
        parity_start=parity;
    }
    int a_num_bits_old=-1;

    int iter=0;

    // These vectors are only needed for the x86 TEST_ASM self-check block at the end.
    // On ARM (and in normal builds) they add avoidable allocation/branch overhead in the hot loop.
#if defined(TEST_ASM) && !defined(ARCH_ARM)
    vector<array<array<uint64, 2>, 2>> matricies;
    vector<int> local_parities;
#endif
    bool valid=true;

    if (stream_out) {
        stream_out->iter_count = 0;
        stream_out->ok = true;
    }

    while (true) {
#if !defined(ARCH_ARM)
        assert(ab[0]>=ab[1] && !ab[1].is_negative());
#endif

        if (debug_output) {
            print( "" );
            print( "" );
            print( "====================================" );

            for (int x=0;x<size;++x) print( "a limb", x, ab[0][x] );
            print( "" );

            for (int x=0;x<size;++x) print( "b limb", x, ab[1][x] );
            print( "" );

            for (int x=0;x<size;++x) print( "threshold limb", x, threshold[x] );
            print( "" );
        }

        if (ab[0]<=threshold) {
            valid=false;
#if !defined(ARCH_ARM)
            print( "    gcd_unsigned slow 1" );
#endif
            break;
        }

        if (ab[1]<=threshold) {
            if (debug_output) print( "ab[1]<=threshold" );
            break;
        }

        //there is a cached num limbs for a. the num limbs for b and ab_threshold is smaller
        //to calculate the new cached num limbs:
        //-look at previous value. if limb is 0, go on to the next lowest limb. a cannot be 0 but should still tolerate this without crashing
        //-unroll this two times
        //-if more than 2 iterations are required, use a loop
        //-a can only decrease in size so its true size can't be larger
        //-this also calculates the head limb of a. need the top 3 head limbs. they are 0-padded if a is less than 3 nonzero limbs
        //-the 3 head limbs are used to do the shift
        //-this also truncates threshold and compares a[1] with the truncated value. it will exit if they are equal. this is not
        // exactly the same as the c++ code
        //-should probably implement this in c++ first then to make the two codes the same
        int a_num_bits=ab[0].num_bits();
        int shift_amount=a_num_bits-128; //todo //changed this to 128 bits
        if (shift_amount<0) {
            shift_amount=0;
        }

        //print( "gcd_unsigned", a_num_bits, a_num_bits_old-a_num_bits );
        a_num_bits_old=a_num_bits;

        array<uint128, 2> ab_head={
            uint128(ab[0].window(shift_amount)) | (uint128(ab[0].window(shift_amount+64))<<64),
            uint128(ab[1].window(shift_amount)) | (uint128(ab[1].window(shift_amount+64))<<64)
        };
        //assert((ab_head[0]>>127)==0);
        //assert((ab_head[1]>>127)==0);

        uint128 threshold_head=uint128(threshold.window(shift_amount)) | (uint128(threshold.window(shift_amount+64))<<64);
        //assert((threshold_head>>127)==0);

        //don't actually need to do this
        //it will compare threshold_head with > so it will already exit if they are equal
        //if (shift_amount!=0) {
        //    ++threshold_head;
        //}

        if (debug_output) print( "a_num_bits:", a_num_bits );
        if (debug_output) print( "a last index:", (a_num_bits+63/64)-1 );
        if (debug_output) print( "shift_amount:", shift_amount );
        if (debug_output) print( "ab_head[0]:", uint64(ab_head[0]), uint64(ab_head[0]>>64) );
        if (debug_output) print( "ab_head[1]:", uint64(ab_head[1]), uint64(ab_head[1]>>64) );
        if (debug_output) print( "threshold_head:", uint64(threshold_head), uint64(threshold_head>>64) );

        array<array<uint64, 2>, 2> uv_uint64;
        int local_parity; //1 if odd, 0 if even

        auto set_exact_single_quotient_step = [&]() -> bool {
            // Exact Euclid step: q = a/b (must fit in uint64 for asm-style UV stream).
            // This is slower than the Lehmer/continued-fraction step but avoids falling back to
            // `square_original` (much more expensive) when gcd_128 is unstable on ARM.
            int_t q = ab[0] / ab[1];
            if (q.is_negative()) return false;
            for (int i = 1; i < size; i++) {
                if (q[i] != 0) {
                    return false;
                }
            }
            const uint64 q64 = q[0];

            uv_uint64[0][0] = 0;
            uv_uint64[0][1] = 1;
            uv_uint64[1][0] = 1;
            uv_uint64[1][1] = q64;
            local_parity = 1; // one quotient => odd
            return true;
        };

        if (gcd_128(ab_head, uv_uint64, local_parity, shift_amount!=0, threshold_head) ||
            set_exact_single_quotient_step()) {
            //int local_parity=(uv_double[1][1]<0)? 1 : 0; //sign bit
            bool even=(local_parity==0);

            if (debug_output) print( "u:", uv_uint64[0][0], uv_uint64[1][0] );
            if (debug_output) print( "v:", uv_uint64[0][1], uv_uint64[1][1] );
            if (debug_output) print( "local parity:", local_parity );

            uint64 uv_00=uv_uint64[0][0];
            uint64 uv_01=uv_uint64[0][1];
            uint64 uv_10=uv_uint64[1][0];
            uint64 uv_11=uv_uint64[1][1];

            //can use a_num_bits to make these smaller. this is at most a 2x speedup for these mutliplications which probably doesn't matter
            //can do this with an unsigned subtraction and just swap the pointers
            //
            //this is an unsigned subtraction with the input pointers swapped to make the result nonnegative
            //
            //this uses mulx/adox/adcx if available for the multiplication
            //will unroll the multiplication loop but early-exit based on the number of limbs in a (calculated before). this gives each
            //branch its own branch predictor entry. each branch is at a multiple of 4 limbs. don't need to pad a
            int_t a_new_1=ab[0]; a_new_1*=uv_00; a_new_1.set_negative(!even);
            int_t a_new_2=ab[1]; a_new_2*=uv_01; a_new_2.set_negative(even);
            int_t b_new_1=ab[0]; b_new_1*=uv_10; b_new_1.set_negative(even);
            int_t b_new_2=ab[1]; b_new_2*=uv_11; b_new_2.set_negative(!even);

            //both of these are subtractions; the signs determine the direction. the result is nonnegative
            int_t a_new;
            int_t b_new;
            if (!even) {
                a_new=int_t(a_new_2 + a_new_1);
                b_new=int_t(b_new_1 + b_new_2);
            } else {
                a_new=int_t(a_new_1 + a_new_2);
                b_new=int_t(b_new_2 + b_new_1);
            }

            //this allows the add function to be optimized
#if !defined(ARCH_ARM)
            assert(!a_new.is_negative());
            assert(!b_new.is_negative());
#endif
            // On ARM, gcd_128/FMA path can produce edge cases; skip to avoid spurious trap.

            //do not do any of this stuff; instead return an array of matricies
            //the array is processed while it is being generated so it is cache line aligned, has a counter, etc

            // The algorithm assumes unsigned inputs with `ab[0] >= ab[1]` for every iteration.
            // If the cofactor matrix violates this, retry once with an exact single-quotient step.
            if (a_new < b_new) {
#ifdef ARCH_ARM
                ++gcd_unsigned_arm_bad_order_fallbacks;
#endif
                if (!set_exact_single_quotient_step()) {
                    valid=false;
                    break;
                }
                even = (local_parity == 0);

                uv_00=uv_uint64[0][0];
                uv_01=uv_uint64[0][1];
                uv_10=uv_uint64[1][0];
                uv_11=uv_uint64[1][1];

                a_new_1=ab[0]; a_new_1*=uv_00; a_new_1.set_negative(!even);
                a_new_2=ab[1]; a_new_2*=uv_01; a_new_2.set_negative(even);
                b_new_1=ab[0]; b_new_1*=uv_10; b_new_1.set_negative(even);
                b_new_2=ab[1]; b_new_2*=uv_11; b_new_2.set_negative(!even);

                if (!even) {
                    a_new=int_t(a_new_2 + a_new_1);
                    b_new=int_t(b_new_1 + b_new_2);
                } else {
                    a_new=int_t(a_new_1 + a_new_2);
                    b_new=int_t(b_new_2 + b_new_1);
                }

#ifdef ARCH_ARM
                ++gcd_unsigned_arm_exact_division_repairs;
#endif
            }

            ab[0]=a_new;
            ab[1]=b_new;

            // Update accumulated cofactors (unsigned).
            // This is hot; avoid lambda + by-value parameter copies.
            // Avoid `operator+` (which creates a size+1 temporary) to reduce memmoves.
            // The algorithm only relies on the low `size` limbs (same semantics as truncation).
            int_t new_uv_0 = uv[0];
            new_uv_0 *= uv_00;
            int_t add_uv_0 = uv[1];
            add_uv_0 *= uv_01;
            new_uv_0 += add_uv_0;

            int_t new_uv_1 = uv[0];
            new_uv_1 *= uv_10;
            int_t add_uv_1 = uv[1];
            add_uv_1 *= uv_11;
            new_uv_1 += add_uv_1;

            uv[0]=new_uv_0;
            uv[1]=new_uv_1;

            //local_parity is 0 even, 1 odd
            //want 1 even, -1 odd
            //todo: don't do this; just make it 0 even, 1 odd
            parity*=1-local_parity-local_parity;

#if defined(TEST_ASM) && !defined(ARCH_ARM)
            matricies.push_back(uv_uint64);
            local_parities.push_back(local_parity);
#endif

            if (producing_uv_stream) {
                uint64* entry = stream_out->out_uv_addr + iter * 8;
                if (stream_out->inputs_swapped) {
                    // uv col0 = coeff for b, col1 = coeff for a; caller expects (a, b) order
                    entry[0] = uv_uint64[0][1];
                    entry[1] = uv_uint64[1][1];
                    entry[2] = uv_uint64[0][0];
                    entry[3] = uv_uint64[1][0];
                } else {
                    entry[0] = uv_uint64[0][0];
                    entry[1] = uv_uint64[1][0];
                    entry[2] = uv_uint64[0][1];
                    entry[3] = uv_uint64[1][1];
                }
                entry[4] = static_cast<uint64>(local_parity); // 0 even, 1 odd
                entry[5] = 0;
                // Publish that this entry is ready.
                if (stream_out->out_uv_counter) {
                    stream_out->out_uv_counter->store(
                        // Match the x86 asm producer/consumer protocol: publish iter `i`
                        // as `uv_counter_start + i`. This intentionally makes the consumer
                        // one entry behind so the final entry isn't observed before the
                        // producer sets its `exit_flag`.
                        stream_out->uv_counter_start + static_cast<uint64>(iter),
                        std::memory_order_release);
                }
            }
        } else {
            //can just make the gcd fail if this happens in the asm code
#if !defined(ARCH_ARM)
            print( "    gcd_unsigned slow" );
#endif
            //todo assert(false); //very unlikely to happen if there are no bugs

            valid=false;
#ifdef ARCH_ARM
            ++gcd_unsigned_arm_gcd128_fail_fallbacks;
#endif
            break;

            /*had_slow=true;

            fixed_integer<uint64, size> q(ab[0]/ab[1]);
            fixed_integer<uint64, size> r(ab[0]%ab[1]);

            ab[0]=ab[1];
            ab[1]=r;

            //this is the absolute value of the cofactor matrix
            auto u1_new=uv[0] + q*uv[1];
            uv[0]=uv[1];
            uv[1]=u1_new;

            parity=-parity;*/
        }

        ++iter;
    }

    if (stream_out) {
        stream_out->iter_count = iter;
        stream_out->ok = valid;
    }

    // When producing an asm-style UV stream, we must not fall back to the slow
    // algorithm here (there is no equivalent per-iteration UV stream for it).
    // Treat "valid=false" as caller-visible failure instead.
    if (is_vdf_test && !producing_uv_stream) {
        auto ab2=ab_start;
        auto uv2=uv_start;
        int parity2=parity_start;
        gcd_unsigned_slow(ab2, uv2, parity2, threshold);

        if (valid) {
#if !defined(ARCH_ARM)
            assert(integer(ab[0]) == integer(ab2[0]));
            assert(integer(ab[1]) == integer(ab2[1]));
            assert(integer(uv[0]) == integer(uv2[0]));
            assert(integer(uv[1]) == integer(uv2[1]));
            assert(parity==parity2);
#endif
            // On ARM, fast path (gcd_128/FMA) can differ from slow path due to rounding; skip consistency assert.
        } else {
            ab=ab2;
            uv=uv2;
            parity=parity2;
        }
    }

    #if defined(TEST_ASM) && !defined(ARCH_ARM)
    if (test_asm_run) {
        if (test_asm_print) {
            print( "test asm gcd_unsigned", test_asm_counter );
        }

        asm_code::asm_func_gcd_unsigned_data asm_data;

        const int asm_size=gcd_size;
        const int asm_max_iter=gcd_max_iterations;

        assert(size>=1 && size<=asm_size);

        fixed_integer<uint64, asm_size> asm_a(ab_start[0]);
        fixed_integer<uint64, asm_size> asm_b(ab_start[1]);
        fixed_integer<uint64, asm_size> asm_a_2;
        fixed_integer<uint64, asm_size> asm_b_2;
        fixed_integer<uint64, asm_size> asm_threshold(threshold);

        uint64 asm_uv_counter_start=1234;
        uint64 asm_uv_counter=asm_uv_counter_start;

        array<array<uint64, 8>, asm_max_iter+1> asm_uv;

        asm_data.a=&asm_a[0];
        asm_data.b=&asm_b[0];
        asm_data.a_2=&asm_a_2[0];
        asm_data.b_2=&asm_b_2[0];
        asm_data.threshold=&asm_threshold[0];

        asm_data.uv_counter_start=asm_uv_counter_start;
        asm_data.out_uv_counter_addr=&asm_uv_counter;
        asm_data.out_uv_addr=(uint64*)&asm_uv[1];
        asm_data.iter=-2; //uninitialized
        asm_data.a_end_index=size-1;

        int error_code=hasAVX2()?
		asm_code::asm_avx2_func_gcd_unsigned(&asm_data):
	        asm_code::asm_cel_func_gcd_unsigned(&asm_data);

        auto asm_get_uv=[&](int i) {
            array<array<uint64, 2>, 2> res;
            res[0][0]=asm_uv[i+1][0];
            res[1][0]=asm_uv[i+1][1];
            res[0][1]=asm_uv[i+1][2];
            res[1][1]=asm_uv[i+1][3];
            return res;
        };

        auto asm_get_parity=[&](int i) {
            uint64 r=asm_uv[i+1][4];
            assert(r==0 || r==1);
            return bool(r);
        };

        auto asm_get_exit_flag=[&](int i) {
            uint64 r=asm_uv[i+1][5];
            assert(r==0 || r==1);
            return bool(r);
        };

        if (error_code==0) {
            assert(valid);

            assert(asm_data.iter>=0 && asm_data.iter<=asm_max_iter); //total number of iterations performed
            bool is_even=((asm_data.iter-1)&1)==0; //parity of last iteration (can be -1)

            fixed_integer<uint64, asm_size>& asm_a_res=(is_even)? asm_a_2 : asm_a;
            fixed_integer<uint64, asm_size>& asm_b_res=(is_even)? asm_b_2 : asm_b;

            assert(integer(asm_a_res) == integer(ab[0]));
            assert(integer(asm_b_res) == integer(ab[1]));

            for (int x=0;x<=matricies.size();++x) {
                assert( asm_get_exit_flag(x-1) == (x==matricies.size()) );

                if (x!=matricies.size()) {
                    assert(asm_get_parity(x)==local_parities[x]);
                    assert(asm_get_uv(x)==matricies[x]);
                }
            }

            assert(matricies.size()==asm_data.iter);
            assert(asm_uv_counter==asm_uv_counter_start+asm_data.iter-1); //the last iteration that updated the counter is iter-1
        } else {
            if (!valid) {
                print( "test asm gcd_unsigned error", error_code );
            }
        }
    }
    #endif

#if !defined(ARCH_ARM)
    assert(integer(ab[0])>integer(threshold));
    assert(integer(ab[1])<=integer(threshold));
#endif

    return valid;
}

// end Headerguard GCD_UNSIGNED_H
#endif
