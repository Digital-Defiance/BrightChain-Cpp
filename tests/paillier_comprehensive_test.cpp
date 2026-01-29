#include <gtest/gtest.h>
#include <brightchain/paillier.hpp>
#include <brightchain/member.hpp>

using namespace brightchain;

class PaillierTest : public ::testing::Test {
protected:
    void SetUp() override {
        member = std::make_unique<Member>(
            Member::generate(MemberType::User, "TestUser", "test@example.com")
        );
    }
    
    std::unique_ptr<Member> member;
};

// Basic Encryption/Decryption Tests
TEST_F(PaillierTest, EncryptDecryptZero) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    std::vector<uint8_t> zero = {0x00};
    auto ct = pub->encrypt(zero);
    auto pt = priv->decrypt(ct);
    
    EXPECT_EQ(pt[0], 0x00);
}

TEST_F(PaillierTest, EncryptDecryptOne) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    std::vector<uint8_t> one = {0x01};
    auto ct = pub->encrypt(one);
    auto pt = priv->decrypt(ct);
    
    EXPECT_EQ(pt[0], 0x01);
}

TEST_F(PaillierTest, EncryptDecryptLargeValue) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    std::vector<uint8_t> large = {0xFF};
    auto ct = pub->encrypt(large);
    auto pt = priv->decrypt(ct);
    
    EXPECT_EQ(pt[0], 0xFF);
}

TEST_F(PaillierTest, EncryptDecryptMultiByteValue) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    std::vector<uint8_t> value = {0x12, 0x34};
    auto ct = pub->encrypt(value);
    auto pt = priv->decrypt(ct);
    
    EXPECT_EQ(pt, value);
}

// Homomorphic Addition Tests
TEST_F(PaillierTest, HomomorphicAddTwoOnes) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    auto ct1 = pub->encrypt({0x01});
    auto ct2 = pub->encrypt({0x01});
    auto sum = pub->addition({ct1, ct2});
    auto result = priv->decrypt(sum);
    
    EXPECT_EQ(result[0], 0x02);
}

TEST_F(PaillierTest, HomomorphicAddThreeValues) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    auto ct1 = pub->encrypt({0x01});
    auto ct2 = pub->encrypt({0x02});
    auto ct3 = pub->encrypt({0x03});
    auto sum = pub->addition({ct1, ct2, ct3});
    auto result = priv->decrypt(sum);
    
    EXPECT_EQ(result[0], 0x06);
}

TEST_F(PaillierTest, HomomorphicAddWithZero) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    auto ct1 = pub->encrypt({0x05});
    auto ct2 = pub->encrypt({0x00});
    auto sum = pub->addition({ct1, ct2});
    auto result = priv->decrypt(sum);
    
    EXPECT_EQ(result[0], 0x05);
}

TEST_F(PaillierTest, HomomorphicAddManyValues) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    std::vector<std::vector<uint8_t>> ciphertexts;
    for (int i = 0; i < 10; i++) {
        ciphertexts.push_back(pub->encrypt({0x01}));
    }
    
    auto sum = pub->addition(ciphertexts);
    auto result = priv->decrypt(sum);
    
    EXPECT_EQ(result[0], 0x0A); // 10
}

// Voting Scenario Tests
TEST_F(PaillierTest, SimpleVote_TwoCandidates) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    // 5 voters, 2 candidates
    // Votes: A=3, B=2
    std::vector<std::vector<uint8_t>> votes_a;
    std::vector<std::vector<uint8_t>> votes_b;
    
    // Voter 1: A
    votes_a.push_back(pub->encrypt({0x01}));
    votes_b.push_back(pub->encrypt({0x00}));
    
    // Voter 2: B
    votes_a.push_back(pub->encrypt({0x00}));
    votes_b.push_back(pub->encrypt({0x01}));
    
    // Voter 3: A
    votes_a.push_back(pub->encrypt({0x01}));
    votes_b.push_back(pub->encrypt({0x00}));
    
    // Voter 4: B
    votes_a.push_back(pub->encrypt({0x00}));
    votes_b.push_back(pub->encrypt({0x01}));
    
    // Voter 5: A
    votes_a.push_back(pub->encrypt({0x01}));
    votes_b.push_back(pub->encrypt({0x00}));
    
    auto tally_a = pub->addition(votes_a);
    auto tally_b = pub->addition(votes_b);
    
    auto count_a = priv->decrypt(tally_a);
    auto count_b = priv->decrypt(tally_b);
    
    EXPECT_EQ(count_a[0], 0x03);
    EXPECT_EQ(count_b[0], 0x02);
}

