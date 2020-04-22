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
    std::vector<uint8_t> witness1({0, 8, 86, 132, 155, 87, 219, 218, 14, 202, 164, 217, 122, 142, 27, 16, 121, 232, 117, 140, 16, 174, 148, 100, 147, 111, 23, 160, 140, 165, 193, 234, 0, 193, 197, 128, 84, 119, 187, 207, 130, 191, 250, 131, 104, 38, 81, 43, 78, 69, 243, 1, 203, 46, 182, 156, 155, 126, 78, 153, 42, 174, 211, 89, 241, 255, 253, 159, 221, 187, 124, 45, 162, 41, 169, 146, 109, 119, 245, 159, 41, 200, 182, 68, 239, 159, 88, 90, 201, 85, 181, 41, 220, 47, 29, 35, 221, 133, 216, 15, 92, 211, 179, 116, 96, 91, 99, 235, 246, 139, 117, 198, 99, 81, 171, 150, 25, 108, 82, 124, 65, 169, 204, 24, 57, 141, 79, 190, 159, 143});
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
