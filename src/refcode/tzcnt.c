/*
Count trailing zero bits. Choice of public domain or MIT-0.

David Reid - mackron@gmail.com

The tzcnt32 and tzcnt64 functions count the number of trailing zero bits in a given 32- or 64-bit variable. When the input variable is 0, the
total size in bits will be returned (32 for tzcnt32 and 64 for tzcnt64).

For x86/64 platforms, this will use the TZCNT instruction if available. On ARM it will be implemented in terms of the CLZ instruction. If these
are unavailable it will fall back to compiler-specific built-ins. If these are unavailable it'll fall back to the generic implementation.

Functions
---------
tzcnt32_generic
tzcnt64_generic
    Generic implementation.

tzcnt32_msvc_bsf
tzcnt64_msvc_bsf
    MSVC built-in implementation using _BitScanForward/64(). Note that tzcnt64_mscv_bsf() is only available when compiling as 64-bit.

tzcnt32_gcc_builtin
tzcnt64_gcc_builtin
    GCC/Clang built-in implementation using __builtin_ctzl/l(). Note that tzcnt64_gcc_builtin() is only available when compiling as 64-bit.

tzcnt32_msvc_x86
tzcnt64_msvc_x64
    MSVC implementation in terms of the __lzcnt and __lzcnt64 intrinsic. Note that these are only available when targeting x86/64. tzcnt64_msvc_x64()
    is only available when compiling as 64-bit.

tzcnt32_gcc_x86
tzcnt64_gcc_x64
    GCC/Clang inline assembly implementation. This will emit the TZCNT instruction. Note that these are only available when targeting x86/x64
    and when compiled using a compiler that supports GCC style inline assembly.

tzcnt32_gcc_arm
tzcnt64_gcc_arm
    GCC/Clang inline assembly implementation. This will be implemented in terms of the CLZ instruction. Note that these are only available when
    targeting ARM architecture version 5 and above and when compiled using a compiler that supports GCC style inline assembly.

tzcnt32_hard
tzcnt64_hard
    High level helper for calling an hardware implementation. This will choose either tzcnt32_msvc_x86()/tzcnt64_msvc_x64() or tzcnt32_gcc_x86()/
    tzcnt64_gcc_x64() depending on the environment. Note that this is only available when targeting x86/64. tzcnt64_hard() is only available
    when compiling as 64-bit. You should only call this if has_tzcnt_hard() returns non-zero.

tzcnt32_soft
tzcnt64_soft
    High level helper for calling the best software implementation available for the current build environment.

tzcnt32
tzcnt64
    High level helper for calling either a hardware or software implementation depending on the build environment. This will always favor a
    hardware implementation. Do not call this in high performance code. The reason for this is that each it will call has_tzcnt_hard() each
    time which may be too fine grained for your purposes. You may be better off calling has_tzcnt_hard() once at a higher level.

has_tzcnt_hard
    Determines whether or not a hardware implementation of tzcnt is available. Use this to know whether or not you can call tzcnt32/64_hard().
    Note that this calls CPUID for each, so you may want to cache the result. Use HAS_TZCNT32/64_HARD to check for compile-time support.
*/
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(__i386) || defined(_M_IX86)
    #define ARCH_X86
#elif defined(__x86_64__) || defined(_M_X64)
    #define ARCH_X64
#elif (defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 5) || (defined(_M_ARM) && _M_ARM >= 5) || defined(__ARM_FEATURE_CLZ) /* ARM (Architecture Version 5) */
    #define ARCH_ARM
#endif

#if defined(_WIN64) || defined(_LP64) || defined(__LP64__)
    #define ARCH_64BIT
#else
    #define ARCH_32BIT
#endif

#if defined(ARCH_X86) || defined(ARCH_X64)
    /* x86/64 */
    #if defined(_MSC_VER) && _MSC_VER >= 1500
        #define HAS_TZCNT32_HARD
        #if defined(ARCH_64BIT)
            #define HAS_TZCNT64_HARD
        #endif
    #elif defined(__GNUC__) || defined(__clang__)
        #define HAS_TZCNT32_HARD
        #if defined(ARCH_64BIT)
            #define HAS_TZCNT64_HARD
        #endif
    #endif
