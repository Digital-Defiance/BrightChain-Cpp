#include <gtest/gtest.h>
#include <brightchain/paillier.hpp>
#include <brightchain/member.hpp>

using namespace brightchain;

class PaillierBasicTest : public ::testing::Test {};

TEST_F(PaillierBasicTest, BasicEncryptDecrypt) {
    // Generate member with keys
    auto member = Member::generate(MemberType::User, "Alice", "alice@example.com");
    
    // Derive voting keys
    member.deriveVotingKeys(2048, 64); // Smaller for faster test
    
    ASSERT_TRUE(member.hasVotingKeys());
    ASSERT_TRUE(member.hasVotingPrivateKey());
    
    auto publicKey = member.votingPublicKey();
    auto privateKey = member.votingPrivateKey();
    
    // Encrypt a vote (1)
    std::vector<uint8_t> plaintext = {0x01};
    auto ciphertext = publicKey->encrypt(plaintext);
    
    // Decrypt
    auto decrypted = privateKey->decrypt(ciphertext);
    
    EXPECT_EQ(plaintext, decrypted);
}

TEST_F(PaillierBasicTest, HomomorphicAddition) {
    // Generate member
    auto member = Member::generate(MemberType::User, "Bob", "bob@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto publicKey = member.votingPublicKey();
    auto privateKey = member.votingPrivateKey();
    
    // Encrypt two votes
    std::vector<uint8_t> vote1 = {0x01}; // 1
    std::vector<uint8_t> vote2 = {0x01}; // 1
    
    auto ct1 = publicKey->encrypt(vote1);
    auto ct2 = publicKey->encrypt(vote2);
    
    // Add ciphertexts homomorphically
    auto sum_ct = publicKey->addition({ct1, ct2});
    
    // Decrypt sum
    auto sum = privateKey->decrypt(sum_ct);
    
    // Should be 2
    EXPECT_EQ(sum[0], 0x02);
}

TEST_F(PaillierBasicTest, VotingScenario) {
    // Create 3 voters
    auto voter1 = Member::generate(MemberType::User, "Voter1", "v1@example.com");
    auto voter2 = Member::generate(MemberType::User, "Voter2", "v2@example.com");
    auto voter3 = Member::generate(MemberType::User, "Voter3", "v3@example.com");
    
    // Authority derives voting keys
    auto authority = Member::generate(MemberType::Admin, "Authority", "auth@example.com");
    authority.deriveVotingKeys(2048, 64);
    
    auto publicKey = authority.votingPublicKey();
    auto privateKey = authority.votingPrivateKey();
    
    // 3 choices: Alice, Bob, Charlie
    // Voter1 votes for Alice (index 0)
    // Voter2 votes for Bob (index 1)
    // Voter3 votes for Alice (index 0)
    
    std::vector<std::vector<uint8_t>> votes_choice0; // Alice
    std::vector<std::vector<uint8_t>> votes_choice1; // Bob
    std::vector<std::vector<uint8_t>> votes_choice2; // Charlie
    
    // Voter1: Alice
    votes_choice0.push_back(publicKey->encrypt({0x01}));
    votes_choice1.push_back(publicKey->encrypt({0x00}));
    votes_choice2.push_back(publicKey->encrypt({0x00}));
    
    // Voter2: Bob
    votes_choice0.push_back(publicKey->encrypt({0x00}));
    votes_choice1.push_back(publicKey->encrypt({0x01}));
    votes_choice2.push_back(publicKey->encrypt({0x00}));
    
    // Voter3: Alice
    votes_choice0.push_back(publicKey->encrypt({0x01}));
    votes_choice1.push_back(publicKey->encrypt({0x00}));
    votes_choice2.push_back(publicKey->encrypt({0x00}));
    
    // Tally votes homomorphically
    auto tally0 = publicKey->addition(votes_choice0);
    auto tally1 = publicKey->addition(votes_choice1);
    auto tally2 = publicKey->addition(votes_choice2);
    
    // Decrypt tallies
    auto count0 = privateKey->decrypt(tally0);
    auto count1 = privateKey->decrypt(tally1);
    auto count2 = privateKey->decrypt(tally2);
    
    // Alice: 2, Bob: 1, Charlie: 0
    EXPECT_EQ(count0[0], 0x02);
    EXPECT_EQ(count1[0], 0x01);
    EXPECT_EQ(count2[0], 0x00);
}
