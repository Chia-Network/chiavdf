#ifndef PARAMETERS_H
#define PARAMETERS_H

//have to pass one of these in as a macro
//#define VDF_MODE 0 //used for the final submission and correctness testing
//#define VDF_MODE 1 //used for performance or other testing

//also have to pass in one of these
//#define ENABLE_ALL_INSTRUCTIONS 1
//#define ENABLE_ALL_INSTRUCTIONS 0

//
//

//divide table
const int divide_table_index_bits=11;
const int gcd_num_quotient_bits=31; //excludes sign bit
const int data_size=31;
const int gcd_base_max_iter_divide_table=16;

//continued fraction table
const int gcd_table_num_exponent_bits=3;
const int gcd_table_num_fraction_bits=7;
const int gcd_base_max_iter=5;

extern bool use_divide_table;
extern int gcd_base_bits;
extern int gcd_128_max_iter;
extern std::string asmprefix;
extern bool enable_all_instructions;

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <cstdint>
#if defined(_MSC_VER)
#include <immintrin.h>
#endif

inline std::atomic<bool> bAVX2{false};
inline std::atomic<bool> enable_avx512_ifma{false};
inline std::once_flag avx_flags_once;

inline bool env_flag(const char* name) {
  const char* value = getenv(name);
  if (!value) {
    return false;
  }
  if (value[0] == '\0') {
    return false;
  }
  if (value[0] == '1' || value[0] == 'y' || value[0] == 'Y' || value[0] == 't' || value[0] == 'T') {
    return true;
  }
  if (value[0] == '0' || value[0] == 'n' || value[0] == 'N' || value[0] == 'f' || value[0] == 'F') {
    return false;
  }
  return true;
}

inline bool should_log_avx() {
  return env_flag("CHIAVDF_LOG_AVX");
}

#if defined(__i386) || defined(_M_IX86)
    #define ARCH_X86
#elif defined(__x86_64__) || defined(_M_X64)
    #define ARCH_X64
#elif defined(__aarch64__) || (defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 5) || (defined(_M_ARM) && _M_ARM >= 5) || defined(__ARM_FEATURE_CLZ) /* ARM (aarch64 or Architecture Version 5+) */
    #define ARCH_ARM
#endif

#if defined(_WIN64) || defined(_LP64) || defined(__LP64__)
    #define ARCH_64BIT
#else
    #define ARCH_32BIT
#endif

inline void init_avx_flags()
{
#if defined(ARCH_X86) || defined(ARCH_X64)
    const bool disable_avx2 = env_flag("CHIA_DISABLE_AVX2");
    const bool force_avx2 = env_flag("CHIA_FORCE_AVX2");
    const bool disable_avx512 = env_flag("CHIA_DISABLE_AVX512_IFMA");
    const bool enable_avx512 = env_flag("CHIA_ENABLE_AVX512_IFMA");
    const bool force_avx512 = env_flag("CHIA_FORCE_AVX512_IFMA");
    int info[4] = {0};
    int info1[4] = {0};
#if defined(_MSC_VER)
    __cpuid(info1, 0x1);
    __cpuid(info, 0x7);
#elif defined(__GNUC__) || defined(__clang__)
#if defined(ARCH_X86) && defined(__PIC__)
    __asm__ __volatile__ (
                "xchg{l} {%%}ebx, %k1;"
                "cpuid;"
                "xchg{l} {%%}ebx, %k1;"
                : "=a"(info1[0]), "=&r"(info1[1]), "=c"(info1[2]), "=d"(info1[3]) : "a"(0x1), "c"(0)
    );
    __asm__ __volatile__ (
                "xchg{l} {%%}ebx, %k1;"
                "cpuid;"
                "xchg{l} {%%}ebx, %k1;"
                : "=a"(info[0]), "=&r"(info[1]), "=c"(info[2]), "=d"(info[3]) : "a"(0x7), "c"(0)
    );
#else
    __asm__ __volatile__ (
                "cpuid" : "=a"(info1[0]), "=b"(info1[1]), "=c"(info1[2]), "=d"(info1[3]) : "a"(0x1), "c"(0)
    );
    __asm__ __volatile__ (
                "cpuid" : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3]) : "a"(0x7), "c"(0)
    );
#endif
#endif
    uint64_t xcr0 = 0;
    bool osxsave_enabled = false;
#if defined(_MSC_VER)
    const int OSXSAVE = 1 << 27;
    osxsave_enabled = ((info1[2] & OSXSAVE) == OSXSAVE);
    if (osxsave_enabled) {
        xcr0 = static_cast<uint64_t>(_xgetbv(0));
    }
