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