TEST_F(PaillierTest, ThreeCandidateVote) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    // 10 voters, 3 candidates: Alice, Bob, Charlie
    // Expected: Alice=4, Bob=3, Charlie=3
    std::vector<std::vector<uint8_t>> votes_alice;
    std::vector<std::vector<uint8_t>> votes_bob;
    std::vector<std::vector<uint8_t>> votes_charlie;
    
    // Simulate 10 votes
    int alice_votes = 4, bob_votes = 3, charlie_votes = 3;
    
    for (int i = 0; i < alice_votes; i++) {
        votes_alice.push_back(pub->encrypt({0x01}));
        votes_bob.push_back(pub->encrypt({0x00}));
        votes_charlie.push_back(pub->encrypt({0x00}));
    }
    
    for (int i = 0; i < bob_votes; i++) {
        votes_alice.push_back(pub->encrypt({0x00}));
        votes_bob.push_back(pub->encrypt({0x01}));
        votes_charlie.push_back(pub->encrypt({0x00}));
    }
    
    for (int i = 0; i < charlie_votes; i++) {
        votes_alice.push_back(pub->encrypt({0x00}));
        votes_bob.push_back(pub->encrypt({0x00}));
        votes_charlie.push_back(pub->encrypt({0x01}));
    }
    
    auto tally_alice = pub->addition(votes_alice);
    auto tally_bob = pub->addition(votes_bob);
    auto tally_charlie = pub->addition(votes_charlie);
    
    auto count_alice = priv->decrypt(tally_alice);
    auto count_bob = priv->decrypt(tally_bob);
    auto count_charlie = priv->decrypt(tally_charlie);
    
    EXPECT_EQ(count_alice[0], 0x04);
    EXPECT_EQ(count_bob[0], 0x03);
    EXPECT_EQ(count_charlie[0], 0x03);
}

TEST_F(PaillierTest, UnanimousVote) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    // All 7 voters vote for same candidate
    std::vector<std::vector<uint8_t>> votes;
    for (int i = 0; i < 7; i++) {
        votes.push_back(pub->encrypt({0x01}));
    }
    
    auto tally = pub->addition(votes);
    auto count = priv->decrypt(tally);
    
    EXPECT_EQ(count[0], 0x07);
}

TEST_F(PaillierTest, NoVotesForCandidate) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    // 5 voters, none vote for this candidate
    std::vector<std::vector<uint8_t>> votes;
    for (int i = 0; i < 5; i++) {
        votes.push_back(pub->encrypt({0x00}));
    }
    
    auto tally = pub->addition(votes);
    auto count = priv->decrypt(tally);
    
    EXPECT_EQ(count[0], 0x00);
}

// Weighted Voting Tests
TEST_F(PaillierTest, WeightedVote_DifferentWeights) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    // 3 voters with different weights
    // Voter 1: weight 5
    // Voter 2: weight 3
    // Voter 3: weight 2
    std::vector<std::vector<uint8_t>> votes;
    votes.push_back(pub->encrypt({0x05}));
    votes.push_back(pub->encrypt({0x03}));
    votes.push_back(pub->encrypt({0x02}));
    
    auto tally = pub->addition(votes);
    auto total = priv->decrypt(tally);
    
    EXPECT_EQ(total[0], 0x0A); // 10
}

TEST_F(PaillierTest, WeightedVote_TwoCandidates) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    // Candidate A: weights 5, 3 = 8
    // Candidate B: weights 7, 2 = 9
    std::vector<std::vector<uint8_t>> votes_a;
    std::vector<std::vector<uint8_t>> votes_b;
    
    votes_a.push_back(pub->encrypt({0x05}));
    votes_a.push_back(pub->encrypt({0x03}));
    votes_b.push_back(pub->encrypt({0x07}));
    votes_b.push_back(pub->encrypt({0x02}));
    
    auto tally_a = pub->addition(votes_a);
    auto tally_b = pub->addition(votes_b);
    
    auto count_a = priv->decrypt(tally_a);
    auto count_b = priv->decrypt(tally_b);
    
    EXPECT_EQ(count_a[0], 0x08);
    EXPECT_EQ(count_b[0], 0x09);
}

