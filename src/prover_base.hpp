#ifndef PROVER_BASE_H
#define PROVER_BASE_H

#include <atomic>

#include "proof_common.h"
#include "util.h"

class Prover {
  public:
    Prover(Segment segm, integer D) {
        this->segm = segm;
        this->D = D;
        this->num_iterations = segm.length;
        is_finished = false;
    }

    virtual form* GetForm(uint64_t iteration) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool PerformExtraStep() = 0;
    virtual void OnFinish() = 0;

    bool IsFinished() {
        return is_finished;
    }

    form GetProof() {
        return proof;
    }

    uint64_t GetBlock(uint64_t i, uint64_t k, uint64_t T, integer& B) {
        integer res = FastPow(2, T - k * (i + 1), B);
        // Faster than `to_vector()`/mpz_export for this hot mapping.
        mpz_mul_2exp(res.impl, res.impl, k);
        mpz_fdiv_q(res.impl, res.impl, B.impl);
        return mpz_get_ui(res.impl);
    }

    void GenerateProof() {
        PulmarkReducer reducer;

        integer B = GetB(D, segm.x, segm.y);
        integer L=root(-D, 4);

        // Optimize `GetBlock(p)` calls in the hot loop:
        // For fixed (k,l,T,B), the mapping uses `r_p = 2^(T-k*(p+1)) mod B` and returns
        // `b_p = floor((r_p * 2^k) / B)`. When p increases by `l`, the exponent decreases by `k*l`,
        // so we can update `r` by multiplying by inv(2^(k*l)) mod B instead of calling `FastPow`.
        bool getblock_opt_ok = false;
        integer getblock_inv_2kl;
        integer getblock_r;
        integer getblock_tmp;
        const uint64_t k_u64 = static_cast<uint64_t>(k);
        const uint64_t l_u64 = static_cast<uint64_t>(l);
        const uint64_t kl_u64 = k_u64 * l_u64;
        if (k_u64 != 0 && l_u64 != 0 && num_iterations >= k_u64) {
            integer two_kl_mod = FastPow(2, kl_u64, B);
            if (mpz_invert(getblock_inv_2kl.impl, two_kl_mod.impl, B.impl) != 0) {
                getblock_opt_ok = true;
            }
        }

        form id;
        try {
            id = form::identity(D);
        } catch(std::exception& e) {
            std::cout << "Warning: Could not create identity: " << e.what() << "\n";
            std::cout << "Discriminant: " << D.impl << "\n";
            std::cout << "Segment start:" << segm.start << "\n";
            std::cout << "Segment length:" << segm.length << "\n";
            std::cout << std::flush;

            return ;
        }
        uint64_t k1 = k / 2;
        uint64_t k0 = k - k1;
        form x = id;

        const size_t num_buckets = static_cast<size_t>(1ULL << static_cast<uint64_t>(k));
        std::vector<form> ys(num_buckets);
        std::vector<uint32_t> ys_gen(num_buckets, 0);
        uint32_t cur_gen = 1;

        auto ensure_bucket_initialized = [&](size_t idx) {
            if (ys_gen[idx] != cur_gen) {
                ys[idx] = id;
                ys_gen[idx] = cur_gen;
            }
        };

        auto maybe_mul_bucket = [&](form& out, size_t idx) {
            // Multiplying by identity is a no-op; skip buckets that were never written this generation.
            if (ys_gen[idx] == cur_gen) {
                nucomp_form(out, out, ys[idx], D, L);
            }
        };

        for (int64_t j = l - 1; j >= 0; j--) {
            x = FastPowFormNucomp(x, D, integer(1 << k), L, reducer);

            // New generation: treat all buckets as identity until written.
            // Avoids reinitializing `ys` every time (expensive on ARM).
            if (cur_gen == 0) {
                std::fill(ys_gen.begin(), ys_gen.end(), 0);
                cur_gen = 1;
            } else {
                ++cur_gen;
                if (cur_gen == 0) {
                    std::fill(ys_gen.begin(), ys_gen.end(), 0);
                    cur_gen = 1;
                }
            }

            form *tmp;
            uint64_t limit = num_iterations / (k * l);
            if (num_iterations % (k * l))
                limit++;

            // Initialize `r` for p=j (i=0) once per j.
            if (getblock_opt_ok) {
                const uint64_t j_u64 = static_cast<uint64_t>(j);
                if (num_iterations >= k_u64 * (j_u64 + 1)) {
                    getblock_r = FastPow(2, num_iterations - k_u64 * (j_u64 + 1), B);
                } else {
                    // No valid p for this j; inner loop will exit immediately.
                    getblock_r = integer(0);
                }
            }

            for (uint64_t i = 0; i < limit; i++) {
                const uint64_t p = i * l_u64 + static_cast<uint64_t>(j);
                const unsigned __int128 needed = static_cast<unsigned __int128>(k_u64) * (static_cast<unsigned __int128>(p) + 1);
                if (needed > static_cast<unsigned __int128>(num_iterations)) {
                    break;
                }

                uint64_t b;
                if (getblock_opt_ok && num_iterations >= k_u64) {
                    mpz_mul_2exp(getblock_tmp.impl, getblock_r.impl, k_u64);
                    mpz_fdiv_q(getblock_tmp.impl, getblock_tmp.impl, B.impl);
                    b = mpz_get_ui(getblock_tmp.impl);

                    // Advance by `l`: p := p + l, exponent decreases by k*l.
                    mpz_mul(getblock_r.impl, getblock_r.impl, getblock_inv_2kl.impl);
                    mpz_mod(getblock_r.impl, getblock_r.impl, B.impl);
                } else {
                    b = GetBlock(p, k_u64, num_iterations, B);
                }

                if (b >= (1ULL << k_u64)) {
                    // Defensive: if mapping ever produces out-of-range, fall back.
                    b = GetBlock(p, k_u64, num_iterations, B);
                }

                {
                    if (!PerformExtraStep()) return;
                    tmp = GetForm(i);
                    ensure_bucket_initialized(static_cast<size_t>(b));
                    nucomp_form(ys[b], ys[b], *tmp, D, L);
                }
            }

            for (uint64_t b1 = 0; b1 < (1UL << k1); b1++) {
                form z = id;
                for (uint64_t b0 = 0; b0 < (1UL << k0); b0++) {
                    if (!PerformExtraStep()) return;
                    const size_t idx = static_cast<size_t>(b1) * static_cast<size_t>(1ULL << k0) + static_cast<size_t>(b0);
                    maybe_mul_bucket(z, idx);
                }
                z = FastPowFormNucomp(z, D, integer(b1 * (1 << k0)), L, reducer);
                nucomp_form(x, x, z, D, L);
            }

            for (uint64_t b0 = 0; b0 < (1UL << k0); b0++) {
                form z = id;
                for (uint64_t b1 = 0; b1 < (1UL << k1); b1++) {
                    if (!PerformExtraStep()) return;
                    const size_t idx = static_cast<size_t>(b1) * static_cast<size_t>(1ULL << k0) + static_cast<size_t>(b0);
                    maybe_mul_bucket(z, idx);
                }
                z = FastPowFormNucomp(z, D, integer(b0), L, reducer);
                nucomp_form(x, x, z, D, L);
            }
        }
        reducer.reduce(x);
        proof = x;
        OnFinish();
    }

  protected:
    Segment segm;
    integer D;
    form proof;
    uint64_t num_iterations;
    uint32_t k;
    uint32_t l;
    std::atomic<bool> is_finished;
};
#endif // PROVER_BASE_H
