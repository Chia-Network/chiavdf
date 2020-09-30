#ifndef AVX512_INTEGER_H
#define AVX512_INTEGER_H

#define TEST_AVX512 0

template<int d_num_limbs, int d_padded_size> struct alignas(64) avx512_integer {
    static const int num_limbs=d_num_limbs;
    static const int padded_size=d_padded_size;

    //this is the largest mpz integer size that is used
    static const int validate_mpz_expected_size=33;
    static const int validate_mpz_padded_size=40;

    static_assert(padded_size%8==0, "");

    uint64 data_padded[padded_size+16]; //must be cache line aligned. includes 64 bytes of padding at the start and end
    uint64 sign;

    #if TEST_AVX512
        integer expected_value; //16 bytes
        uint64 is_init=0;
        uint64 padding[4];
    #else
        uint64 padding[7];
    #endif

    uint64* data() { return data_padded+8; }
    const uint64* data() const { return data_padded+8; }

    avx512_integer() {}

    template<int mpz_expected_size, int mpz_padded_size> avx512_integer(const mpz<mpz_expected_size, mpz_padded_size>& t_mpz) {
        *this=t_mpz;
    }

    template<int mpz_expected_size, int mpz_padded_size>
    avx512_integer& operator=(const mpz<mpz_expected_size, mpz_padded_size>& t_mpz) {
        assert(!t_mpz.was_reallocated());
        sign=asm_code::asm_avx512_func_to_avx512_integer<mpz_expected_size, num_limbs>(
            uint64(int64(t_mpz.c_mpz._mp_size)), t_mpz.data, data()
        );

        #if TEST_AVX512
            mpz_set(expected_value.impl, t_mpz._());
            is_init=1;
            validate();
        #endif

        return *this;
    }

    template<int mpz_expected_size, int mpz_padded_size>
    void assign(mpz<mpz_expected_size, mpz_padded_size>& t_mpz) const {
        assert(!t_mpz.was_reallocated());

        t_mpz.c_mpz._mp_size=int(asm_code::asm_avx512_func_to_gmp_integer<num_limbs, mpz_expected_size>(
            sign, data(), (uint64*)t_mpz._()->_mp_d  // Casting (mp_limb_t *) to (uint64*)
        ));

        #if TEST_AVX512
            assert(is_init);
        #endif
    }

    template<int a_num_limbs, int a_padded_size, int b_num_limbs, int b_padded_size>
    void set_add(const avx512_integer<a_num_limbs, a_padded_size>& a, const avx512_integer<b_num_limbs, b_padded_size>& b) {
        sign=asm_code::asm_avx512_func_add<a_num_limbs, b_num_limbs, num_limbs>(a.sign, a.data(), b.sign, b.data(), data());

        #if TEST_AVX512
            is_init=1;
            assert(a.is_init);
            assert(b.is_init);
            expected_value=a.expected_value+b.expected_value;
            validate();
        #endif
    }

    template<int a_num_limbs, int a_padded_size, int b_num_limbs, int b_padded_size>
    void set_sub(const avx512_integer<a_num_limbs, a_padded_size>& a, const avx512_integer<b_num_limbs, b_padded_size>& b) {
        sign=asm_code::asm_avx512_func_add<a_num_limbs, b_num_limbs, num_limbs>(a.sign, a.data(), ~b.sign, b.data(), data());

        #if TEST_AVX512
            is_init=1;
            assert(a.is_init);
            assert(b.is_init);
            expected_value=a.expected_value-b.expected_value;
            validate();
        #endif
    }

    template<int a_num_limbs, int a_padded_size, int b_num_limbs, int b_padded_size>
    void set_mul(const avx512_integer<a_num_limbs, a_padded_size>& a, const avx512_integer<b_num_limbs, b_padded_size>& b) {
        sign=asm_code::asm_avx512_func_multiply<a_num_limbs, b_num_limbs, num_limbs>(a.sign, a.data(), b.sign, b.data(), data());

        #if TEST_AVX512
            is_init=1;
            assert(a.is_init);
            assert(b.is_init);
            expected_value=a.expected_value*b.expected_value;
            validate();
        #endif
    }

    #if TEST_AVX512
        void validate() const {
            mpz<validate_mpz_expected_size, validate_mpz_padded_size> c_mpz;
            assign(c_mpz);
            assert(is_init);
            assert(c_mpz==expected_value.impl);
        }
    #endif
};

template<int expected_size> struct avx512_integer_for_size {};
template<> struct avx512_integer_for_size< 9> { typedef avx512_integer<12, 16> i; };
template<> struct avx512_integer_for_size<17> { typedef avx512_integer<21, 24> i; };
template<> struct avx512_integer_for_size<25> { typedef avx512_integer<31, 32> i; };
template<> struct avx512_integer_for_size<33> { typedef avx512_integer<41, 48> i; };

template<int expected_size_out, int padded_size_out, int expected_size_a, int padded_size_a, int expected_size_b, int padded_size_b>
void mpz_impl_set_mul(
    mpz<expected_size_out, padded_size_out>& out,
    const mpz<expected_size_a, padded_size_a>& a,
    const mpz<expected_size_b, padded_size_b>& b
) {
    if (enable_avx512_ifma) {
        typename avx512_integer_for_size<expected_size_a>::i a_avx512;
        typename avx512_integer_for_size<expected_size_b>::i b_avx512;
        typename avx512_integer_for_size<expected_size_out>::i out_avx512;

        a_avx512=a;
        b_avx512=b;
        out_avx512.set_mul(a_avx512, b_avx512);
        out_avx512.assign(out);
    } else {
        mpz_mul(out._(), a._(), b._());
    }
}

// end Headerguard AVX512_INTEGER_H
#endif