#elif defined(ARCH_ARM)
    /* ARM */
    #if defined(__GNUC__) || defined(__clang__)
        #define HAS_TZCNT32_HARD
        #if defined(ARCH_64BIT)
            #define HAS_TZCNT64_HARD
        #endif
    #endif
#endif

#if defined(_MSC_VER) && _MSC_VER >= 1500 && (defined(ARCH_X86) || defined(ARCH_X64)) && !defined(__clang__)
    #define HAS_TZCNT_INTRINSIC
#elif (defined(__GNUC__) && ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)))
    #define HAS_TZCNT_INTRINSIC
#elif defined(__clang__)
    #if defined(__has_builtin)
        #if __has_builtin(__builtin_ctzll) || __has_builtin(__builtin_ctzl)
            #define HAS_TZCNT_INTRINSIC
        #endif
    #endif
#endif

unsigned int tzcnt32_generic(unsigned int x)
{
    unsigned int n;

    /* Special case for odd numbers since they should happen about half the time. */
    if (x & 0x1)  {
        return 0;
    }

    if (x == 0) {
        return sizeof(x) << 3;
    }

    n = 1;
    if ((x & 0x0000FFFF) == 0) { x >>= 16; n += 16; }
    if ((x & 0x000000FF) == 0) { x >>=  8; n +=  8; }
    if ((x & 0x0000000F) == 0) { x >>=  4; n +=  4; }
    if ((x & 0x00000003) == 0) { x >>=  2; n +=  2; }
    n -= x & 0x00000001;

    return n;
}

unsigned int tzcnt64_generic(unsigned long long x)
{
    unsigned int n;

    /* Special case for odd numbers since they should happen about half the time. */
    if (x & 0x1)  {
        return 0;
    }

    if (x == 0) {
        return sizeof(x) << 3;
    }

    n = 1;
    if ((x & 0xFFFFFFFF) == 0) { x >>= 32; n += 32; }
    if ((x & 0x0000FFFF) == 0) { x >>= 16; n += 16; }
    if ((x & 0x000000FF) == 0) { x >>=  8; n +=  8; }
    if ((x & 0x0000000F) == 0) { x >>=  4; n +=  4; }
    if ((x & 0x00000003) == 0) { x >>=  2; n +=  2; }
    n -= x & 0x00000001;

    return n;
}

/* Generic compiler specific intrinsics. */
#if defined(_MSC_VER)
unsigned int tzcnt32_msvc_bsf(unsigned int x)
{
    unsigned long n;

    if (x == 0) {
        return 32;
    }

    _BitScanForward(&n, x);

    return n;
}

/* _BitScanReverse64() is only available on 64-bit builds. */
#if defined(ARCH_64BIT)
unsigned int tzcnt64_msvc_bsf(unsigned long long x)
{
    unsigned long n;

    if (x == 0) {
        return 64;
    }

    _BitScanForward64(&n, x);

    return n;
}
#endif  /* ARCH_64BIT */
#elif (defined(__GNUC__) || defined(__clang__)) && defined(HAS_TZCNT_INTRINSIC)
unsigned int tzcnt32_gcc_builtin(unsigned int x)
{
    if (x == 0) {
        return 32;
    }

    return (unsigned int)__builtin_ctzl((unsigned long)x);
}

unsigned int tzcnt64_gcc_builtin(unsigned long long x)
{
    if (x == 0) {
        return 64;
    }

    return (unsigned int)__builtin_ctzll(x);
}
#endif

