#include "verifier.h"
#include "create_discriminant.h"

void assertm(bool expr, std::string msg, bool verbose=false) {
    if (expr && verbose) {
        std::cout << "Assertion passed." << std::endl;
    } else if (!expr) {
        std::cout << "Assertion " << msg << " failed." << std::endl;
    }
}

int main()
{
    auto challenge_hash1 = HexToBytes(string("a4bb1461ade74ac602e9ae511af68bb254dfe65d61b7faf9fab82d0b4364a30b").data());
    auto challenge_hash2 = HexToBytes(string("1633f29c0ca0597258507bc7d323a8bd485d5f059da56340a2c616081fb05b7f").data());
    auto challenge_hash3 = HexToBytes(string("6aa2451d1469e1213e50f114a49744f96073fedbe53921c8294a303779baa32d").data());
    std::vector<uint8_t> witness1({0, 55, 255, 221, 221, 132, 45, 21, 39, 100, 172, 47, 35, 184, 239, 219, 233, 191, 234, 149, 59, 122, 230, 108, 4, 49, 175, 225, 216, 54, 63, 151, 97, 120, 55, 251, 235, 89, 153, 68, 152, 187, 57, 64, 216, 193, 17, 89, 116, 62, 11, 191, 213, 168, 84, 161, 26, 198, 247, 249, 87, 144, 232, 124, 52, 255, 217, 252, 59, 230, 47, 71, 149, 156, 107, 205, 233, 64, 213, 141, 127, 203, 246, 28, 242, 57, 172, 146, 178, 73, 27, 205, 128, 90, 105, 34, 10, 76, 145, 176, 58, 34, 37, 50, 143, 96, 191, 52, 177, 163, 168, 230, 62, 174, 14, 117, 33, 225, 90, 4, 1, 68, 149, 234, 153, 2, 242, 169, 55, 195});
    integer A("129392441642625789887524274893732839124623753968475426991607715550898925107401519646387196500104727753215068212087229898741148154329866232520504900621326");
    integer B("-16851112157693805756687431026931138278065906490623740906983506462979144983441261648159113047576418550440588178771786333612502853511426689500318791981943");

    ProofOfTimeType test1(
        /*discriminant_size_bits=*/1024,
        /*discriminant=*/challenge_hash1,
        /*a=*/A,
        /*b=*/B,
        /*iterations_needed=*/12345,
        /*witness=*/witness1,
        /*witness_type=*/0
    );
    assertm(CheckProofOfTimeType(test1), "Check 0 recursion wesolowski proof", true);

    ProofOfTimeType test2(
        /*discriminant_size_bits=*/1024,
        /*discriminant=*/challenge_hash1,
        /*a=*/A,
        /*b=*/B,
        /*iterations_needed=*/12346,
        /*witness=*/witness1,
        /*witness_type=*/0
    );
    assertm(!CheckProofOfTimeType(test2), "Check iters wrong by 1", true);

    // Modify correct proof by 1 byte.

    witness1[witness1.size() - 1]++;
    ProofOfTimeType test3(
        /*discriminant_size_bits=*/1024,
        /*discriminant=*/challenge_hash1,
        /*a=*/A,
        /*b=*/B,
        /*iterations_needed=*/12345,
        /*witness=*/witness1,
        /*witness_type=*/0
    );
    assertm(!CheckProofOfTimeType(test3), "Check witness wrong 1 byte", true);


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
