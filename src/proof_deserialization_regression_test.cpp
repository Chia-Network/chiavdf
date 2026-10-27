#include "verifier.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

std::vector<uint8_t> hex_to_bytes(const std::string &hex) {
    EXPECT_EQ(hex.size() % 2, 0U);
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

integer get_fixture_discriminant() {
    auto challenge =
        hex_to_bytes("9104c5b5e45d48f374efa0488fe6a617790e9aecb3c9cddec06809b09f45ce9b");
    return CreateDiscriminant(challenge, 1024);
}

std::vector<uint8_t> get_fixture_form_bytes() {
    auto proof_blob = hex_to_bytes(
        "0200553bf0f382fc65a94f20afad5dbce2c1ee8ba3bf93053559ac9960c8fd80ac2222e9b649701a4141a4d899"
        "9f0dbfe0c39ea744096598a7528328e5199f0aa30aec8aae8ab5018bf1245329a8272ddff1afbd87ad2eaba1b7"
        "fd57bd25edc62e0b010000003f0ffcd0dc307a2aa4678bafba661c77d176ef23afc86e7ea9f4f9eac52b8e1850"
        "748019245ecc96547da9b731dc72cded5582a9b0c63e13fd42446c7b28b41d3ded1d0b666d5ddb5b29719e4ebe"
        "70969e67e42ddd8591eae60d83dbe619f1250400");
    EXPECT_GE(proof_blob.size(), static_cast<size_t>(BQFC_FORM_SIZE));
    return std::vector<uint8_t>(proof_blob.begin(), proof_blob.begin() + BQFC_FORM_SIZE);
}

} // namespace

TEST(ProofDeserializationRegressionTest, RejectsWrongSerializedSize) {
    integer d = get_fixture_discriminant();
    std::vector<uint8_t> too_short(BQFC_FORM_SIZE - 1, 0);
    EXPECT_THROW((void)DeserializeForm(d, too_short.data(), too_short.size()), std::runtime_error);
}

TEST(ProofDeserializationRegressionTest, RejectsInvalidCompressedBytesWithoutCrash) {
    integer d = get_fixture_discriminant();
    std::vector<uint8_t> malformed(BQFC_FORM_SIZE, 0);
    malformed[0] = 0x00;
    malformed[1] = 0xFF; // Impossible g_size for d_bits=1024.
    EXPECT_THROW((void)DeserializeForm(d, malformed.data(), malformed.size()), std::runtime_error);
}

TEST(ProofDeserializationRegressionTest, RejectsNonCanonicalEncoding) {
    integer d = get_fixture_discriminant();
    std::vector<uint8_t> canonical = get_fixture_form_bytes();
    EXPECT_NO_THROW((void)DeserializeForm(d, canonical.data(), canonical.size()));

    canonical[BQFC_FORM_SIZE - 1] ^= 0x01;
    EXPECT_THROW((void)DeserializeForm(d, canonical.data(), canonical.size()), std::runtime_error);
}

static integer get_alternate_discriminant() {
    return integer("-146212091130374364448271598629912687111631974722846603227183769906935970876483"
                   "8717828405621624455710521544809757194487677697675579051294615240799023943155423"
                   "5499426906018179571805504348773505612091591676827320013831194035788602401412417"
                   "4476991145983171370265799623472241486347111977874193600694306566545523111");
}

static std::vector<uint8_t> get_alternate_form_bytes() {
    return hex_to_bytes("0300d8262c430e78e7c06cf60c9b2049968f604f3b506a85bfe4fff319f8176760"
                        "e06cab8ab45524458bf558101f9b4ce8c23cc1e053263272b808b76c6f26493a11"
                        "3b62ded5707b28d9eedc0503ac2efcd32be670726725be0fa7ea01f0ef3f602502"
                        "01");
}

class SerializedFormTest : public ::testing::TestWithParam<bool> {};