// Approval Voting Tests
TEST_F(PaillierTest, ApprovalVoting_MultipleChoices) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    // 3 candidates, 4 voters
    // Voter can approve multiple candidates
    std::vector<std::vector<uint8_t>> votes_a;
    std::vector<std::vector<uint8_t>> votes_b;
    std::vector<std::vector<uint8_t>> votes_c;
    
    // Voter 1: approves A and B
    votes_a.push_back(pub->encrypt({0x01}));
    votes_b.push_back(pub->encrypt({0x01}));
    votes_c.push_back(pub->encrypt({0x00}));
    
    // Voter 2: approves A only
    votes_a.push_back(pub->encrypt({0x01}));
    votes_b.push_back(pub->encrypt({0x00}));
    votes_c.push_back(pub->encrypt({0x00}));
    
    // Voter 3: approves all three
    votes_a.push_back(pub->encrypt({0x01}));
    votes_b.push_back(pub->encrypt({0x01}));
    votes_c.push_back(pub->encrypt({0x01}));
    
    // Voter 4: approves B and C
    votes_a.push_back(pub->encrypt({0x00}));
    votes_b.push_back(pub->encrypt({0x01}));
    votes_c.push_back(pub->encrypt({0x01}));
    
    auto tally_a = pub->addition(votes_a);
    auto tally_b = pub->addition(votes_b);
    auto tally_c = pub->addition(votes_c);
    
    auto count_a = priv->decrypt(tally_a);
    auto count_b = priv->decrypt(tally_b);
    auto count_c = priv->decrypt(tally_c);
    
    EXPECT_EQ(count_a[0], 0x03); // A: 3 approvals
    EXPECT_EQ(count_b[0], 0x03); // B: 3 approvals
    EXPECT_EQ(count_c[0], 0x02); // C: 2 approvals
}

// Key Derivation Tests
TEST_F(PaillierTest, KeyDerivation_Deterministic) {
    // Same member should derive same keys
    auto mnemonic = Member::generateMnemonic();
    
    auto member1 = Member::fromMnemonic(mnemonic, MemberType::User, "Alice", "alice@example.com");
    member1.deriveVotingKeys(2048, 64);
    
    auto member2 = Member::fromMnemonic(mnemonic, MemberType::User, "Alice", "alice@example.com");
    member2.deriveVotingKeys(2048, 64);
    
    // Keys should be identical
    EXPECT_EQ(member1.votingPublicKey()->n(), member2.votingPublicKey()->n());
    EXPECT_EQ(member1.votingPublicKey()->g(), member2.votingPublicKey()->g());
}

TEST_F(PaillierTest, KeyDerivation_DifferentMembers) {
    // Different members should have different keys
    auto member1 = Member::generate(MemberType::User, "Alice", "alice@example.com");
    member1.deriveVotingKeys(2048, 64);
    
    auto member2 = Member::generate(MemberType::User, "Bob", "bob@example.com");
    member2.deriveVotingKeys(2048, 64);
    
    // Keys should be different
    EXPECT_NE(member1.votingPublicKey()->n(), member2.votingPublicKey()->n());
}

TEST_F(PaillierTest, KeyDerivation_RequiresPrivateKey) {
    auto pub_only = Member::fromPublicKey(
        MemberType::User, "Alice", "alice@example.com",
        member->publicKey()
    );
    
    EXPECT_THROW(pub_only.deriveVotingKeys(), std::runtime_error);
}

// Member Voting Key Management Tests
TEST_F(PaillierTest, Member_HasVotingKeys) {
    EXPECT_FALSE(member->hasVotingKeys());
    EXPECT_FALSE(member->hasVotingPrivateKey());
    
    member->deriveVotingKeys(2048, 64);
    
    EXPECT_TRUE(member->hasVotingKeys());
    EXPECT_TRUE(member->hasVotingPrivateKey());
}

