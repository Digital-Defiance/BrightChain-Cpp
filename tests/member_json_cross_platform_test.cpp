#include <gtest/gtest.h>
#include <brightchain/member.hpp>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace brightchain;
using json = nlohmann::json;

class MemberJsonCrossPlatformTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Try to load TypeScript-generated member JSON
        std::ifstream f("tests/test_vectors_member_json.json");
        if (!f.is_open()) f.open("test_vectors_member_json.json");
        if (!f.is_open()) f.open("../tests/test_vectors_member_json.json");
        if (!f.is_open()) f.open("../../tests/test_vectors_member_json.json");
        
        if (f.is_open()) {
            tsVectors = json::parse(f);
            hasTsVectors = true;
        } else {
            hasTsVectors = false;
        }
    }
    
    json tsVectors;
    bool hasTsVectors;
};

TEST_F(MemberJsonCrossPlatformTest, CppCanLoadTsJson) {
    if (!hasTsVectors) {
        GTEST_SKIP() << "TypeScript vectors not available";
    }
    
    // Load TypeScript-generated member JSON (or C++ for testing)
    std::string key = tsVectors.contains("memberWithVotingKeys") ? "memberWithVotingKeys" : "memberWithPrivateKey";
    auto memberJson = tsVectors[key].dump();
    
    // C++ should be able to deserialize it
    auto member = Member::fromJson(memberJson);
    
    // Verify basic fields
    EXPECT_EQ(member.name(), tsVectors[key]["name"].get<std::string>());
    EXPECT_EQ(member.email(), tsVectors[key]["email"].get<std::string>());
    
    // Verify has voting keys
    EXPECT_TRUE(member.hasVotingKeys());
    EXPECT_TRUE(member.hasVotingPrivateKey());
}

TEST_F(MemberJsonCrossPlatformTest, CppJsonRoundTrip) {
    // Create member in C++
    auto member1 = Member::generate(MemberType::User, "C++ User", "cpp@example.com");
    member1.deriveVotingKeys(512, 16);
    
    // Serialize to JSON
    auto json1 = member1.toJson(true);
    
    // Deserialize
    auto member2 = Member::fromJson(json1);
    
    // Verify everything matches
    EXPECT_EQ(member1.id(), member2.id());
    EXPECT_EQ(member1.name(), member2.name());
    EXPECT_EQ(member1.email(), member2.email());
    EXPECT_EQ(member1.publicKey(), member2.publicKey());
    EXPECT_EQ(member1.privateKey(), member2.privateKey());
    
    // Verify voting keys match
    EXPECT_EQ(member1.votingPublicKey()->n(), member2.votingPublicKey()->n());
    EXPECT_EQ(member1.votingPublicKey()->g(), member2.votingPublicKey()->g());
    EXPECT_EQ(member1.votingPrivateKey()->lambda(), member2.votingPrivateKey()->lambda());
    EXPECT_EQ(member1.votingPrivateKey()->mu(), member2.votingPrivateKey()->mu());
    
    // Verify can encrypt/decrypt with restored keys
    std::vector<uint8_t> testData = {0x42};
    auto encrypted = member2.votingPublicKey()->encrypt(testData);
    auto decrypted = member2.votingPrivateKey()->decrypt(encrypted);
    EXPECT_EQ(decrypted, testData);
}

TEST_F(MemberJsonCrossPlatformTest, GenerateCppMemberJson) {
    // Generate test member with known mnemonic
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    auto member = Member::fromMnemonic(mnemonic, MemberType::User, "Test User", "test@example.com");
    member.deriveVotingKeys(512, 16);
    
    // Create JSON with both public and private data
    json output;
    output["source"] = "cpp";
    output["memberPublicOnly"] = json::parse(member.toJson(false));
    output["memberWithPrivateKey"] = json::parse(member.toJson(true));
    
    // Generate another member for comparison
    auto member2 = Member::generate(MemberType::System, "System User", "system@example.com");
    member2.deriveVotingKeys(512, 16);
    output["memberGenerated"] = json::parse(member2.toJson(true));
    
    // Save to file
    std::ofstream outFile("test_vectors_cpp_member_json.json");
    if (outFile.is_open()) {
        outFile << output.dump(2);
        std::cout << "\n✓ Generated C++ member JSON vectors\n";
    }
}

TEST_F(MemberJsonCrossPlatformTest, JsonFieldsMatchExpected) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(512, 16);
    
    auto jsonStr = member.toJson(true);
    auto j = json::parse(jsonStr);
    
    // Verify all expected fields are present
    EXPECT_TRUE(j.contains("id"));
    EXPECT_TRUE(j.contains("type"));
    EXPECT_TRUE(j.contains("name"));
    EXPECT_TRUE(j.contains("email"));
    EXPECT_TRUE(j.contains("publicKey"));
    EXPECT_TRUE(j.contains("privateKey"));
    EXPECT_TRUE(j.contains("dateCreated"));
    EXPECT_TRUE(j.contains("dateUpdated"));
    EXPECT_TRUE(j.contains("votingPublicKey"));
    EXPECT_TRUE(j.contains("votingPrivateKey"));
    
    // Verify voting key structure
    EXPECT_TRUE(j["votingPublicKey"].contains("n"));
    EXPECT_TRUE(j["votingPublicKey"].contains("g"));
    EXPECT_TRUE(j["votingPrivateKey"].contains("lambda"));
    EXPECT_TRUE(j["votingPrivateKey"].contains("mu"));
    
    // Verify types
    EXPECT_TRUE(j["publicKey"].is_array());
    EXPECT_TRUE(j["privateKey"].is_array());
    EXPECT_TRUE(j["votingPublicKey"]["n"].is_string());
    EXPECT_TRUE(j["votingPublicKey"]["g"].is_string());
}

TEST_F(MemberJsonCrossPlatformTest, PublicKeyArrayFormat) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    auto jsonStr = member.toJson(false);
    auto j = json::parse(jsonStr);
    
    // Public key should be array of numbers
    EXPECT_TRUE(j["publicKey"].is_array());
    EXPECT_EQ(j["publicKey"].size(), 33); // Compressed public key
    
    // Each element should be a number 0-255
    for (const auto& byte : j["publicKey"]) {
        EXPECT_TRUE(byte.is_number());
        int val = byte.get<int>();
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 255);
    }
}

TEST_F(MemberJsonCrossPlatformTest, VotingKeysHexFormat) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(512, 16);
    
    auto jsonStr = member.toJson(true);
    auto j = json::parse(jsonStr);
    
    // Voting keys should be hex strings
    std::string n = j["votingPublicKey"]["n"].get<std::string>();
    std::string g = j["votingPublicKey"]["g"].get<std::string>();
    std::string lambda = j["votingPrivateKey"]["lambda"].get<std::string>();
    std::string mu = j["votingPrivateKey"]["mu"].get<std::string>();
    
    // Should be valid hex (even length, only hex chars)
    EXPECT_EQ(n.length() % 2, 0);
    EXPECT_EQ(g.length() % 2, 0);
    EXPECT_EQ(lambda.length() % 2, 0);
    EXPECT_EQ(mu.length() % 2, 0);
    
    // Check hex characters
    auto isHex = [](const std::string& s) {
        return s.find_first_not_of("0123456789abcdef") == std::string::npos;
    };
    
    EXPECT_TRUE(isHex(n));
    EXPECT_TRUE(isHex(g));
    EXPECT_TRUE(isHex(lambda));
    EXPECT_TRUE(isHex(mu));
}
