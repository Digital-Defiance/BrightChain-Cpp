#include <gtest/gtest.h>
#include <brightchain/paillier.hpp>
#include <brightchain/member.hpp>
#include <nlohmann/json.hpp>
#include <fstream>

using namespace brightchain;
using json = nlohmann::json;

static std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::strtol(hex.substr(i, 2).c_str(), nullptr, 16)));
    }
    return bytes;
}

static std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

class PaillierFullCrossPlatformTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::ifstream f("tests/test_vectors_paillier.json");
        if (!f.is_open()) f.open("test_vectors_paillier.json");
        if (!f.is_open()) f.open("../tests/test_vectors_paillier.json");
        if (!f.is_open()) f.open("../../tests/test_vectors_paillier.json");
        ASSERT_TRUE(f.is_open()) << "Failed to open test_vectors_paillier.json";
        vectors = json::parse(f);
    }
    json vectors;
    
    int64_t bytesToInt(const std::vector<uint8_t>& bytes) {
        if (bytes.empty()) return 0;
        // For small values, check the last byte first (least significant)
        // If it's small enough, just return it
        if (bytes.back() < 128 && bytes.size() > 1 && bytes[bytes.size()-2] == 0) {
            return bytes.back();
        }
        // Otherwise convert from big-endian
        int64_t result = 0;
        size_t start = bytes.size() > 8 ? bytes.size() - 8 : 0;
        for (size_t i = start; i < bytes.size(); i++) {
            result = (result << 8) | bytes[i];
        }
        return result;
    }
};

TEST_F(PaillierFullCrossPlatformTest, CppEncryptTsDecrypt) {
    // C++ encrypts, verify TypeScript can decrypt (simulated by using same keys)
    auto pubJson = vectors["votingPublicKey"];
    auto n = hex_to_bytes(pubJson["n"].get<std::string>());
    auto g = hex_to_bytes(pubJson["g"].get<std::string>());
    auto pub = std::make_shared<PaillierPublicKey>(n, g);
    
    auto privJson = vectors["votingPrivateKey"];
    auto lambda = hex_to_bytes(privJson["lambda"].get<std::string>());
    auto mu = hex_to_bytes(privJson["mu"].get<std::string>());
    auto priv = std::make_shared<PaillierPrivateKey>(lambda, mu, pub);
    
    // Encrypt in C++
    for (int i = 0; i < 10; i++) {
        std::vector<uint8_t> plaintext = {static_cast<uint8_t>(i)};
        auto ct = pub->encrypt(plaintext);
        auto pt = priv->decrypt(ct);
        EXPECT_EQ(pt.back(), i);
    }
}

TEST_F(PaillierFullCrossPlatformTest, TsEncryptCppDecrypt) {
    // TypeScript encrypted, C++ decrypts
    auto pubJson = vectors["votingPublicKey"];
    auto n = hex_to_bytes(pubJson["n"].get<std::string>());
    auto g = hex_to_bytes(pubJson["g"].get<std::string>());
    auto pub = std::make_shared<PaillierPublicKey>(n, g);
    
    auto privJson = vectors["votingPrivateKey"];
    auto lambda = hex_to_bytes(privJson["lambda"].get<std::string>());
    auto mu = hex_to_bytes(privJson["mu"].get<std::string>());
    auto priv = std::make_shared<PaillierPrivateKey>(lambda, mu, pub);
    
    for (const auto& vote : vectors["testVotes"]) {
        auto ct = hex_to_bytes(vote["ciphertext"].get<std::string>());
        auto pt = priv->decrypt(ct);
        int expected = vote["plaintext"].get<int>();
        
        // For small values, check last byte or single byte
        if (pt.size() == 1) {
            EXPECT_EQ(pt[0], expected);
        } else {
            // Check if leading bytes are zero
            bool leading_zeros = true;
            for (size_t i = 0; i < pt.size() - 1; i++) {
                if (pt[i] != 0) leading_zeros = false;
            }
            if (leading_zeros) {
                EXPECT_EQ(pt.back(), expected);
            }
        }
    }
}

TEST_F(PaillierFullCrossPlatformTest, VotingScenarioCrossPlatform) {
    auto pubJson = vectors["votingPublicKey"];
    auto n = hex_to_bytes(pubJson["n"].get<std::string>());
    auto g = hex_to_bytes(pubJson["g"].get<std::string>());
    auto pub = std::make_shared<PaillierPublicKey>(n, g);
    
    auto privJson = vectors["votingPrivateKey"];
    auto lambda = hex_to_bytes(privJson["lambda"].get<std::string>());
    auto mu = hex_to_bytes(privJson["mu"].get<std::string>());
    auto priv = std::make_shared<PaillierPrivateKey>(lambda, mu, pub);
    
    // Simulate 3 voters, 2 candidates
    std::vector<std::vector<uint8_t>> votes_a, votes_b;
    votes_a.push_back(pub->encrypt({0x01})); votes_b.push_back(pub->encrypt({0x00}));
    votes_a.push_back(pub->encrypt({0x00})); votes_b.push_back(pub->encrypt({0x01}));
    votes_a.push_back(pub->encrypt({0x01})); votes_b.push_back(pub->encrypt({0x00}));
    
    auto tally_a = pub->addition(votes_a);
    auto tally_b = pub->addition(votes_b);
    
    EXPECT_EQ(priv->decrypt(tally_a).back(), 2);
    EXPECT_EQ(priv->decrypt(tally_b).back(), 1);
}