#elif defined(__GNUC__) || defined(__clang__)
    const int OSXSAVE = 1 << 27;
    osxsave_enabled = ((info1[2] & OSXSAVE) == OSXSAVE);
    if (osxsave_enabled) {
        uint32_t eax = 0;
        uint32_t edx = 0;
        __asm__ __volatile__ (
            ".byte 0x0f, 0x01, 0xd0"
            : "=a"(eax), "=d"(edx)
            : "c"(0)
        );
        xcr0 = (static_cast<uint64_t>(edx) << 32) | eax;
    }
#endif

    constexpr uint64_t XCR0_XMM_MASK = (1ULL << 1);
    constexpr uint64_t XCR0_YMM_MASK = (1ULL << 2);
    constexpr uint64_t XCR0_AVX_MASK = XCR0_XMM_MASK | XCR0_YMM_MASK;
    constexpr uint64_t XCR0_AVX512_MASK = XCR0_AVX_MASK | (1ULL << 5) | (1ULL << 6) | (1ULL << 7);

    const int AVX = 1 << 28;
    const int AVX2 = 1<<5;
    const int ADX = 1<<19;
    const int AVX512F = 1<<16;
    const int AVX512IFMA = 1<<21;

    bool avxbit = ((info1[2] & AVX) == AVX);
    bool avx2bit = ((info[1] & AVX2) == AVX2);
    bool adxbit = ((info[1] & ADX) == ADX);
    bool avx512fbit = ((info[1] & AVX512F) == AVX512F);
    bool avx512ifmabit = ((info[1] & AVX512IFMA) == AVX512IFMA);
    bool avx_os_state = osxsave_enabled && ((xcr0 & XCR0_AVX_MASK) == XCR0_AVX_MASK);
    bool avx512_os_state = osxsave_enabled && ((xcr0 & XCR0_AVX512_MASK) == XCR0_AVX512_MASK);

    if (disable_avx2) {
      bAVX2.store(false, std::memory_order_relaxed);
    } else if (force_avx2) {
      bAVX2.store(true, std::memory_order_relaxed);
    } else {
      bAVX2.store(avxbit && avx2bit && adxbit && avx_os_state, std::memory_order_relaxed);
    }
    if (bAVX2.load(std::memory_order_relaxed) && should_log_avx()) {
      std::fprintf(stderr, "AVX2 enabled (avx=%d avx2=%d adx=%d osxsave=%d xcr0=0x%llx)\n",
                   avxbit ? 1 : 0,
                   avx2bit ? 1 : 0,
                   adxbit ? 1 : 0,
                   osxsave_enabled ? 1 : 0,
                   static_cast<unsigned long long>(xcr0));
    }

    if (disable_avx512) {
      enable_avx512_ifma.store(false, std::memory_order_relaxed);
    } else if (force_avx512) {
      enable_avx512_ifma.store(true, std::memory_order_relaxed);
    } else if (enable_avx512) {
      enable_avx512_ifma.store(avxbit && avx512fbit && avx512ifmabit && avx512_os_state, std::memory_order_relaxed);
    } else {
      enable_avx512_ifma.store(false, std::memory_order_relaxed);
    }
    if (enable_avx512_ifma.load(std::memory_order_relaxed) && should_log_avx()) {
      std::fprintf(stderr, "AVX512 IFMA enabled (avx=%d f=%d ifma=%d osxsave=%d xcr0=0x%llx)\n",
                   avxbit ? 1 : 0,
                   avx512fbit ? 1 : 0,
                   avx512ifmabit ? 1 : 0,
                   osxsave_enabled ? 1 : 0,
                   static_cast<unsigned long long>(xcr0));
    }
#elif defined(ARCH_ARM)
    bAVX2.store(false, std::memory_order_relaxed);
    enable_avx512_ifma.store(false, std::memory_order_relaxed);
#else
    bAVX2.store(false, std::memory_order_relaxed);
    enable_avx512_ifma.store(false, std::memory_order_relaxed);
#endif
}

inline bool hasAVX2()
{
  std::call_once(avx_flags_once, init_avx_flags);
  return bAVX2.load(std::memory_order_relaxed);
}

/*
divide_table_index bits
10 - 0m1.269s
11 - 0m1.261s
12 - 0m1.262s
13 - 0m1.341s
**/

/*
gcd_base_max_iter_divide_table
13 - 0m1.290s
14 - 0m1.275s
15 - 0m1.265s
16 - 0m1.261s
17 - 0m1.268s
18 - 0m1.278s
19 - 0m1.283s
**/

