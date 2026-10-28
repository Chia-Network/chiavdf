#include "include.h"
#include "create_discriminant.h"
#include "integer_common.h"
#include "vdf_new.h"
#include "nucomp.h"
#include "picosha2.h"
#include "proof_common.h"
#include "checked_cast.h"
#include <sys/stat.h>
#include <algorithm>
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
    if (intermediate <= 1.0) {
        // For tiny iteration counts, the asymptotic estimate is undefined
        // (log(log(intermediate)) domain). Keep parameters minimal and valid.
        k = 1;
        return;
    }
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

// Ceiling division that cannot wrap for any uint64 inputs (d must be non-zero).
inline uint64_t CeilDiv(uint64_t n, uint64_t d) { return n / d + (n % d != 0); }

// Folds one round's 2^k buckets into x: the end of each round j of the
// Wesolowski proof. Shared by GenerateWesolowski and ProveSlowLowMemory, so
// both compute the proof with the very same operations.
void FoldWesolowskiBuckets(form& x, std::vector<form> const& ys, uint64_t k,
                           integer& D, integer& L, PulmarkReducer& reducer) {
    uint64_t k1 = k / 2;
    uint64_t k0 = k - k1;
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

form GenerateWesolowski(form &y, form &x_init,
                        integer &D, PulmarkReducer& reducer,
                        std::vector<form> const& intermediates,
                        uint64_t num_iterations,
                        uint64_t k, uint64_t l) {
    integer B = GetB(D, x_init, y);
    integer L=root(-D, 4);

    assert(k > 0);
    assert(l > 0);

    form x = form::identity(D);

    for (int64_t j = l - 1; j >= 0; j--) {
        x = FastPowFormNucomp(x, D, integer(uint64(1) << k), L, reducer);

        std::vector<form> ys((1ULL << k));
        for (uint64_t i = 0; i < (1ULL << k); i++)
            ys[i] = form::identity(D);

        for (uint64_t i = 0; i < CeilDiv(num_iterations, k * l); i++) {
            if (num_iterations >= k * (i * l + j + 1)) {
                uint64_t b = GetBlock(i*l + j, k, num_iterations, B);
                nucomp_form(ys[b], ys[b], intermediates[i], D, L);
            }
        }
        FoldWesolowskiBuckets(x, ys, k, D, L, reducer);
    }

    reducer.reduce(x);
    return x;
}

// True when the shutdown file was given and no longer exists: the prover
// then stops and returns an empty result.
static bool ShutdownRequested(std::string const& shutdown_file_path) {
    if (shutdown_file_path == "") return false;
    struct stat buffer;
    int statrst = stat(shutdown_file_path.c_str(), &buffer);
    return (statrst != 0) && (errno != EINTR);
}

// The k and l that ProveSlow has always chosen for num_iterations.
static void ProverParameters(uint64_t num_iterations, uint64_t& k, uint64_t& l) {
    int k_int, l_int;
    ApproximateParameters(num_iterations, l_int, k_int);
    if (k_int <= 0) k_int = 1;
    if (l_int <= 0) l_int = 1;
    k = static_cast<uint64_t>(k_int);
    l = static_cast<uint64_t>(l_int);
}

// y followed by the proof, as prove() returns them.
static std::vector<uint8_t> SerializeProofResult(form& y, form& proof, int d_bits) {
    std::vector<uint8_t> result = SerializeForm(y, d_bits);
    std::vector<uint8_t> proof_bytes = SerializeForm(proof, d_bits);
    result.insert(result.end(), proof_bytes.begin(), proof_bytes.end());
    return result;
}

// ProveSlow with k and l given instead of chosen by ApproximateParameters.
// The proof verifies for any k, l > 0; tests use this to reach l > 1 with
// few iterations.
std::vector<uint8_t> ProveSlowWithParameters(integer& D, form& x, uint64_t num_iterations,
                                             std::string const& shutdown_file_path,
                                             uint64_t k, uint64_t l) {
    integer L = root(-D, 4);
    PulmarkReducer reducer;
    form y = form::from_abd(x.a, x.b, D);
    int d_bits = D.num_bits();
    uint64_t const kl = k * l;

    uint64_t const size_vec = CeilDiv(num_iterations, static_cast<uint64_t>(kl));
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
        if ((i & 0xffff) == 0 && ShutdownRequested(shutdown_file_path)) return {};
    }

    form proof = GenerateWesolowski(y, x, D, reducer, intermediates, num_iterations, k, l);
    return SerializeProofResult(y, proof, d_bits);
}