int has_tzcnt_hard()
{
#if defined(ARCH_X86) || defined(ARCH_X64)
    int info[4] = {0};

    #if defined(_MSC_VER)
        __cpuid(info, 0x80000001);
    #elif defined(__GNUC__) || defined(__clang__)
        /*
        It looks like the -fPIC option uses the ebx register which GCC complains about. We can work around this by just using a different register, the
        specific register of which I'm letting the compiler decide on. The "k" prefix is used to specify a 32-bit register. The {...} syntax is for
        supporting different assembly dialects.

        What's basically happening is that we're saving and restoring the ebx register manually.
        */
        #if defined(ARCH_X86) && defined(__PIC__)
            __asm__ __volatile__ (
                "xchg{l} {%%}ebx, %k1;"
                "cpuid;"
                "xchg{l} {%%}ebx, %k1;"
                : "=a"(info[0]), "=&r"(info[1]), "=c"(info[2]), "=d"(info[3]) : "a"(0x80000001), "c"(0)
            );
        #else
            __asm__ __volatile__ (
                "cpuid" : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3]) : "a"(0x80000001), "c"(0)
            );
        #endif
    #endif

    return (info[2] & (1 << 5)) != 0;
#elif defined(ARCH_ARM)
    return 1;   /* The CLZ instruction is available starting from ARM architecture version 5. Our ARCH_ARM #define is only defined when targeting version 5 at compile time. */
#else
    return 0;   /* Hardware TZCNT is only supported in x86/64 and ARM for now. */
#endif
}

/* Intrinsics and inline-assembly. x86/64 has a hardware TZCNT instruction. You can only call these if has_tzcnt_hard() returns true. */
#if defined(HAS_TZCNT32_HARD)
    #if defined(ARCH_X86) || defined(ARCH_X64)
        #if defined(_MSC_VER) && !defined(__clang__)
            /* Unfortunately no tzcnt instrinsic on MSVC, but we can build it in terms of lzcnt(). */
            unsigned int tzcnt32_msvc_x86(unsigned int x)
            {
                if (x == 0) {
                    return sizeof(x) << 3;
                }

                return 31 - __lzcnt(x & -(int)x);
            }
        #elif defined(__GNUC__) || defined(__clang__)
            unsigned int tzcnt32_gcc_x86(unsigned int x)
            {
                /*
                att:   tzcntl [out], [in]
                intel: tzcnt  [in], [out]
                */
                unsigned int r;
                __asm__ __volatile__ (
                    "tzcnt{l %1, %0| %0, %1}" : "=r"(r) : "r"(x)
                );

                return r;
            }
        #endif
    #endif
    #if defined(ARCH_ARM)
        #if defined(__GNUC__) || defined(__clang__)
            /* The ARM implementation needs to be written in terms of the CLZ instruction. This can probably be optimized by implementing the whole function in assembly. */
            unsigned int tzcnt32_gcc_arm(unsigned int x)
            {
                unsigned int r;

                if (x == 0) {
                    return sizeof(x) << 3;
                }

                __asm__ __volatile__ (
                #if defined(ARCH_32BIT)
                    "clz %[out], %[in]" : [out]"=r"(r) : [in]"r"(x)
                #else
                    "clz %w[out], %w[in]" : [out]"=r"(r) : [in]"r"(x)
                #endif
                );
                
                return 31 - r;
            }
        #endif
    #endif

    unsigned int tzcnt32_hard(unsigned int x)
    {
        #if defined(ARCH_X86) || defined(ARCH_X64)
            #if defined(_MSC_VER) && !defined(__clang__)
                return tzcnt32_msvc_x86(x);
            #elif defined(__GNUC__) || defined(__clang__)
                return tzcnt32_gcc_x86(x);
            #else
                #error "This compiler does not support the tzcnt intrinsic."
            #endif
        #elif defined(ARCH_ARM)
            #if defined(__GNUC__) || defined(__clang__)
                return tzcnt32_gcc_arm(x);
            #else
                #error "This compiler does not support the clz intrinsic."
            #endif
        #else
            #error "The build target does not support a native instruction."
        #endif
    }
#endif

