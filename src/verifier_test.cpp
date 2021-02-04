#include "verifier.h"
#include "create_discriminant.h"

void assertm(bool expr, std::string msg, bool verbose=false) {
    if (expr && verbose) {
        std::cout << "Assertion passed." << std::endl;
    } else if (!expr) {
        std::cout << "Assertion " << msg << " failed." << std::endl;
    }
}

std::vector<uint8_t> HexToBytes(const char *hex_proof) {
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
	uint8_t arr[10000];
	std::vector<uint8_t> result=HexToBytes("003f360be667de706fe886f766fe20240de04fe2c2f91207f1bbdddf20c554ab8d168b2ce9664d75f4613375a0ab12bf8158983574c9f5cd61c6b8a905fd3fa6bbffc5401b4ccedbe093b560293263a226e46302e720726586251116bc689ef09dc70d99e0a090c4409f928e218e85032fdbee02fedd563073be555b75a70a2d6a430033bc7a4926e3504e87698a0ace0dee6364cced2e9142b4e4cbe55a6371aab41e501ceed21d79d3a0dbbd82ce913c5de40b13eb7c59b1b52b6ef270ee603bd5e7fffcc9f5fae6dbd5aeec394181af130c0fdd195b22be745449b7a584ac80fc75ed49acfdb4d650f5cd344f86377ebbbaef5b19a0af3ae08101d1697f5656a52193000000000071c6f40024c342868a0c2a201b1b26a5d52c5d2f92a106c19ff926deb3fba1e74a444ecee3f8f507c062b949a2eaadd442b049417f82e8811526fa83c6d099d75323e068ffeca9dcd163761000c65d21dede72787ac350f25bdd3d29db6e9cb0e22c8124c724db33660c88784e2871b62ecf816846db7b469c71cad9a5dcfc5548ed2dd781006fa15b968facf4d79219646267eb187a670306d1ff1a59fc28ae00d36bb5a1cba659f48aa64a9022711a66105ef14401ff3948add265240aaad329ee76ba4c2300496746b86bcccacff5947c3fcb956cde2cffae10435960d7097f989aac742cf1047887f11584d20297958385e1715fe0f9b69141750c20d8134420eafec68fd10000000001555540006958aabfe4cc5d870e61fef82bcf1f2c3859e2bd8b1177e8a8872376b5cabace5dcb59b6fecada7e522d05f6f0e352939a6bfdf8c454fbe822cfa5ce97d17be0ffde44a4812cde9d04ec5c08dce6f9146586fdc8e081e05ec690b7effe24ea756f3d300f361203b61e1a39220c6eafa7852842674e317dcae5549c78c7144296ff004a6d0d2854c55e4c1de2f17dc4480b81652cfec37124ef41560a28c853482732434d1c006763b2e341528ae0bcc29fb76f1a4dafd99ade4fd75ec9cc9ca3f3d7001bcb6eb71e43eb22169ab721637551a8ec93838eb0825e9ecba9175297a00b146e9fdd244c5b722f29d3c46ec38840ba18f1f06ddec3dea844867386c2e1ac95");
std::copy(result.begin(), result.end(), arr);

        bool is_valid = CheckProofOfTimeNWesolowski(
            integer("-131653324254138636653163861414331698305531090221496467927360326686715180966094250598321899621249972220387687148397451395672779897144571112116763666653213748473909547482437246405018707472153290116227072825447643324530509016778432769802300913461285128339119844239772697652504835780459732685000796733645621728639"),
            DEFAULT_ELEMENT,
            arr,
            result.size(),
            33554432,
            1024,
            2);

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
