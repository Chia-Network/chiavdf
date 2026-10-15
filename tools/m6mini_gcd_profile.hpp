// Benchmark-only instrumentation: compile m6mini_prover_bench.cpp with
// -include tools/m6mini_gcd_profile.hpp. Timing includes clock-call overhead;
// use ordinary uninstrumented builds for performance comparisons.
#include <gmp.h>
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <map>
#include <mutex>
#include <iostream>
#include <thread>
#include "xgcd_partial.c"
#define CHIAVDF_GCD_PROFILE 1
namespace m6_gcd_profile {
inline std::mutex output_mutex;
inline const auto main_thread = std::this_thread::get_id();
inline thread_local bool enabled = std::this_thread::get_id() != main_thread;
struct Stats {
    uint64_t full_calls = 0, partial_calls = 0, full_ns = 0, partial_ns = 0, coprime = 0;
    std::map<size_t, uint64_t> full_limbs, partial_limbs, limit_limbs;
    ~Stats() {
        std::lock_guard<std::mutex> lock(output_mutex);
        auto histogram = [](const auto& values) {
            std::cerr << '{'; bool first = true;
            for (const auto& [size,count] : values) {
                if (!first) std::cerr << ',';
                first = false; std::cerr << '"' << size << "\":" << count;
            }
            std::cerr << '}';
        };
        std::cerr << "{\"thread\":\"" << std::this_thread::get_id()
                  << "\",\"main_thread\":" << (std::this_thread::get_id() == main_thread ? "true" : "false")
                  << ",\"full_calls\":" << full_calls << ",\"partial_calls\":" << partial_calls
                  << ",\"full_ns\":" << full_ns << ",\"partial_ns\":" << partial_ns
                  << ",\"coprime\":" << coprime << ",\"full_limbs\":";
        histogram(full_limbs); std::cerr << ",\"partial_limbs\":"; histogram(partial_limbs);
        std::cerr << ",\"limit_limbs\":"; histogram(limit_limbs); std::cerr << "}\n";
    }
};
inline thread_local Stats stats;
inline void full(mpz_ptr g, mpz_ptr s, mpz_ptr t, mpz_srcptr a, mpz_srcptr b) {
    if (!enabled) { __gmpz_gcdext(g,s,t,a,b); return; }
    auto& p = stats; ++p.full_calls; ++p.full_limbs[std::max(mpz_size(a),mpz_size(b))];
    auto start = std::chrono::steady_clock::now();
    __gmpz_gcdext(g,s,t,a,b);
    p.full_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-start).count();
    if (mpz_cmp_ui(g,1)==0) ++p.coprime;
}
inline void partial(mpz_ptr co2, mpz_ptr co1, mpz_ptr r2, mpz_ptr r1, mpz_srcptr limit) {
    if (!enabled) { mpz_xgcd_partial(co2,co1,r2,r1,limit); return; }
    auto& p = stats; ++p.partial_calls;
    ++p.partial_limbs[std::max(mpz_size(r2),mpz_size(r1))]; ++p.limit_limbs[mpz_size(limit)];
    auto start = std::chrono::steady_clock::now();
    mpz_xgcd_partial(co2,co1,r2,r1,limit);
    p.partial_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-start).count();
}
}
#undef mpz_gcdext
#define mpz_gcdext m6_gcd_profile::full
#define mpz_xgcd_partial m6_gcd_profile::partial