TEST_F(PaillierTest, Member_UnloadVotingPrivateKey) {
    member->deriveVotingKeys(2048, 64);
    
    EXPECT_TRUE(member->hasVotingPrivateKey());
    
    member->unloadVotingPrivateKey();
    
    EXPECT_TRUE(member->hasVotingKeys());
    EXPECT_FALSE(member->hasVotingPrivateKey());
}

TEST_F(PaillierTest, Member_LoadExternalVotingKeys) {
    auto authority = Member::generate(MemberType::Admin, "Authority", "auth@example.com");
    authority.deriveVotingKeys(2048, 64);
    
    auto pub = authority.votingPublicKey();
    
    // Load only public key into another member
    member->loadVotingKeys(pub);
    
    EXPECT_TRUE(member->hasVotingKeys());
    EXPECT_FALSE(member->hasVotingPrivateKey());
    
    // Can encrypt but not decrypt
    auto ct = pub->encrypt({0x01});
    EXPECT_NO_THROW(ct);
}

// Large Scale Voting Tests
TEST_F(PaillierTest, LargeScale_100Voters) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    std::vector<std::vector<uint8_t>> votes;
    for (int i = 0; i < 100; i++) {
        votes.push_back(pub->encrypt({0x01}));
    }
    
    auto tally = pub->addition(votes);
    auto count = priv->decrypt(tally);
    
    EXPECT_EQ(count[0], 100);
}

TEST_F(PaillierTest, LargeScale_FiveCandidates50Voters) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    // 5 candidates, 50 voters
    // Distribution: 15, 12, 10, 8, 5
    std::vector<int> distribution = {15, 12, 10, 8, 5};
    std::vector<std::vector<std::vector<uint8_t>>> all_votes(5);
    
    for (size_t candidate = 0; candidate < 5; candidate++) {
        for (int voter = 0; voter < 50; voter++) {
            uint8_t vote = (voter < distribution[candidate]) ? 0x01 : 0x00;
            all_votes[candidate].push_back(pub->encrypt({vote}));
        }
    }
    
    std::vector<uint8_t> results;
    for (size_t candidate = 0; candidate < 5; candidate++) {
        auto tally = pub->addition(all_votes[candidate]);
        auto count = priv->decrypt(tally);
        results.push_back(count[0]);
    }
    
    EXPECT_EQ(results[0], 15);
    EXPECT_EQ(results[1], 12);
    EXPECT_EQ(results[2], 10);
    EXPECT_EQ(results[3], 8);
    EXPECT_EQ(results[4], 5);
}

// Edge Cases and Security Tests
TEST_F(PaillierTest, EdgeCase_SingleVoter) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    auto ct = pub->encrypt({0x01});
    auto pt = priv->decrypt(ct);
    
    EXPECT_EQ(pt[0], 0x01);
}

TEST_F(PaillierTest, EdgeCase_AllZeroVotes) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    std::vector<std::vector<uint8_t>> votes;
    for (int i = 0; i < 10; i++) {
        votes.push_back(pub->encrypt({0x00}));
    }
    
    auto tally = pub->addition(votes);
    auto count = priv->decrypt(tally);
    
    EXPECT_EQ(count[0], 0x00);
}

TEST_F(PaillierTest, Security_DifferentRandomness) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    
    // Same plaintext encrypted twice should produce different ciphertexts
    auto ct1 = pub->encrypt({0x01});
    auto ct2 = pub->encrypt({0x01});
    
    EXPECT_NE(ct1, ct2);
}

TEST_F(PaillierTest, Security_CannotDecryptWithWrongKey) {
    auto member1 = Member::generate(MemberType::User, "Alice", "alice@example.com");
    member1.deriveVotingKeys(2048, 64);
    
    auto member2 = Member::generate(MemberType::User, "Bob", "bob@example.com");
    member2.deriveVotingKeys(2048, 64);
    
    // Encrypt with member1's key
    auto ct = member1.votingPublicKey()->encrypt({0x05});
    
    // Try to decrypt with member2's key - should give wrong result
    auto wrong_pt = member2.votingPrivateKey()->decrypt(ct);
    
    EXPECT_NE(wrong_pt[0], 0x05);
}

