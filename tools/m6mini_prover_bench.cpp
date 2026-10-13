// Isolate construction on precomputed, reduced 4000-iteration intermediates.
// Build like m6mini_verify_delivery.cpp, replacing its source and output name.
#include "verifier.h"
#include "prover.hpp"
#include "alloc.hpp"
#include <algorithm>
#include <cfenv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

static constexpr uint64_t interval = 4000;
static std::string hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto b : bytes) out << std::setw(2) << unsigned(b);
    return out.str();
}
static form decode(const integer& d, const std::string& encoded) {
    if (encoded.size() != 200) throw std::runtime_error("Bad fixture form size");
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < encoded.size(); i += 2)
        bytes.push_back(std::stoul(encoded.substr(i, 2), nullptr, 16));
    return DeserializeForm(d, bytes.data(), bytes.size());
}
static std::vector<uint64_t> numbers(const std::string& csv) {
    std::vector<uint64_t> out;
    std::istringstream in(csv);
    std::string word;
    while (std::getline(in, word, ',')) out.push_back(std::stoull(word));
    return out;
}
// Reference to the current hardware heuristic, for interpreting sweep results.
static uint32_t original_k(uint64_t iters) {
    const uint32_t divisors[] = {2,4,5,8,10,16,20};
    uint64_t cost = iters + interval * 4;
    uint32_t last = 1;
    for (auto k : divisors) {
        uint64_t next = iters / k + ((interval / k) << (k + 1));
        if (next > cost) return last;
        cost = next; last = k;
    }
    return last;
}
class FixtureProver : public ParallelProver {
    const std::vector<form>& values;
  public:
    FixtureProver(Segment s, integer d, const std::vector<form>& v, uint32_t selected)
        : ParallelProver(s,d), values(v) {
        k = selected; l = interval / k;
    }
    form GetForm(uint64_t pos) override { return values.at(pos); }
    void start() override { GenerateProof(); }
    void stop() override {}
    bool PerformExtraStep() override { return true; }
    void OnFinish() override { is_finished = true; }
};
int main(int argc, char** argv) {
    try {
        init_gmp(); fesetround(FE_TOWARDZERO);
        if (argc == 6 && std::string(argv[1]) == "prepare") {
            integer d(argv[2]), limit = root(-d, 4);
            uint64_t count = std::stoull(argv[4]);
            if (!count || count % interval) throw std::runtime_error("Fixture maximum must be grid-aligned");
            form f = decode(d, argv[3]); PulmarkReducer reducer;
            std::ofstream out(argv[5]);
            out << d.to_string() << '\n' << count << '\n';
            for (uint64_t i = 0; i <= count; ++i) {
                if (!(i % interval)) out << hex(SerializeForm(f,d.num_bits())) << '\n';
                if (i < count) { nudupl_form(f,f,d,limit); reducer.reduce(f); }
            }
            if (!out) throw std::runtime_error("Fixture write failed");
            return 0;
        }
        if (argc != 6 || std::string(argv[1]) != "run")
            throw std::runtime_error("prepare D INITIAL_HEX MAX_ITERS FILE | run FILE REPS LENGTHS K_VALUES");
        std::ifstream in(argv[2]); std::string discriminant, encoded; uint64_t maximum;
        if (!(in >> discriminant >> maximum)) throw std::runtime_error("Fixture header missing");
        integer d(discriminant), limit = root(-d,4);
        std::vector<form> values;
        while (in >> encoded) values.push_back(decode(d,encoded));
        if (values.size() != maximum / interval + 1) throw std::runtime_error("Incomplete fixture");
        auto lengths = numbers(argv[4]), ks = numbers(argv[5]);
        std::map<uint64_t,std::string> expected;
        unsigned reps = std::stoul(argv[3]);
        for (unsigned rep = 0; rep < reps; ++rep) {
            auto order = ks;
            if (rep % 2) std::reverse(order.begin(),order.end());
            for (auto length : lengths) {
                if (!length || length > maximum) throw std::runtime_error("Length exceeds fixture");
                form x = values[0], y = values[length / interval];
                PulmarkReducer reducer;
                for (uint64_t n = length / interval * interval; n < length; ++n) {
                    nudupl_form(y,y,d,limit); reducer.reduce(y);
                }
                for (auto requested_k : order) {
                    auto selected = requested_k ? requested_k : original_k(length);
                    if (!selected || selected > 10 || interval % selected)
                        throw std::runtime_error("Invalid bucket parameter");
                    FixtureProver prover(Segment(0,length,x,y),d,values,selected);
#if defined(CHIAVDF_GCD_PROFILE)
                    m6_gcd_profile::enabled = true;
#endif
                    auto start = std::chrono::steady_clock::now(); prover.start();
                    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now()-start).count();
#if defined(CHIAVDF_GCD_PROFILE)
                    m6_gcd_profile::enabled = false;
#endif
                    form proof = prover.GetProof(); bool valid = false;
                    VerifyWesolowskiProof(d,x,y,proof,length,valid);
                    if (!valid) throw std::runtime_error("Sweep proof failed verification");
                    auto bytes = hex(SerializeForm(proof,d.num_bits()));
                    if (expected.count(length) && expected[length] != bytes)
                        throw std::runtime_error("Bucket parameters produced different canonical proofs");
                    expected[length] = bytes;
                    std::cout << "{\"round\":" << rep << ",\"length\":" << length
                        << ",\"k\":" << selected << ",\"default_k\":" << original_k(length)
                        << ",\"construction_us\":" << us << ",\"verified\":true,\"proof_hex\":\"" << bytes << "\"}" << std::endl;
                }
            }
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
