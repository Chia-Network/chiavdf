#include "verifier.h"
#include "checked_cast.h"
#include "create_discriminant.h"
#include "c_bindings/c_wrapper.h"
#include "prover_slow.h"

void assertm(bool expr, std::string msg, bool verbose=false) {
    if (expr && verbose) {
        std::cout << "Assertion passed." << std::endl;
    } else if (!expr) {
        std::cout << "Assertion " << msg << " failed." << std::endl;
    }
}

std::vector<uint8_t> HexToBytes(const char *hex_proof) {
    size_t len = strlen(hex_proof);
    assert(len % 2 == 0);
    std::vector<uint8_t> result;
    for (size_t i = 0; i < len; i += 2)
    {
        int hex1 = hex_proof[i] >= 'a' ? (hex_proof[i] - 'a' + 10) : (hex_proof[i] - '0');
        int hex2 = hex_proof[i + 1] >= 'a' ? (hex_proof[i + 1] - 'a' + 10) : (hex_proof[i + 1] - '0');
        result.push_back(hex1 * 16 + hex2);
    }
    return result;
}

ByteArray prove_wrapper(const uint8_t* challenge_hash, size_t challenge_size, const uint8_t* x_s, size_t x_s_size, size_t discriminant_size_bits, uint64_t num_iterations) {
    try {
        std::vector<uint8_t> challenge_hash_bytes(challenge_hash, challenge_hash + challenge_size);
        integer discriminant = CreateDiscriminant(challenge_hash_bytes, checked_cast<int>(discriminant_size_bits));
        form x = DeserializeForm(discriminant, x_s, x_s_size);
        std::vector<uint8_t> result = ProveSlow(discriminant, x, num_iterations, "");

        // Allocate memory for the result and copy data
        uint8_t* resultData = new uint8_t[result.size()];
        std::copy(result.begin(), result.end(), resultData);

        return ByteArray  { resultData, result.size() };
    } catch (...) {
        return ByteArray { nullptr, 0 };
    }
}

int main()
{
    // Test overflow for slow prover (part of challenge b'\xa6\xc4%X\x17O\xb1\xee\xdcd')
    int l,k;
    ApproximateParameters(90909, l, k);

    cout << "ApproximateParameters for 90909 l: " << l << " k: " << k << endl;

    std::vector<uint8_t> challenge_hash=HexToBytes("a6c42558174fb1eedc64");
    std::vector<uint8_t> x_s=HexToBytes("0300aca4849458af5c557710c80f21519f196907764d2d55c9b70581a90d49ca7b3201ad6a9da836429e6592c200e965434f0100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");

    ByteArray ba=prove_wrapper(challenge_hash.data(), challenge_hash.size(), x_s.data(), x_s.size(), 512, 90909);

    for (size_t i = 0; i < ba.length; i++)
        printf( "%02x", ba.data[i]);
    printf("\n");

    delete[] ba.data;

    // Keep a fixed known-proof check using a current-format fixture from src/vdf.txt.
    auto known_challenge = HexToBytes("9104c5b5e45d48f374efa0488fe6a617790e9aecb3c9cddec06809b09f45ce9b");
    auto known_x = HexToBytes("08000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    auto known_proof_blob = HexToBytes("0200553bf0f382fc65a94f20afad5dbce2c1ee8ba3bf93053559ac9960c8fd80ac2222e9b649701a4141a4d8999f0dbfe0c39ea744096598a7528328e5199f0aa30aec8aae8ab5018bf1245329a8272ddff1afbd87ad2eaba1b7fd57bd25edc62e0b010000003f0ffcd0dc307a2aa4678bafba661c77d176ef23afc86e7ea9f4f9eac52b8e1850748019245ecc96547da9b731dc72cded5582a9b0c63e13fd42446c7b28b41d3ded1d0b666d5ddb5b29719e4ebe70969e67e42ddd8591eae60d83dbe619f1250400");
    integer known_discriminant = CreateDiscriminant(known_challenge, 1024);
    assertm(CheckProofOfTimeNWesolowski(
            known_discriminant,
            known_x.data(),
            known_proof_blob.data(),
            known_proof_blob.size(),
            129499136,
            1024,
            0), "Known proof should verify");

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