TEST_F(PaillierTest, Correctness_AdditionCommutative) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    auto ct1 = pub->encrypt({0x03});
    auto ct2 = pub->encrypt({0x05});
    
    auto sum1 = pub->addition({ct1, ct2});
    auto sum2 = pub->addition({ct2, ct1});
    
    auto result1 = priv->decrypt(sum1);
    auto result2 = priv->decrypt(sum2);
    
    EXPECT_EQ(result1[0], 0x08);
    EXPECT_EQ(result2[0], 0x08);
}

TEST_F(PaillierTest, Correctness_AdditionAssociative) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    auto ct1 = pub->encrypt({0x02});
    auto ct2 = pub->encrypt({0x03});
    auto ct3 = pub->encrypt({0x04});
    
    // (ct1 + ct2) + ct3
    auto temp1 = pub->addition({ct1, ct2});
    auto sum1 = pub->addition({temp1, ct3});
    
    // ct1 + (ct2 + ct3)
    auto temp2 = pub->addition({ct2, ct3});
    auto sum2 = pub->addition({ct1, temp2});
    
    auto result1 = priv->decrypt(sum1);
    auto result2 = priv->decrypt(sum2);
    
    EXPECT_EQ(result1[0], 0x09);
    EXPECT_EQ(result2[0], 0x09);
}

// Real-world Voting Scenarios
TEST_F(PaillierTest, RealWorld_YesNoVote) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    // Simple yes/no vote with 20 voters
    // Yes: 13, No: 7
    std::vector<std::vector<uint8_t>> yes_votes;
    std::vector<std::vector<uint8_t>> no_votes;
    
    for (int i = 0; i < 13; i++) {
        yes_votes.push_back(pub->encrypt({0x01}));
        no_votes.push_back(pub->encrypt({0x00}));
    }
    
    for (int i = 0; i < 7; i++) {
        yes_votes.push_back(pub->encrypt({0x00}));
        no_votes.push_back(pub->encrypt({0x01}));
    }
    
    auto yes_tally = pub->addition(yes_votes);
    auto no_tally = pub->addition(no_votes);
    
    auto yes_count = priv->decrypt(yes_tally);
    auto no_count = priv->decrypt(no_tally);
    
    EXPECT_EQ(yes_count[0], 13);
    EXPECT_EQ(no_count[0], 7);
}

TEST_F(PaillierTest, RealWorld_BoardElection) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    // Board election: 5 candidates, 3 seats, 30 voters
    // Each voter votes for up to 3 candidates
    std::vector<std::vector<std::vector<uint8_t>>> votes(5);
    
    // Simulate voting pattern
    // Candidate 0: 18 votes
    // Candidate 1: 15 votes
    // Candidate 2: 12 votes
    // Candidate 3: 10 votes
    // Candidate 4: 8 votes
    
    int vote_counts[] = {18, 15, 12, 10, 8};
    
    for (int c = 0; c < 5; c++) {
        for (int v = 0; v < 30; v++) {
            uint8_t vote = (v < vote_counts[c]) ? 0x01 : 0x00;
            votes[c].push_back(pub->encrypt({vote}));
        }
    }
    
    std::vector<uint8_t> results;
    for (int c = 0; c < 5; c++) {
        auto tally = pub->addition(votes[c]);
        auto count = priv->decrypt(tally);
        results.push_back(count[0]);
    }
    
    EXPECT_EQ(results[0], 18);
    EXPECT_EQ(results[1], 15);
    EXPECT_EQ(results[2], 12);
    EXPECT_EQ(results[3], 10);
    EXPECT_EQ(results[4], 8);
    
    // Top 3 should be candidates 0, 1, 2
    EXPECT_GT(results[0], results[3]);
    EXPECT_GT(results[1], results[3]);
    EXPECT_GT(results[2], results[3]);
}

// Performance Tests
TEST_F(PaillierTest, Performance_EncryptionSpeed) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 50; i++) {
        pub->encrypt({0x01});
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should complete in reasonable time (less than 10 seconds for 50 encryptions)
    EXPECT_LT(duration.count(), 10000);
}