std::vector<uint8_t> ProveSlow(integer& D, form& x, uint64_t num_iterations, std::string shutdown_file_path) {
    uint64_t k, l;
    ProverParameters(num_iterations, k, l);
    return ProveSlowWithParameters(D, x, num_iterations, shutdown_file_path, k, l);
}

// ProveSlowLowMemory with k and l given (see ProveSlowWithParameters).
std::vector<uint8_t> ProveSlowLowMemoryWithParameters(integer& D, form& x, uint64_t num_iterations,
                                                      std::string const& shutdown_file_path,
                                                      uint64_t checkpoint_interval,
                                                      uint64_t k, uint64_t l) {
    integer L = root(-D, 4);
    PulmarkReducer reducer;
    form y = form::from_abd(x.a, x.b, D);
    int d_bits = D.num_bits();
    uint64_t const kl = k * l;

    // A multiple of k*l, at least k*l. Rounding down cannot overflow, so a
    // caller-supplied value near 2^64 can never lead to a division by zero.
    if (checkpoint_interval == 0) checkpoint_interval = kl << 16;
    checkpoint_interval = std::max(kl, checkpoint_interval - checkpoint_interval % kl);

    // First pass: y, and a checkpoint every checkpoint_interval iterations.
    std::vector<form> checkpoints;
    checkpoints.reserve(num_iterations / checkpoint_interval + 1);
    for (uint64_t i = 0; i < num_iterations; i++) {
        if (i % checkpoint_interval == 0) checkpoints.push_back(y);
        nudupl_form(y, y, D, L);
        reducer.reduce(y);
        if ((i & 0xffff) == 0 && ShutdownRequested(shutdown_file_path)) return {};
    }

    integer B = GetB(D, x, y);

    // Second pass: the (k*l)-th forms again, into the buckets of every round.
    std::vector<std::vector<form>> ys(l);
    for (uint64_t j = 0; j < l; j++) {
        ys[j].resize(1ULL << k);
        for (uint64_t b = 0; b < (1ULL << k); b++) ys[j][b] = form::identity(D);
    }
    for (uint64_t c = 0; c < checkpoints.size(); c++) {
        form f = checkpoints[c];
        uint64_t const end = std::min(num_iterations, (c + 1) * checkpoint_interval);
        for (uint64_t i = c * checkpoint_interval; i < end; i++) {
            if (i % kl == 0) {
                uint64_t const idx = i / kl;
                for (uint64_t j = 0; j < l; j++) {
                    if (num_iterations >= k * (idx * l + j + 1)) {
                        uint64_t b = GetBlock(idx * l + j, k, num_iterations, B);
                        nucomp_form(ys[j][b], ys[j][b], f, D, L);
                    }
                }
            }
            if (i + 1 < end) {
                nudupl_form(f, f, D, L);
                reducer.reduce(f);
            }
            if ((i & 0xffff) == 0 && ShutdownRequested(shutdown_file_path)) return {};
        }
    }

    // The rounds, last first, as in GenerateWesolowski.
    form proof = form::identity(D);
    for (int64_t j = static_cast<int64_t>(l) - 1; j >= 0; j--) {
        proof = FastPowFormNucomp(proof, D, integer(uint64(1) << k), L, reducer);
        FoldWesolowskiBuckets(proof, ys[j], k, D, L, reducer);
    }
    reducer.reduce(proof);

    return SerializeProofResult(y, proof, d_bits);
}

// The same proof as ProveSlow, byte for byte, with memory that does not grow
// with num_iterations.
//
// ProveSlow keeps every (k*l)-th form of the computation until the proof is
// made: num_iterations / (k*l) forms. Instead, a first pass keeps one
// checkpoint every `checkpoint_interval` iterations (rounded down to a
// multiple of k*l, at least k*l; 0 means k*l * 2^16) and ends at y, which
// gives B. A second pass computes the forms again from the checkpoints and
// adds each (k*l)-th one to the buckets of all l rounds at once, in the order
// GenerateWesolowski does; FoldWesolowskiBuckets then folds the rounds as it
// does. Memory: l * 2^k + num_iterations / checkpoint_interval forms; time:
// about twice ProveSlow's squarings.
std::vector<uint8_t> ProveSlowLowMemory(integer& D, form& x, uint64_t num_iterations,
                                        std::string shutdown_file_path,
                                        uint64_t checkpoint_interval = 0) {
    uint64_t k, l;
    ProverParameters(num_iterations, k, l);
    return ProveSlowLowMemoryWithParameters(D, x, num_iterations, shutdown_file_path,
                                            checkpoint_interval, k, l);
}