#if defined(HAS_TZCNT64_HARD)
    #if defined(ARCH_X86) || defined(ARCH_X64)
        #if defined(_MSC_VER) && !defined(__clang__)
            unsigned int tzcnt64_msvc_x64(unsigned long long x)
            {
                if (x == 0) {
                    return sizeof(x) << 3;
                }

                return 63 - (unsigned int)__lzcnt64(x);
            }
        #elif defined(__GNUC__) || defined(__clang__)
            unsigned int tzcnt64_gcc_x64(unsigned long long x)
            {
                /*
                att:   tzcnt [out], [in]
                intel: tzcnt [in], [out]
                */
                unsigned long long r;
                __asm__ __volatile__ (
                    "tzcnt{ %1, %0| %0, %1}" : "=r"(r) : "r"(x)
                );

                return r;
            }
        #endif
    #endif
    #if defined(ARCH_ARM)
        #if defined(__GNUC__) || defined(__clang__)
            /* The ARM implementation needs to be written in terms of the CLZ instruction. This can probably be optimized by implementing the whole function in assembly. */
            unsigned int tzcnt64_gcc_arm(unsigned long long x)
            {
                unsigned long long r;

                if (x == 0) {
                    return sizeof(x) << 3;
                }

                __asm__ __volatile__ (
                    "clz %[out], %[in]" : [out]"=r"(r) : [in]"r"(x)
                );
                
                return 63 - r;
            }
        #endif
    #endif

    unsigned int tzcnt64_hard(unsigned int x)
    {
    #if defined(ARCH_X64)
        #if defined(_MSC_VER) && !defined(__clang__)
            return tzcnt64_msvc_x64(x);
        #elif defined(__GNUC__) || defined(__clang__)
            return tzcnt64_gcc_x64(x);
        #else
            #error "This compiler does not support the tzcnt intrinsic."
        #endif
    #elif defined(ARCH_ARM) && defined(ARCH_64BIT)
        #if defined(__GNUC__) || defined(__clang__)
            return tzcnt64_gcc_arm(x);
        #else
            #error "This compiler does not support the clz intrinsic."
        #endif
    #else
        #error "The build target does not support a native instruction."
    #endif
    }
#endif


unsigned int tzcnt32_soft(unsigned int x)
{
#if defined(_MSC_VER)
    return tzcnt32_msvc_bsf(x);
#elif defined(HAS_TZCNT_INTRINSIC)
    return tzcnt32_gcc_builtin(x);
#else
    return tzcnt32_generic(x);
#endif
}

unsigned int tzcnt64_soft(unsigned int x)
{
#if defined(ARCH_64BIT)
    #if defined(_MSC_VER)
        return tzcnt64_msvc_bsf(x);
    #elif defined(HAS_TZCNT_INTRINSIC)
        return tzcnt64_gcc_builtin(x);
    #else
        return tzcnt64_generic(x);
    #endif
#else
    return tzcnt64_generic(x);
#endif
}


unsigned int tzcnt32(unsigned int x)
{
#if defined(HAS_TZCNT32_HARD)
    if (has_tzcnt_hard()) {
        return tzcnt32_hard(x);
    } else
#endif
    {
        return tzcnt32_soft(x);
    }
}

unsigned int tzcnt64(unsigned int x)
{
#if defined(HAS_TZCNT64_HARD)
    if (has_tzcnt_hard()) {
        return tzcnt64_hard(x);
    } else
#endif
    {
        return tzcnt64_soft(x);
    }
}


#include <stdio.h>
int do_test32(unsigned int (* tzcnt32proc)(unsigned int), const char* procName)
{
    int testResult = 0;
    unsigned int i;
    
    printf("%s: ", procName);
    for (i = 0; i <= 32; ++i) {
        unsigned int x = (0x00000001 << i);
        unsigned int r;
        
        /* Need a special case for the i=32 case because shifting by more than 31 is undefined. */
        if (i == 32) {
            x = 0;
        }
        
        r = tzcnt32proc(x);
        if (r != i) {
            printf("\n  Failed: x=%u r=%u", x, r);
            testResult = -1;
        }
    }
    
    if (testResult == 0) {
        printf("Passed");
    }
    printf("\n");
    
    return testResult;
}

