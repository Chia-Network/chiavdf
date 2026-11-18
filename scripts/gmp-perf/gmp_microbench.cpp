// Microbenchmark of the GMP mpz operations chiavdf leans on, at the operand
// sizes it uses (1024-bit discriminant, ~512-bit form coefficients).
// Built against each GMP variant on Windows (MinGW) and Linux so results can
// be compared directly. Prints ns/op per operation.
//
//   g++ -O2 gmp_microbench.cpp -I$PREFIX/include $PREFIX/lib/libgmp.a
#include <gmp.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

using clk = std::chrono::steady_clock;

static void bench(const char* name, long iters, const std::function<void()>& fn) {
    for (long i = 0; i < iters / 10; ++i) fn();  // warm-up
    auto t0 = clk::now();
    for (long i = 0; i < iters; ++i) fn();
    double ns = std::chrono::duration<double, std::nano>(clk::now() - t0).count();
    std::printf("%-34s %10.1f ns/op\n", name, ns / iters);
}

int main(int argc, char** argv) {
    long scale = argc > 1 ? std::atol(argv[1]) : 1;
    std::printf("GMP %s, CC=%s\nCFLAGS=%s\n", gmp_version, __GMP_CC, __GMP_CFLAGS);
    std::printf("sizeof(long)=%u sizeof(mp_limb_t)=%u\n\n", (unsigned)sizeof(long),
                (unsigned)sizeof(mp_limb_t));

    gmp_randstate_t rs;
    gmp_randinit_default(rs);
    gmp_randseed_ui(rs, 12345);

    mpz_t a512, b512, a1024, b1024, a2048, r, q, s, t, g;
    mpz_inits(a512, b512, a1024, b1024, a2048, r, q, s, t, g, nullptr);
    mpz_urandomb(a512, rs, 512);
    mpz_urandomb(b512, rs, 512);
    mpz_urandomb(a1024, rs, 1024);
    mpz_urandomb(b1024, rs, 1024);
    mpz_urandomb(a2048, rs, 2048);
    mpz_setbit(a512, 511);
    mpz_setbit(b512, 511);
    mpz_setbit(a1024, 1023);
    mpz_setbit(b1024, 1023);
    mpz_setbit(a2048, 2047);
    // Pre-size outputs so steady-state numbers exclude reallocation.
    mpz_realloc2(r, 4096);
    mpz_realloc2(q, 4096);

    const long N = 2000000 * scale;
    bench("mpz_add 1024", N, [&] { mpz_add(r, a1024, b1024); });
    bench("mpz_mul 512x512", N, [&] { mpz_mul(r, a512, b512); });
    bench("mpz_mul 1024x1024", N / 2, [&] { mpz_mul(r, a1024, b1024); });
    bench("mpz_mul 1024x512", N / 2, [&] { mpz_mul(r, a1024, b512); });
    bench("mpz_mul_si 1024 (int32 range)", N, [&] { mpz_mul_si(r, a1024, -123456789); });
    bench("mpz_fdiv_qr 2048/1024", N / 4, [&] { mpz_fdiv_qr(q, r, a2048, b1024); });
    bench("mpz_fdiv_q 1024/512", N / 2, [&] { mpz_fdiv_q(q, a1024, b512); });
    bench("mpz_tdiv_r 1024/512", N / 2, [&] { mpz_tdiv_r(r, a1024, b512); });
    bench("mpz_gcdext 512", N / 20, [&] { mpz_gcdext(g, s, t, a512, b512); });
    bench("mpz_gcdext 1024", N / 40, [&] { mpz_gcdext(g, s, t, a1024, b1024); });
    bench("mpz_gcd 1024", N / 20, [&] { mpz_gcd(g, a1024, b1024); });
    // Allocator-heavy patterns: fresh temporaries each op (malloc/free cost).
    bench("init+mul+clear 512 (alloc churn)", N, [&] {
        mpz_t x;
        mpz_init(x);
        mpz_mul(x, a512, b512);
        mpz_clear(x);
    });
    bench("init+gcdext+clear 512", N / 20, [&] {
        mpz_t gg, ss, tt;
        mpz_inits(gg, ss, tt, nullptr);
        mpz_gcdext(gg, ss, tt, a512, b512);
        mpz_clears(gg, ss, tt, nullptr);
    });
    bench("mpz_set_str/get_str 1024 (hex)", N / 40, [&] {
        char buf[1024];
        mpz_get_str(buf, 16, a1024);
        mpz_set_str(r, buf, 16);
    });

    mpz_clears(a512, b512, a1024, b1024, a2048, r, q, s, t, g, nullptr);
    gmp_randclear(rs);
    return 0;
}
