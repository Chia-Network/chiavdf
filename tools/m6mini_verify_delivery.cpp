// Verify complete timelord wire proofs after timing; also reject an incorrect iteration count.
// From the checkout root (after building src/lzcnt.o):
// c++ -O3 -flto -std=c++17 -DVDF_MODE=0 -DFAST_MACHINE=1 -DCHIAOSX=1 \
//   -Isrc -I/opt/homebrew/include tools/m6mini_verify_delivery.cpp src/lzcnt.o \
//   -L/opt/homebrew/lib -lgmpxx -lgmp -pthread -o src/verify_delivery
#include "verifier.h"
#include "alloc.hpp"
#include <cfenv>
#include <iostream>
#include <sstream>

static bool verify(const std::string& discriminant, const std::string& hex) {
    if (hex.size() % 2) return false;
    std::vector<uint8_t> wire;
    for (size_t i = 0; i < hex.size(); i += 2)
        wire.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    if (wire.size() < 217 || BytesToInt64(&wire[8]) != BQFC_FORM_SIZE) return false;
    const uint64_t iterations = BytesToInt64(wire.data());
    const uint8_t depth = wire[16 + BQFC_FORM_SIZE];
    if (depth + 1 > 64) return false; // Mainnet MAX_VDF_WITNESS_SIZE.
    if (wire.size() != 17 + 2 * BQFC_FORM_SIZE + depth * (8 + B_bytes + BQFC_FORM_SIZE))
        return false;
    std::vector<uint8_t> blob(wire.begin() + 16, wire.begin() + 16 + BQFC_FORM_SIZE);
    blob.insert(blob.end(), wire.begin() + 17 + BQFC_FORM_SIZE, wire.end());
    integer d(discriminant);
    uint8_t initial[BQFC_FORM_SIZE] = {8};
    if (!CheckProofOfTimeNWesolowski(d, initial, blob.data(), blob.size(), iterations, 1024, depth))
        return false;
    try {
        return !CheckProofOfTimeNWesolowski(d, initial, blob.data(), blob.size(), iterations + 1, 1024, depth);
    } catch (const std::exception&) {
        return true; // Rejecting the incorrect iteration count by exception is also valid.
    }
}

int main() {
    init_gmp();
    fesetround(FE_TOWARDZERO);
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream record(line);
        std::string d, hex;
        record >> d >> hex;
        bool valid = false;
        try { valid = verify(d, hex); } catch (const std::exception&) {}
        std::cout << (valid ? "1" : "0") << '\n';
    }
}