int do_test64(unsigned int (* tzcnt64proc)(unsigned long long), const char* procName)
{
    int testResult = 0;
    unsigned int i;
    
    printf("%s: ", procName);
    for (i = 0; i <= 64; ++i) {
        unsigned long long x = (0x0000000000000001ULL << i);
        unsigned int r;
        
        /* Need a special case for the i=64 case because shifting by more than 63 is undefined. */
        if (i == 64) {
            x = 0;
        }
        
        r = tzcnt64proc(x);
        if (r != i) {
            printf("\n  Failed: x=%llu r=%u", x, r);
            testResult = -1;
        }
    }
    
    if (testResult == 0) {
        printf("Passed");
    }
    printf("\n");
    
    return testResult;
}

int main(int argc, char** argv)
{
    int exitCode = 0;

    /* has_tzcnt_hard() */
#if defined(_WIN64) || defined(_LP64) || defined(__LP64__)
    printf("64 bit\n");
#else
    printf("32 bit\n");
#endif
    printf("has_tzcnt_hard()=%d\n", has_tzcnt_hard());


    /* tzcnt32 */
    
    /* Hardware. */
#if defined(HAS_TZCNT32_HARD)
    if (has_tzcnt_hard()) {
    #if defined(ARCH_X86) || defined(ARCH_X64)
        #if defined(_MSC_VER) && !defined(__clang__)
            if (do_test32(tzcnt32_msvc_x86, "tzcnt32_msvc_x86") != 0) {
                exitCode = -1;
            }
        #elif defined(__GNUC__) || defined(__clang__)
            if (do_test32(tzcnt32_gcc_x86, "tzcnt32_gcc_x86") != 0) {
                exitCode = -1;
            }
        #endif
    #endif
    #if defined(ARCH_ARM)
        #if defined(__GNUC__) || defined(__clang__)
            if (do_test32(tzcnt32_gcc_arm, "tzcnt32_gcc_arm") != 0) {
                exitCode = -1;
            }
        #endif
    #endif
    }
#endif

    /* Software */
#if defined(_MSC_VER)
    if (do_test32(tzcnt32_msvc_bsf, "tzcnt32_msvc_bsf") != 0) {
        exitCode = -1;
    }
#elif defined(HAS_TZCNT_INTRINSIC)
    if (do_test32(tzcnt32_gcc_builtin, "tzcnt32_gcc_builtin") != 0) {
        exitCode = -1;
    }
#endif
    if (do_test32(tzcnt32_generic, "tzcnt32_generic") != 0) {
        exitCode = -1;
    }


    /* tzcnt64 */
    
    /* Hardware. */
#if defined(HAS_TZCNT64_HARD)
    if (has_tzcnt_hard()) {
    #if defined(ARCH_X64)
        #if defined(_MSC_VER) && !defined(__clang__)
            if (do_test64(tzcnt64_msvc_x64, "tzcnt64_msvc_x64") != 0) {
                exitCode = -1;
            }
        #elif defined(__GNUC__) || defined(__clang__)
            if (do_test64(tzcnt64_gcc_x64, "tzcnt64_gcc_x64") != 0) {
                exitCode = -1;
            }
        #endif
    #endif
    #if defined(ARCH_ARM)
        #if defined(__GNUC__) || defined(__clang__)
            if (do_test64(tzcnt64_gcc_arm, "tzcnt64_gcc_arm") != 0) {
                exitCode = -1;
            }
        #endif
    #endif
    }
#endif

    /* Software */
#if defined(ARCH_64BIT)
    #if defined(_MSC_VER)
        if (do_test64(tzcnt64_msvc_bsf, "tzcnt64_msvc_bsf") != 0) {
            exitCode = -1;
        }
    #elif defined(HAS_TZCNT_INTRINSIC)
        if (do_test64(tzcnt64_gcc_builtin, "tzcnt64_gcc_builtin") != 0) {
            exitCode = -1;
        }
    #endif
#endif
    if (do_test64(tzcnt64_generic, "tzcnt64_generic") != 0) {
        exitCode = -1;
    }
    

    (void)argc;
    (void)argv;
    return exitCode;
}
