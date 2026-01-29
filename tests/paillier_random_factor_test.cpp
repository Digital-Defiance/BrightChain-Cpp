#include <gtest/gtest.h>
#include <brightchain/paillier.hpp>
#include <brightchain/member.hpp>

using namespace brightchain;

class PaillierRandomFactorTest : public ::testing::Test {};

TEST_F(PaillierRandomFactorTest, HasPrimesAfterDerivation) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto priv = member.votingPrivateKey();
    EXPECT_TRUE(priv->hasPrimes());
}

TEST_F(PaillierRandomFactorTest, GetRandomFactorWorks) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    std::vector<uint8_t> plaintext = {0x05};
    auto ct = pub->encrypt(plaintext);
    
    // Should not throw
    EXPECT_NO_THROW(priv->getRandomFactor(ct));
}

TEST_F(PaillierRandomFactorTest, ThrowsWithoutPrimes) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    
    // Create private key without p and q
    std::vector<uint8_t> lambda = {0x01};
    std::vector<uint8_t> mu = {0x01};
    auto priv = std::make_shared<PaillierPrivateKey>(lambda, mu, pub);
    
    EXPECT_FALSE(priv->hasPrimes());
    
    auto ct = pub->encrypt({0x01});
    EXPECT_THROW(priv->getRandomFactor(ct), std::runtime_error);
}
