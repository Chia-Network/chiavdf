#include "verifier.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    EXPECT_EQ(hex.size() % 2, 0U);
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

integer get_fixture_discriminant() {
    auto challenge = hex_to_bytes("9104c5b5e45d48f374efa0488fe6a617790e9aecb3c9cddec06809b09f45ce9b");
    return CreateDiscriminant(challenge, 1024);
}

std::vector<uint8_t> get_fixture_form_bytes() {
    auto proof_blob = hex_to_bytes(
        "0200553bf0f382fc65a94f20afad5dbce2c1ee8ba3bf93053559ac9960c8fd80ac2222e9b649701a4141a4d8999f0dbfe0c39ea744096598a7528328e5199f0aa30aec8aae8ab5018bf1245329a8272ddff1afbd87ad2eaba1b7fd57bd25edc62e0b010000003f0ffcd0dc307a2aa4678bafba661c77d176ef23afc86e7ea9f4f9eac52b8e1850748019245ecc96547da9b731dc72cded5582a9b0c63e13fd42446c7b28b41d3ded1d0b666d5ddb5b29719e4ebe70969e67e42ddd8591eae60d83dbe619f1250400");
    EXPECT_GE(proof_blob.size(), static_cast<size_t>(BQFC_FORM_SIZE));
    return std::vector<uint8_t>(proof_blob.begin(), proof_blob.begin() + BQFC_FORM_SIZE);
}

}  // namespace

TEST(ProofDeserializationRegressionTest, RejectsWrongSerializedSize) {
    integer d = get_fixture_discriminant();
    std::vector<uint8_t> too_short(BQFC_FORM_SIZE - 1, 0);
    EXPECT_THROW((void)DeserializeForm(d, too_short.data(), too_short.size()), std::runtime_error);
}

TEST(ProofDeserializationRegressionTest, RejectsInvalidCompressedBytesWithoutCrash) {
    integer d = get_fixture_discriminant();
    std::vector<uint8_t> malformed(BQFC_FORM_SIZE, 0);
    malformed[0] = 0x00;
    malformed[1] = 0xFF;  // Impossible g_size for d_bits=1024.
    EXPECT_THROW((void)DeserializeForm(d, malformed.data(), malformed.size()), std::runtime_error);
}

TEST(ProofDeserializationRegressionTest, RejectsNonCanonicalEncoding) {
    integer d = get_fixture_discriminant();
    std::vector<uint8_t> canonical = get_fixture_form_bytes();
    EXPECT_NO_THROW((void)DeserializeForm(d, canonical.data(), canonical.size()));

    canonical[BQFC_FORM_SIZE - 1] ^= 0x01;
    EXPECT_THROW((void)DeserializeForm(d, canonical.data(), canonical.size()), std::runtime_error);
}

// Regression test for b0 malleability: for a form with g=2 (g_size=0), the
// b0 field at byte 99 admits multiple self-consistent encodings b0, b0+4,
// b0+8, … that decode to class-equivalent (but not equal) forms.  All values
// other than the canonical b0 must be rejected.
//
// Vector: Chia mainnet block height 309155, CC infusion-point VDF,
// 1024-bit discriminant.  Original b0 = 0x01; XOR 0x04 gives b0 = 0x05
// (same mod-4 residue → would previously pass bqfc_verify_canon).
TEST(ProofDeserializationRegressionTest, RejectsInflatedB0Field) {
    // discriminant for mainnet block 309155 CC-IP VDF (1024 bits)
    integer d("-146212091130374364448271598629912687111631974722846603227183769906935970876483871782840562162445571052154480975719448767769767557905129461524079902394315542354994269060181795718055043487735056120915916768273200138311940357886024014124174476991145983171370265799623472241486347111977874193600694306566545523111");

    // canonical y-form bytes (g=2, g_size=0, b0=0x01 at byte 99)
    auto canonical = hex_to_bytes(
        "0300d8262c430e78e7c06cf60c9b2049968f604f3b506a85bfe4fff319f8176760"
        "e06cab8ab45524458bf558101f9b4ce8c23cc1e053263272b808b76c6f26493a11"
        "3b62ded5707b28d9eedc0503ac2efcd32be670726725be0fa7ea01f0ef3f602502"
        "01");
    ASSERT_EQ(canonical.size(), static_cast<size_t>(BQFC_FORM_SIZE));

    // canonical encoding must be accepted
    EXPECT_NO_THROW((void)DeserializeForm(d, canonical.data(), canonical.size()));

    // b0 ^= 0x04  →  b0 = 0x05 (same mod-4 residue, inflated by 4)
    // decodes to a class-equivalent but non-reduced form; must be rejected
    std::vector<uint8_t> mutated = canonical;
    mutated[99] ^= 0x04;
    EXPECT_THROW((void)DeserializeForm(d, mutated.data(), mutated.size()),
                 std::runtime_error);

    // b0 ^= 0x08  →  b0 = 0x09 (inflated by 8); also must be rejected
    mutated = canonical;
    mutated[99] ^= 0x08;
    EXPECT_THROW((void)DeserializeForm(d, mutated.data(), mutated.size()),
                 std::runtime_error);
}
