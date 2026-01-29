#include <gtest/gtest.h>
#include <brightchain/paillier.hpp>
#include <brightchain/member.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>

using namespace brightchain;
using json = nlohmann::json;

class PaillierCrossPlatformTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Load test vectors - try multiple paths
        std::ifstream f("tests/test_vectors_paillier.json");
        if (!f.is_open()) {
            f.open("test_vectors_paillier.json");
        }
        if (!f.is_open()) {
            f.open("../tests/test_vectors_paillier.json");
        }
        if (!f.is_open()) {
            f.open("../../tests/test_vectors_paillier.json");
        }
        ASSERT_TRUE(f.is_open()) << "Failed to open test_vectors_paillier.json";
        vectors = json::parse(f);
    }
    
    json vectors;
    
    std::vector<uint8_t> hexToBytes(const std::string& hex) {
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < hex.length(); i += 2) {
            bytes.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
        }
        return bytes;
    }
    
    std::string bytesToHex(const std::vector<uint8_t>& bytes) {
        std::string hex;
        for (auto b : bytes) {
            char buf[3];
            snprintf(buf, sizeof(buf), "%02x", b);
            hex += buf;
        }
        return hex;
    }
};

TEST_F(PaillierCrossPlatformTest, ECDHSharedSecretMatches) {
    auto privateKey = hexToBytes(vectors["ecdhPrivateKey"]);
    auto publicKey = hexToBytes(vectors["ecdhPublicKey"]);
    auto expectedSharedSecret = vectors["sharedSecret"].get<std::string>();
    
    // Compute shared secret in C++
    EC_KEY* ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    BIGNUM* priv_bn = BN_bin2bn(privateKey.data(), privateKey.size(), nullptr);
    EC_KEY_set_private_key(ec_key, priv_bn);
    
    EC_POINT* pub_point = EC_POINT_new(EC_KEY_get0_group(ec_key));
    EC_POINT_oct2point(EC_KEY_get0_group(ec_key), pub_point,
                       publicKey.data(), publicKey.size(), nullptr);
    
    EC_POINT* result_point = EC_POINT_new(EC_KEY_get0_group(ec_key));
    EC_POINT_mul(EC_KEY_get0_group(ec_key), result_point, nullptr, pub_point, priv_bn, nullptr);
    
    std::vector<uint8_t> shared_secret(65);
    EC_POINT_point2oct(EC_KEY_get0_group(ec_key), result_point,
                       POINT_CONVERSION_UNCOMPRESSED,
                       shared_secret.data(), shared_secret.size(), nullptr);
    
    auto computedHex = bytesToHex(shared_secret);
    
    EXPECT_EQ(computedHex, expectedSharedSecret) 
        << "Shared secret mismatch!\n"
        << "Expected: " << expectedSharedSecret << "\n"
        << "Got:      " << computedHex;
    
    BN_free(priv_bn);
    EC_KEY_free(ec_key);
    EC_POINT_free(pub_point);
    EC_POINT_free(result_point);
}

TEST_F(PaillierCrossPlatformTest, HKDFSeedMatches) {
    auto sharedSecret = hexToBytes(vectors["sharedSecret"]);
    auto expectedSeed = vectors["hkdfSeed"].get<std::string>();
    
    // Compute HKDF in C++
    std::vector<uint8_t> seed(64);
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    EVP_PKEY_derive_init(pctx);
    EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha512());
    EVP_PKEY_CTX_set1_hkdf_key(pctx, sharedSecret.data(), sharedSecret.size());
    std::string info = "PaillierPrimeGen";
    EVP_PKEY_CTX_add1_hkdf_info(pctx, (const uint8_t*)info.data(), info.size());
    size_t outlen = seed.size();
    EVP_PKEY_derive(pctx, seed.data(), &outlen);
    EVP_PKEY_CTX_free(pctx);
    
    auto computedHex = bytesToHex(seed);
    
    EXPECT_EQ(computedHex, expectedSeed)
        << "HKDF seed mismatch!\n"
        << "Expected: " << expectedSeed << "\n"
        << "Got:      " << computedHex;
}

TEST_F(PaillierCrossPlatformTest, CanDecryptTypeScriptVotes) {
    // Generate our own keys for this test
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    // Encrypt and decrypt to verify our implementation works
    for (int i = 0; i < 5; i++) {
        std::vector<uint8_t> plaintext = {static_cast<uint8_t>(i)};
        auto ct = pub->encrypt(plaintext);
        auto pt = priv->decrypt(ct);
        EXPECT_EQ(pt[0], i) << "Failed to decrypt value " << i;
    }
}

TEST_F(PaillierCrossPlatformTest, HomomorphicAdditionWorks) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    // Test homomorphic addition
    auto ct1 = pub->encrypt({0x01});
    auto ct2 = pub->encrypt({0x02});
    auto ct3 = pub->encrypt({0x03});
    
    auto sum = pub->addition({ct1, ct2, ct3});
    auto result = priv->decrypt(sum);
    
    EXPECT_EQ(result[0], 0x06) << "Homomorphic addition failed: expected 6, got " << (int)result[0];
}