TEST_P(SerializedFormTest, AcceptsValidForm) {
    for (const auto &entry :
         {std::make_pair(get_fixture_discriminant(), get_fixture_form_bytes()),
          std::make_pair(get_alternate_discriminant(), get_alternate_form_bytes())}) {
        integer a, b;
        EXPECT_EQ(bqfc_deserialize(a.impl, b.impl, entry.first.impl, entry.second.data(),
                                   entry.second.size(), entry.first.num_bits(), GetParam()),
                  0);
        EXPECT_NO_THROW((void)DeserializeForm(entry.first, entry.second.data(), entry.second.size(),
                                              GetParam()));
    }
    integer d(-23), a, b;
    for (uint8_t first : {uint8_t(4), uint8_t(8)}) {
        std::vector<uint8_t> bytes(BQFC_FORM_SIZE, 0);
        bytes[0] = first;
        EXPECT_EQ(bqfc_deserialize(a.impl, b.impl, d.impl, bytes.data(), bytes.size(), d.num_bits(),
                                   GetParam()),
                  0);
        EXPECT_NO_THROW((void)DeserializeForm(d, bytes.data(), bytes.size(), GetParam()));
    }
    for (const auto &entry :
         {std::make_tuple(-15, 2, 1), std::make_tuple(-3, 1, 1), std::make_tuple(-23, 2, -1)}) {
        integer disc(std::get<0>(entry)), a(std::get<1>(entry)), b(std::get<2>(entry));
        std::vector<uint8_t> bytes(BQFC_FORM_SIZE, 0);
        ASSERT_EQ(bqfc_serialize(bytes.data(), a.impl, b.impl, disc.num_bits()), 0);
        integer decoded_a, decoded_b;
        EXPECT_EQ(bqfc_deserialize(decoded_a.impl, decoded_b.impl, disc.impl, bytes.data(),
                                   bytes.size(), disc.num_bits(), GetParam()),
                  0);
        EXPECT_NO_THROW((void)DeserializeForm(disc, bytes.data(), bytes.size(), GetParam()));
    }
}

TEST_P(SerializedFormTest, RejectsInvalidForm) {
    auto reject = [&](const integer &d, const std::vector<uint8_t> &bytes) {
        integer a, b;
        EXPECT_NE(bqfc_deserialize(a.impl, b.impl, d.impl, bytes.data(), bytes.size(), d.num_bits(),
                                   GetParam()),
                  0);
        EXPECT_THROW((void)DeserializeForm(d, bytes.data(), bytes.size(), GetParam()),
                     std::runtime_error);
    };
    const auto alternate = get_alternate_form_bytes();
    const auto d = get_alternate_discriminant();
    for (uint8_t value : {uint8_t(4), uint8_t(8)}) {
        auto bytes = alternate;
        bytes.back() ^= value;
        reject(d, bytes);
    }
    integer small_d(-23);
    for (uint8_t first : {uint8_t(4), uint8_t(8)}) {
        std::vector<uint8_t> original(BQFC_FORM_SIZE, 0);
        original[0] = first;
        for (size_t index = 1; index < original.size(); ++index) {
            auto bytes = original;
            bytes[index] = 1;
            reject(small_d, bytes);
        }
        for (unsigned bit = 0; bit < 8; ++bit) {
            auto bytes = original;
            bytes[0] ^= uint8_t(1u << bit);
            reject(small_d, bytes);
        }
    }
    for (const auto &entry : {std::make_tuple(-23, 3, 1), std::make_tuple(-23, 1, -1),
                              std::make_tuple(-15, 2, -1), std::make_tuple(-27, 2, 1)}) {
        integer disc(std::get<0>(entry)), a(std::get<1>(entry)), b(std::get<2>(entry));
        std::vector<uint8_t> bytes(BQFC_FORM_SIZE, 0);
        ASSERT_EQ(bqfc_serialize(bytes.data(), a.impl, b.impl, disc.num_bits()), 0);
        reject(disc, bytes);
    }
    integer a, b;
    EXPECT_NE(bqfc_deserialize(a.impl, b.impl, small_d.impl, nullptr, BQFC_FORM_SIZE,
                               small_d.num_bits(), GetParam()),
              0);
}

INSTANTIATE_TEST_SUITE_P(Deserialization, SerializedFormTest, ::testing::Values(false, true));