/*
100k iterations; median of 3 runs. consistency between runs was very high

effect of scheduler:
taskset 0,1     : 0m1.352s (63% speedup single thread, 37% over 0,2)
taskset 0,2     : 0m1.850s
default         : 0m1.348s (fastest)
single threaded : 0m2.212s [this has gone down to 0m1.496s for some reason with the divide table]

exponent    fraction    base_bits   base_iter   128_iter    seconds
3           7           50          5           3           0m1.350s    [fastest with range checks enabled]
3           7           52          5           3           0m1.318s    [range checks disabled; 2.4% faster]

[this block with bmi and fma disabled]
3           7           46          5           3           0m1.426s
3           7           47          5           3           0m1.417s
3           7           48          5           3           0m1.421s
3           7           49          5           3           0m1.413s
3           7           50          5           3           0m1.401s    [still fastest; bmi+fma is 3.8% faster]
3           7           51          5           3           0m1.406s
3           7           52          5           3           0m1.460s
3           7           50          6           3           0m1.416s

3           7           49          6           3           0m1.376s

2           8           45          6           3           0m1.590s
2           8           49          6           3           0m1.485s
2           8           51          6           3           0m1.479s
2           8           52          6           3           0m1.501s
2           8           53          6           3           0m1.531s
2           8           54          6           3           0m13.675s
2           8           55          6           3           0m13.648s

3           7           49          2           3           0m14.571s
3           7           49          3           3           0m1.597s
3           7           49          4           3           0m1.430s
3           7           49          5           3           0m1.348s
3           7           49          6           3           0m1.376s
3           7           49          10          3           0m1.485s

3           7           49          1           18          0m2.226s
3           7           49          2           10          0m1.756s
3           7           49          3           6           0m1.557s
3           7           49          4           4           0m1.388s
3           7           49          5           4           0m1.525s
3           7           49          6           3           0m1.377s
3           7           49          7           3           0m1.446s
3           7           49          8           2           0m1.503s

3           6           45          4           3           0m15.176s
3           7           45          4           3           0m1.443s
3           8           45          4           3           0m1.386s
3           9           45          4           3           0m1.355s
3           10          45          4           3           0m1.353s
3           11          45          4           3           0m1.419s
3           12          45          4           3           0m1.451s
3           13          45          4           3           0m1.584s

3           7           40          4           2           0m1.611s
3           8           40          4           2           0m1.570s
3           9           40          4           2           0m1.554s
3           10          40          4           2           0m1.594s
3           11          40          4           2           0m1.622s
3           12          40          4           2           0m1.674s
3           13          40          4           2           0m1.832s

3           7           48          5           3           0m1.358s
3           7           49          5           3           0m1.353s
3           7           50          5           3           0m1.350s

3           8           48          5           3           0m1.366s
3           8           49          5           3           0m1.349s
3           8           50          5           3           0m1.334s

3           9           48          5           3           0m1.370s
3           9           49          5           3           0m1.349s
3           9           50          5           3           0m1.346s

3           10          48          5           3           0m1.404s
3           10          49          5           3           0m1.382s
3           10          50          5           3           0m1.379s
***/

const uint64 max_spin_counter=10000000;

//this value makes square_original not be called in 100k iterations. with every iteration reduced, minimum value is 1
const int num_extra_bits_ab=3;

const bool calculate_k_repeated_mod=false;
const bool calculate_k_repeated_mod_interval=1;

const int validate_interval=1; //power of 2. will check the discriminant in the slave thread at this interval. -1 to disable. no effect on performance
const int checkpoint_interval=10000; //at each checkpoint, the slave thread is restarted and the master thread calculates c
//checkpoint_interval=100000: 39388
//checkpoint_interval=10000:  39249 cycles per fast iteration
//checkpoint_interval=1000:   38939
//checkpoint_interval=100:    39988
//no effect on performance (with track cycles enabled)

// ==== test ====
#if VDF_MODE==1
    #define VDF_TEST
    const bool is_vdf_test=true;

    const bool enable_random_error_injection=false;
    const double random_error_injection_rate=0; //0 to 1

    //#define GENERATE_ASM_TRACKING_DATA
    //#define ENABLE_TRACK_CYCLES
    const bool vdf_test_correctness=false;
    const bool enable_threads=true;
#endif

// ==== production ====
#if VDF_MODE==0
    const bool is_vdf_test=false;

    const bool enable_random_error_injection=false;
    const double random_error_injection_rate=0; //0 to 1

    const bool vdf_test_correctness=false;
    const bool enable_threads=true;

    //#define ENABLE_TRACK_CYCLES
#endif

//
//

//this doesn't do anything outside of test code
//this doesn't work with the divide table currently
#define TEST_ASM

const int gcd_size=20; //multiple of 4. must be at least half the discriminant size in bits divided by 64

const int gcd_max_iterations=gcd_size*2; //typically 1 iteration per limb

const int max_bits_base=1024; //half the discriminant number of bits, rounded up
const int reduce_max_iterations=10000;

const int num_asm_tracking_data=128;

const int track_cycles_num_buckets=24; //each bucket is from 2^i to 2^(i+1) cycles
const int track_cycles_max_num=128;

void mark_vdf_test() {
    static bool did_warning=false;
    if (!is_vdf_test && !did_warning) {
        print( "test code enabled in production build" );
        did_warning=true;
    }
}

// end Headerguard PARAMETERS_H
#endif