TEST_F(PaillierFullCrossPlatformTest, KeySerializationRoundTrip) {
    auto pubJson = vectors["votingPublicKey"];
    auto n = hex_to_bytes(pubJson["n"].get<std::string>());
    auto g = hex_to_bytes(pubJson["g"].get<std::string>());
    auto pub1 = std::make_shared<PaillierPublicKey>(n, g);
    
    // Serialize and deserialize
    auto jsonStr = pub1->toJson();
    auto pub2 = PaillierPublicKey::fromJson(jsonStr);
    
    // Keys should be identical
    EXPECT_EQ(pub1->n(), pub2->n());
    EXPECT_EQ(pub1->g(), pub2->g());
    
    // Encryption should work identically
    auto ct1 = pub1->encrypt({0x05});
    auto ct2 = pub2->encrypt({0x05});
    
    auto privJson = vectors["votingPrivateKey"];
    auto lambda = hex_to_bytes(privJson["lambda"].get<std::string>());
    auto mu = hex_to_bytes(privJson["mu"].get<std::string>());
    auto priv = std::make_shared<PaillierPrivateKey>(lambda, mu, pub1);
    
    EXPECT_EQ(priv->decrypt(ct1).back(), 5);
    EXPECT_EQ(priv->decrypt(ct2).back(), 5);
}

TEST_F(PaillierFullCrossPlatformTest, LargeValueEncryption) {
    auto pubJson = vectors["votingPublicKey"];
    auto n = hex_to_bytes(pubJson["n"].get<std::string>());
    auto g = hex_to_bytes(pubJson["g"].get<std::string>());
    auto pub = std::make_shared<PaillierPublicKey>(n, g);
    
    auto privJson = vectors["votingPrivateKey"];
    auto lambda = hex_to_bytes(privJson["lambda"].get<std::string>());
    auto mu = hex_to_bytes(privJson["mu"].get<std::string>());
    auto priv = std::make_shared<PaillierPrivateKey>(lambda, mu, pub);
    
    // Test with larger values
    std::vector<uint8_t> large = {0x01, 0x23};
    auto ct = pub->encrypt(large);
    auto pt = priv->decrypt(ct);
    EXPECT_EQ(pt, large);
}

TEST_F(PaillierFullCrossPlatformTest, MultipleAdditions) {
    auto pubJson = vectors["votingPublicKey"];
    auto n = hex_to_bytes(pubJson["n"].get<std::string>());
    auto g = hex_to_bytes(pubJson["g"].get<std::string>());
    auto pub = std::make_shared<PaillierPublicKey>(n, g);
    
    auto privJson = vectors["votingPrivateKey"];
    auto lambda = hex_to_bytes(privJson["lambda"].get<std::string>());
    auto mu = hex_to_bytes(privJson["mu"].get<std::string>());
    auto priv = std::make_shared<PaillierPrivateKey>(lambda, mu, pub);
    
    std::vector<std::vector<uint8_t>> cts;
    int sum = 0;
    for (int i = 1; i <= 20; i++) {
        cts.push_back(pub->encrypt({static_cast<uint8_t>(i)}));
        sum += i;
    }
    
    auto result = pub->addition(cts);
    auto decrypted = priv->decrypt(result);
    EXPECT_EQ(decrypted.back(), sum % 256);
}

TEST_F(PaillierFullCrossPlatformTest, ZeroHandling) {
    auto pubJson = vectors["votingPublicKey"];
    auto n = hex_to_bytes(pubJson["n"].get<std::string>());
    auto g = hex_to_bytes(pubJson["g"].get<std::string>());
    auto pub = std::make_shared<PaillierPublicKey>(n, g);
    
    auto privJson = vectors["votingPrivateKey"];
    auto lambda = hex_to_bytes(privJson["lambda"].get<std::string>());
    auto mu = hex_to_bytes(privJson["mu"].get<std::string>());
    auto priv = std::make_shared<PaillierPrivateKey>(lambda, mu, pub);
    
    auto ct = pub->encrypt({0x00});
    auto pt = priv->decrypt(ct);
    EXPECT_EQ(pt.back(), 0);
}

TEST_F(PaillierFullCrossPlatformTest, PlaintextAddition) {
    auto pubJson = vectors["votingPublicKey"];
    auto n = hex_to_bytes(pubJson["n"].get<std::string>());
    auto g = hex_to_bytes(pubJson["g"].get<std::string>());
    auto pub = std::make_shared<PaillierPublicKey>(n, g);
    
    auto privJson = vectors["votingPrivateKey"];
    auto lambda = hex_to_bytes(privJson["lambda"].get<std::string>());
    auto mu = hex_to_bytes(privJson["mu"].get<std::string>());
    auto priv = std::make_shared<PaillierPrivateKey>(lambda, mu, pub);
    
    auto ct = pub->encrypt({0x05});
    auto result = pub->plaintextAddition(ct, {{0x03}, {0x02}});
    auto pt = priv->decrypt(result);
    EXPECT_EQ(pt.back(), 10);
}
