// Build with -DCHIAVDF_XGCD_REFERENCE_HEADER='"/path/to/frozen/xgcd_partial.c"'.
// Compare every remainder/cofactor to the frozen implementation, including TLS reuse.
#include <gmpxx.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#define mpz_xgcd_partial reference_xgcd_partial
#define chiavdf_mpz_bitlen_nonneg reference_bitlen_nonneg
#define chiavdf_mpz_extract_uword_from_shift_nonneg reference_extract_nonneg
#define chiavdf_clz_u32 reference_clz_u32
#define chiavdf_clz_u64 reference_clz_u64
#include CHIAVDF_XGCD_REFERENCE_HEADER
#undef mpz_xgcd_partial
#undef chiavdf_mpz_bitlen_nonneg
#undef chiavdf_mpz_extract_uword_from_shift_nonneg
#undef chiavdf_clz_u32
#undef chiavdf_clz_u64
#undef _XGCD_PARTIAL
#include "xgcd_partial.c"

struct Input { mpz_class a, b, limit; };
struct Output { mpz_class a, b, co2, co1; };
using Function = void (*)(mpz_ptr, mpz_ptr, mpz_ptr, mpz_ptr, mpz_srcptr);
static void run(Function fn, const Input& in, Output& out) {
    out.a = in.a; out.b = in.b;
    fn(out.co2.get_mpz_t(), out.co1.get_mpz_t(), out.a.get_mpz_t(),
       out.b.get_mpz_t(), in.limit.get_mpz_t());
}
static bool equal(const Output& a, const Output& b) {
    return a.a == b.a && a.b == b.b && a.co1 == b.co1 && a.co2 == b.co2;
}
int main() {
    std::vector<Input> corpus;
    gmp_randclass rng(gmp_randinit_mt); rng.seed(20261012UL);
    for (unsigned n = 0; n < 24000; ++n) {
        unsigned bits = 2 + n % 2047;
        Input v{rng.get_z_bits(bits), rng.get_z_bits(bits), rng.get_z_bits(bits / 2)};
        if (v.a < v.b) std::swap(v.a, v.b);
        if (n % 7 == 0) v.b = 0;
        if (n % 11 == 0) v.b = v.a;
        if (n % 13 == 0) v.limit = 0;
        if (n % 17 == 0) { v.a *= 105; v.b *= 105; }
        if (n % 19 == 0) v.limit = v.b;
        if (n % 23 == 0 && v.b > 0) v.limit = v.b - 1;
        corpus.push_back(std::move(v));
    }
    for (unsigned bits = 1; bits <= 2048; ++bits) {
        mpz_class p = mpz_class(1) << bits;
        for (int delta : {-1, 0, 1})
            corpus.push_back({p + delta, p / 2 + delta, p / 4});
    }
    mpz_class f0 = 1, f1 = 1;
    for (unsigned n = 0; n < 3000; ++n) {
        mpz_class next = f0 + f1; f0 = f1; f1 = next;
        if (n % 3 == 0) corpus.push_back({f1, f0, f1 >> (n % 31)});
    }
    std::atomic<bool> failed{false};
    auto verify = [&] {
        Output old, candidate;
        for (const auto& v : corpus) {
            run(reference_xgcd_partial, v, old); run(mpz_xgcd_partial, v, candidate);
            if (!equal(old, candidate)) { failed = true; break; }
        }
    };
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) workers.emplace_back(verify);
    for (auto& worker : workers) worker.join();
    if (failed) { std::cerr << "Reference equivalence failed\n"; return 1; }
    std::cout << "{\"equivalence_cases\":" << corpus.size() * 4
              << ",\"threads\":4,\"verified\":true}" << std::endl;
    Output result;
    for (int round = 0; round < 8; ++round) {
        for (int slot = 0; slot < 2; ++slot) {
            bool candidate = (slot + round) % 2;
            auto start = std::chrono::steady_clock::now();
            for (const auto& v : corpus)
                run(candidate ? mpz_xgcd_partial : reference_xgcd_partial, v, result);
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            std::cout << "{\"round\":" << round << ",\"candidate\":"
                      << (candidate ? "true" : "false") << ",\"cases\":" << corpus.size()
                      << ",\"elapsed_us\":" << us << "}" << std::endl;
        }
    }
}