TEST_F(PaillierTest, Performance_DecryptionSpeed) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    std::vector<std::vector<uint8_t>> ciphertexts;
    for (int i = 0; i < 50; i++) {
        ciphertexts.push_back(pub->encrypt({0x01}));
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (const auto& ct : ciphertexts) {
        priv->decrypt(ct);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_LT(duration.count(), 10000);
}

TEST_F(PaillierTest, Performance_HomomorphicAdditionSpeed) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    
    std::vector<std::vector<uint8_t>> ciphertexts;
    for (int i = 0; i < 100; i++) {
        ciphertexts.push_back(pub->encrypt({0x01}));
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    auto sum = pub->addition(ciphertexts);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_LT(duration.count(), 5000);
}

// Multi-Authority Tests
TEST_F(PaillierTest, MultiAuthority_DifferentKeys) {
    auto authority1 = Member::generate(MemberType::Admin, "Auth1", "auth1@example.com");
    authority1.deriveVotingKeys(2048, 64);
    
    auto authority2 = Member::generate(MemberType::Admin, "Auth2", "auth2@example.com");
    authority2.deriveVotingKeys(2048, 64);
    
    // Each authority can run independent elections
    auto ct1 = authority1.votingPublicKey()->encrypt({0x05});
    auto ct2 = authority2.votingPublicKey()->encrypt({0x03});
    
    auto pt1 = authority1.votingPrivateKey()->decrypt(ct1);
    auto pt2 = authority2.votingPrivateKey()->decrypt(ct2);
    
    EXPECT_EQ(pt1[0], 0x05);
    EXPECT_EQ(pt2[0], 0x03);
}

TEST_F(PaillierTest, MultiAuthority_CannotMixKeys) {
    auto authority1 = Member::generate(MemberType::Admin, "Auth1", "auth1@example.com");
    authority1.deriveVotingKeys(2048, 64);
    
    auto authority2 = Member::generate(MemberType::Admin, "Auth2", "auth2@example.com");
    authority2.deriveVotingKeys(2048, 64);
    
    // Encrypt with authority1
    auto ct = authority1.votingPublicKey()->encrypt({0x07});
    
    // Cannot correctly decrypt with authority2
    auto wrong_pt = authority2.votingPrivateKey()->decrypt(ct);
    EXPECT_NE(wrong_pt[0], 0x07);
}

// Borda Count Voting Test
TEST_F(PaillierTest, BordaCount_RankedVoting) {
    member->deriveVotingKeys(2048, 64);
    auto pub = member->votingPublicKey();
    auto priv = member->votingPrivateKey();
    
    // 3 candidates, 5 voters
    // Each voter ranks all 3: 1st=3pts, 2nd=2pts, 3rd=1pt
    std::vector<std::vector<std::vector<uint8_t>>> points(3);
    
    // Voter 1: A=3, B=2, C=1
    points[0].push_back(pub->encrypt({0x03}));
    points[1].push_back(pub->encrypt({0x02}));
    points[2].push_back(pub->encrypt({0x01}));
    
    // Voter 2: B=3, A=2, C=1
    points[0].push_back(pub->encrypt({0x02}));
    points[1].push_back(pub->encrypt({0x03}));
    points[2].push_back(pub->encrypt({0x01}));
    
    // Voter 3: A=3, C=2, B=1
    points[0].push_back(pub->encrypt({0x03}));
    points[1].push_back(pub->encrypt({0x01}));
    points[2].push_back(pub->encrypt({0x02}));
    
    // Voter 4: B=3, C=2, A=1
    points[0].push_back(pub->encrypt({0x01}));
    points[1].push_back(pub->encrypt({0x03}));
    points[2].push_back(pub->encrypt({0x02}));
    
    // Voter 5: A=3, B=2, C=1
    points[0].push_back(pub->encrypt({0x03}));
    points[1].push_back(pub->encrypt({0x02}));
    points[2].push_back(pub->encrypt({0x01}));
    
    std::vector<uint8_t> totals;
    for (int c = 0; c < 3; c++) {
        auto tally = pub->addition(points[c]);
        auto total = priv->decrypt(tally);
        totals.push_back(total[0]);
    }
    
    // A: 3+2+3+1+3 = 12
    // B: 2+3+1+3+2 = 11
    // C: 1+1+2+2+1 = 7
    EXPECT_EQ(totals[0], 12);
    EXPECT_EQ(totals[1], 11);
    EXPECT_EQ(totals[2], 7);
}
