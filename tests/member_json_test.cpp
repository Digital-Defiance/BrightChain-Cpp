#include <gtest/gtest.h>
#include <brightchain/member.hpp>

using namespace brightchain;

TEST(MemberJsonTest, SerializePublicDataOnly) {
    auto member = Member::generate(MemberType::User, "Test User", "test@example.com");
    
    auto json = member.toJson(false);
    EXPECT_FALSE(json.empty());
    EXPECT_TRUE(json.find("\"id\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"name\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"email\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"publicKey\"") != std::string::npos);
    EXPECT_FALSE(json.find("\"privateKey\"") != std::string::npos);
}

TEST(MemberJsonTest, SerializeWithPrivateData) {
    auto member = Member::generate(MemberType::User, "Test User", "test@example.com");
    
    auto json = member.toJson(true);
    EXPECT_TRUE(json.find("\"privateKey\"") != std::string::npos);
}

TEST(MemberJsonTest, RoundTripPublicOnly) {
    auto member1 = Member::generate(MemberType::User, "Test User", "test@example.com");
    
    auto json = member1.toJson(false);
    auto member2 = Member::fromJson(json);
    
    EXPECT_EQ(member1.id(), member2.id());
    EXPECT_EQ(member1.name(), member2.name());
    EXPECT_EQ(member1.email(), member2.email());
    EXPECT_EQ(member1.publicKey(), member2.publicKey());
    EXPECT_FALSE(member2.hasPrivateKey());
}

TEST(MemberJsonTest, RoundTripWithPrivateKey) {
    auto member1 = Member::generate(MemberType::User, "Test User", "test@example.com");
    
    auto json = member1.toJson(true);
    auto member2 = Member::fromJson(json);
    
    EXPECT_EQ(member1.id(), member2.id());
    EXPECT_EQ(member1.publicKey(), member2.publicKey());
    EXPECT_TRUE(member2.hasPrivateKey());
    EXPECT_EQ(member1.privateKey(), member2.privateKey());
}

TEST(MemberJsonTest, RoundTripWithVotingKeys) {
    auto member1 = Member::generate(MemberType::User, "Test User", "test@example.com");
    member1.deriveVotingKeys(512, 16);
    
    auto json = member1.toJson(true);
    auto member2 = Member::fromJson(json);
    
    EXPECT_TRUE(member2.hasVotingKeys());
    EXPECT_EQ(member1.votingPublicKey()->n(), member2.votingPublicKey()->n());
    EXPECT_EQ(member1.votingPublicKey()->g(), member2.votingPublicKey()->g());
    
    EXPECT_TRUE(member2.hasVotingPrivateKey());
    EXPECT_EQ(member1.votingPrivateKey()->lambda(), member2.votingPrivateKey()->lambda());
    EXPECT_EQ(member1.votingPrivateKey()->mu(), member2.votingPrivateKey()->mu());
}
