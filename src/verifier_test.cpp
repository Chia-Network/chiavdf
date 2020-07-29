#include "verifier.h"
#include "create_discriminant.h"

void assertm(bool expr, std::string msg, bool verbose=false) {
    if (expr && verbose) {
        std::cout << "Assertion passed." << std::endl;
    } else if (!expr) {
        std::cout << "Assertion " << msg << " failed." << std::endl;
    }
}

std::vector<uint8_t> HexToBytes(char *hex_proof) {
    int len = strlen(hex_proof);
    assert(len % 2 == 0);
    std::vector<uint8_t> result;
    for (int i = 0; i < len; i += 2)
    {
        int hex1 = hex_proof[i] >= 'a' ? (hex_proof[i] - 'a' + 10) : (hex_proof[i] - '0');
        int hex2 = hex_proof[i + 1] >= 'a' ? (hex_proof[i + 1] - 'a' + 10) : (hex_proof[i + 1] - '0');
        result.push_back(hex1 * 16 + hex2);
    }
    return result;
}

int main()
{
    auto challenge_hash1 = HexToBytes(string("a4bb1461ade74ac602e9ae511af68bb254dfe65d61b7faf9fab82d0b4364a30b").data());
    auto challenge_hash2 = HexToBytes(string("1633f29c0ca0597258507bc7d323a8bd485d5f059da56340a2c616081fb05b7f").data());
    auto challenge_hash3 = HexToBytes(string("6aa2451d1469e1213e50f114a49744f96073fedbe53921c8294a303779baa32d").data());

    // Create Discriminant tests
    for (auto seed: {challenge_hash1, challenge_hash2, challenge_hash3}) {
        integer D = CreateDiscriminant(seed, 1024);
        integer P = D * integer(-1);
        assertm(mpz_tstbit(P.impl, 1023) == 1, "1023-th bit should be set");
        assertm(P < (integer(1) << 1024), "P should be at most 1024 bits");
        assertm(D % integer(8) == integer(1), "D should be 1 mod 8");
        assertm(P.prime(), "P should be psuedoprime");
    }

    return 0;
}
