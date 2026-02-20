#include "include.h"
#include "create_discriminant.h"
#include "integer_common.h"
#include "vdf_new.h"
#include "nucomp.h"
#include "picosha2.h"
#include "proof_common.h"
#include "checked_cast.h"
#include <sys/stat.h>
#include <limits>


// TODO: Refactor to use 'Prover' class once new_vdf is merged in.

void ApproximateParameters(uint64_t T, int& l, int& k) {
    auto checked_double_to_int = [](double value) -> int {
        if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<int>::min()) ||
            value > static_cast<double>(std::numeric_limits<int>::max())) {
            throw std::overflow_error("ApproximateParameters: value out of range for int");
        }
        return checked_cast<int>(static_cast<int64_t>(value));
    };

    double log_memory = 23.25349666;
    double log_T = log2(T);
    l = 1;
    if (log_T - log_memory > 0.000001) {
        const double l_estimate = ceil(pow(2, log_memory - 20));
        l = checked_double_to_int(l_estimate);
    }
    double intermediate = T * (double)0.6931471 / (2.0 * l);
    const double k_estimate = std::max(std::round(log(intermediate) - log(log(intermediate)) + 0.25), 1.0);
    k = checked_double_to_int(k_estimate);
}

uint64_t GetBlock(uint64_t i, uint64_t k, uint64_t T, integer& B) {
    integer res = FastPow(2, T - k * (i + 1), B);
    mpz_mul_2exp(res.impl, res.impl, k);
    res = res / B;
    auto res_vector = res.to_vector();
    // 0 value results in empty vector from mpz_export
    // https://gmplib.org/list-archives/gmp-bugs/2009-July/001534.html
    return res_vector.empty() ? 0 : res_vector[0];
}

form GenerateWesolowski(form &y, form &x_init,
                        integer &D, PulmarkReducer& reducer,
                        std::vector<form> const& intermediates,
                        uint64_t num_iterations,
                        uint64_t k, uint64_t l) {
    integer B = GetB(D, x_init, y);
    integer L=root(-D, 4);

    uint64_t k1 = k / 2;
    uint64_t k0 = k - k1;
    assert(k > 0);
    assert(l > 0);

    form x = form::identity(D);

    for (int64_t j = l - 1; j >= 0; j--) {
        x = FastPowFormNucomp(x, D, integer(1 << k), L, reducer);

        std::vector<form> ys((1ULL << k));
        for (uint64_t i = 0; i < (1ULL << k); i++)
            ys[i] = form::identity(D);

        for (uint64_t i = 0; i < (num_iterations + k * l - 1)  / (k * l); i++) {
            if (num_iterations >= k * (i * l + j + 1)) {
                uint64_t b = GetBlock(i*l + j, k, num_iterations, B);
                nucomp_form(ys[b], ys[b], intermediates[i], D, L);
            }
        }
        for (uint64_t b1 = 0; b1 < (1ULL << k1); b1++) {
            form z = form::identity(D);
            for (uint64_t b0 = 0; b0 < (1ULL << k0); b0++) {
                nucomp_form(z, z, ys[b1 * (1ULL << k0) + b0], D, L);
            }
            const uint64 shift_base = (uint64(1) << k0);
            z = FastPowFormNucomp(z, D, integer(static_cast<uint64>(b1) * shift_base), L, reducer);
            nucomp_form(x, x, z, D, L);
        }
        for (uint64_t b0 = 0; b0 < (1ULL << k0); b0++) {
            form z = form::identity(D);
            for (uint64_t b1 = 0; b1 < (1ULL << k1); b1++) {
                nucomp_form(z, z, ys[b1 * (1ULL << k0) + b0], D, L);
            }
            z = FastPowFormNucomp(z, D, integer(b0), L, reducer);
            nucomp_form(x, x, z, D, L);
        }
    }

    reducer.reduce(x);
    return x;
}

std::vector<uint8_t> ProveSlow(integer& D, form& x, uint64_t num_iterations, std::string shutdown_file_path) {
    integer L = root(-D, 4);
    PulmarkReducer reducer;
    form y = form::from_abd(x.a, x.b, D);
    int d_bits = D.num_bits();

    int k, l;
    ApproximateParameters(num_iterations, l, k);
    if (k <= 0) k = 1;
    if (l <= 0) l = 1;
    int const kl = k * l;

    uint64_t const size_vec = (num_iterations + kl - 1) / kl;
    std::vector<form> intermediates(size_vec);
    form* cursor = intermediates.data();
    for (uint64_t i = 0; i < num_iterations; i++) {
        if (i % kl == 0) {
            *cursor = y;
            ++cursor;
        }
        nudupl_form(y, y, D, L);
        reducer.reduce(y);

        // Check for cancellation every 65535 interations
        if ((i&0xffff)==0) {
            // Only if we have a shutdown path
            if (shutdown_file_path!="") {
                struct stat buffer;
            
                int statrst = stat(shutdown_file_path.c_str(), &buffer);
                if ((statrst != 0) && (errno != EINTR)) {
                    // shutdown file doesn't exist, abort out
                    return {};
                }
            }
        }
    }

    form proof = GenerateWesolowski(y, x, D, reducer, intermediates, num_iterations, k, l);
    std::vector<uint8_t> result = SerializeForm(y, d_bits);
    std::vector<uint8_t> proof_bytes = SerializeForm(proof, d_bits);
    result.insert(result.end(), proof_bytes.begin(), proof_bytes.end());
    return result;
}
