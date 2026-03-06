#include "verifier.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> db_hex_to_bytes(const std::string& hex) {
    EXPECT_EQ(hex.size() % 2, 0U);
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

std::vector<uint8_t> db_get_fixture_challenge() {
    return db_hex_to_bytes("9104c5b5e45d48f374efa0488fe6a617790e9aecb3c9cddec06809b09f45ce9b");
}

std::vector<uint8_t> db_get_fixture_x() {
    return db_hex_to_bytes("08000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
}

std::vector<uint8_t> db_get_fixture_proof_blob() {
    return db_hex_to_bytes(
        "0200553bf0f382fc65a94f20afad5dbce2c1ee8ba3bf93053559ac9960c8fd80ac2222e9b649701a4141a4d8999f0dbfe0c39ea744096598a7528328e5199f0aa30aec8aae8ab5018bf1245329a8272ddff1afbd87ad2eaba1b7fd57bd25edc62e0b010000003f0ffcd0dc307a2aa4678bafba661c77d176ef23afc86e7ea9f4f9eac52b8e1850748019245ecc96547da9b731dc72cded5582a9b0c63e13fd42446c7b28b41d3ded1d0b666d5ddb5b29719e4ebe70969e67e42ddd8591eae60d83dbe619f1250400");
}

}  // namespace

TEST(DiscriminantBoundsRegressionTest, VerifyRejectsDiscSizeBitsAboveMaximum) {
    std::vector<uint8_t> challenge = db_get_fixture_challenge();
    std::vector<uint8_t> x = db_get_fixture_x();
    std::vector<uint8_t> proof_blob = db_get_fixture_proof_blob();
    const integer D = CreateDiscriminant(challenge, BQFC_MAX_D_BITS);

    EXPECT_TRUE(CheckProofOfTimeNWesolowski(
        D,
        x.data(),
        proof_blob.data(),
        proof_blob.size(),
        129499136,
        BQFC_MAX_D_BITS,
        0));

    EXPECT_FALSE(CheckProofOfTimeNWesolowski(
        D,
        x.data(),
        proof_blob.data(),
        proof_blob.size(),
        129499136,
        static_cast<uint64_t>(BQFC_MAX_D_BITS) + 1,
        0));
}

TEST(DiscriminantBoundsRegressionTest, CreateDiscriminantAndVerifyRejectsDiscSizeBitsAboveMaximum) {
    std::vector<uint8_t> challenge = db_get_fixture_challenge();
    std::vector<uint8_t> x = db_get_fixture_x();
    std::vector<uint8_t> proof_blob = db_get_fixture_proof_blob();

    EXPECT_FALSE(CreateDiscriminantAndCheckProofOfTimeNWesolowski(
        challenge,
        static_cast<uint32_t>(BQFC_MAX_D_BITS + 1),
        x.data(),
        proof_blob.data(),
        proof_blob.size(),
        129499136,
        0));
}

TEST(DiscriminantBoundsRegressionTest, BqfcSerializationRejectsOversizedDiscriminantBits) {
    uint8_t serialized[BQFC_FORM_SIZE];
    mpz_t a;
    mpz_t b;
    mpz_init_set_ui(a, 1);
    mpz_init_set_ui(b, 1);

    EXPECT_EQ(bqfc_serialize(serialized, a, b, static_cast<size_t>(BQFC_MAX_D_BITS) + 1), -1);

    mpz_clear(a);
    mpz_clear(b);
}

TEST(DiscriminantBoundsRegressionTest, BqfcDeserializationRejectsOversizedDiscriminantBits) {
    uint8_t serialized[BQFC_FORM_SIZE] = {0};
    mpz_t D;
    mpz_t out_a;
    mpz_t out_b;
    mpz_init_set_si(D, -23);
    mpz_init(out_a);
    mpz_init(out_b);

    EXPECT_EQ(
        bqfc_deserialize(out_a, out_b, D, serialized, BQFC_FORM_SIZE, static_cast<size_t>(BQFC_MAX_D_BITS) + 1),
        -1);

    mpz_clear(D);
    mpz_clear(out_a);
    mpz_clear(out_b);
}
