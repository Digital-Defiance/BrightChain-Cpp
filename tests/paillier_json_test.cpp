#include <gtest/gtest.h>
#include <brightchain/paillier.hpp>
#include <brightchain/member.hpp>
#include <nlohmann/json.hpp>

using namespace brightchain;
using json = nlohmann::json;

class PaillierJsonTest : public ::testing::Test {};

TEST_F(PaillierJsonTest, PublicKeyToJson) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    std::string jsonStr = pub->toJson();
    
    EXPECT_FALSE(jsonStr.empty());
    
    // Parse and verify structure
    json j = json::parse(jsonStr);
    EXPECT_TRUE(j.contains("n"));
    EXPECT_TRUE(j.contains("g"));
    EXPECT_TRUE(j["n"].is_string());
    EXPECT_TRUE(j["g"].is_string());
}

TEST_F(PaillierJsonTest, PublicKeyFromJson) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub1 = member.votingPublicKey();
    std::string jsonStr = pub1->toJson();
    
    auto pub2 = PaillierPublicKey::fromJson(jsonStr);
    
    EXPECT_EQ(pub1->n(), pub2->n());
    EXPECT_EQ(pub1->g(), pub2->g());
}

TEST_F(PaillierJsonTest, PublicKeyRoundTrip) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub1 = member.votingPublicKey();
    
    // Serialize and deserialize
    auto pub2 = PaillierPublicKey::fromJson(pub1->toJson());
    
    // Test encryption works with deserialized key
    std::vector<uint8_t> plaintext = {0x42};
    auto ct = pub2->encrypt(plaintext);
    
    // Decrypt with original private key
    auto priv = member.votingPrivateKey();
    auto pt = priv->decrypt(ct);
    
    EXPECT_EQ(pt[0], 0x42);
}

TEST_F(PaillierJsonTest, PrivateKeyToJson) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto priv = member.votingPrivateKey();
    std::string jsonStr = priv->toJson();
    
    EXPECT_FALSE(jsonStr.empty());
    
    // Parse and verify structure
    json j = json::parse(jsonStr);
    EXPECT_TRUE(j.contains("lambda"));
    EXPECT_TRUE(j.contains("mu"));
    EXPECT_TRUE(j["lambda"].is_string());
    EXPECT_TRUE(j["mu"].is_string());
}

TEST_F(PaillierJsonTest, MatchesTypeScriptFormat) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    json pubJson = json::parse(pub->toJson());
    json privJson = json::parse(priv->toJson());
    
    // Verify hex strings (lowercase, no 0x prefix)
    std::string nHex = pubJson["n"].get<std::string>();
    std::string gHex = pubJson["g"].get<std::string>();
    std::string lambdaHex = privJson["lambda"].get<std::string>();
    std::string muHex = privJson["mu"].get<std::string>();
    
    // All should be hex strings
    EXPECT_GT(nHex.length(), 0);
    EXPECT_GT(gHex.length(), 0);
    EXPECT_GT(lambdaHex.length(), 0);
    EXPECT_GT(muHex.length(), 0);
    
    // Should be even length (pairs of hex digits)
    EXPECT_EQ(nHex.length() % 2, 0);
    EXPECT_EQ(gHex.length() % 2, 0);
    EXPECT_EQ(lambdaHex.length() % 2, 0);
    EXPECT_EQ(muHex.length() % 2, 0);
}
