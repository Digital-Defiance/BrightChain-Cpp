#include <gtest/gtest.h>
#include <brightchain/bulletin_board.hpp>
#include <brightchain/member.hpp>

using namespace brightchain;

class BulletinBoardTest : public ::testing::Test {
protected:
    void SetUp() override {
        authority = std::make_unique<Member>(Member::generate(MemberType::System, "Authority", "authority@example.com"));
        board = std::make_unique<BulletinBoard>(*authority);
    }
    
    std::unique_ptr<Member> authority;
    std::unique_ptr<BulletinBoard> board;
};

TEST_F(BulletinBoardTest, PublishVote) {
    std::vector<uint8_t> pollId = {1, 2, 3};
    std::vector<std::vector<uint8_t>> encryptedVote = {{0x01}, {0x02}};
    std::vector<uint8_t> voterIdHash(32, 0xaa);
    
    auto entry = board->publishVote(pollId, encryptedVote, voterIdHash);
    
    EXPECT_EQ(entry.sequence, 0);
    EXPECT_EQ(entry.pollId, pollId);
    EXPECT_EQ(entry.encryptedVote, encryptedVote);
    EXPECT_EQ(entry.voterIdHash, voterIdHash);
    EXPECT_GT(entry.timestamp, 0);
    EXPECT_FALSE(entry.entryHash.empty());
    EXPECT_FALSE(entry.signature.empty());
}

TEST_F(BulletinBoardTest, SequentialPublishing) {
    std::vector<uint8_t> pollId = {1};
    std::vector<std::vector<uint8_t>> vote = {{0x01}};
    std::vector<uint8_t> voterHash(32);
    
    auto entry1 = board->publishVote(pollId, vote, voterHash);
    auto entry2 = board->publishVote(pollId, vote, voterHash);
    auto entry3 = board->publishVote(pollId, vote, voterHash);
    
    EXPECT_EQ(entry1.sequence, 0);
    EXPECT_EQ(entry2.sequence, 1);
    EXPECT_EQ(entry3.sequence, 2);
}

TEST_F(BulletinBoardTest, GetEntriesForPoll) {
    std::vector<uint8_t> poll1 = {1};
    std::vector<uint8_t> poll2 = {2};
    std::vector<std::vector<uint8_t>> vote = {{0x01}};
    std::vector<uint8_t> voterHash(32);
    
    board->publishVote(poll1, vote, voterHash);
    board->publishVote(poll1, vote, voterHash);
    board->publishVote(poll2, vote, voterHash);
    board->publishVote(poll1, vote, voterHash);
    
    auto entries = board->getEntries(poll1);
    EXPECT_EQ(entries.size(), 3);
    
    auto entries2 = board->getEntries(poll2);
    EXPECT_EQ(entries2.size(), 1);
}

TEST_F(BulletinBoardTest, GetAllEntries) {
    std::vector<uint8_t> pollId = {1};
    std::vector<std::vector<uint8_t>> vote = {{0x01}};
    std::vector<uint8_t> voterHash(32);
    
    board->publishVote(pollId, vote, voterHash);
    board->publishVote(pollId, vote, voterHash);
    board->publishVote(pollId, vote, voterHash);
    
    auto entries = board->getAllEntries();
    EXPECT_EQ(entries.size(), 3);
}

TEST_F(BulletinBoardTest, VerifyEntry) {
    std::vector<uint8_t> pollId = {1};
    std::vector<std::vector<uint8_t>> vote = {{0x01}};
    std::vector<uint8_t> voterHash(32);
    
    auto entry = board->publishVote(pollId, vote, voterHash);
    
    EXPECT_TRUE(board->verifyEntry(entry));
}

TEST_F(BulletinBoardTest, PublishTally) {
    std::vector<uint8_t> pollId = {1};
    std::vector<std::vector<uint8_t>> tallies = {{0x05}, {0x03}};
    std::vector<std::string> choices = {"Alice", "Bob"};
    std::vector<std::vector<std::vector<uint8_t>>> encryptedVotes = {{{0x01}}, {{0x02}}};
    
    auto proof = board->publishTally(pollId, tallies, choices, encryptedVotes);
    
    EXPECT_EQ(proof.pollId, pollId);
    EXPECT_EQ(proof.tallies, tallies);
    EXPECT_EQ(proof.choices, choices);
    EXPECT_GT(proof.timestamp, 0);
    EXPECT_FALSE(proof.votesHash.empty());
    EXPECT_FALSE(proof.decryptionProof.empty());
    EXPECT_FALSE(proof.signature.empty());
}

TEST_F(BulletinBoardTest, GetTallyProof) {
    std::vector<uint8_t> pollId = {1};
    std::vector<std::vector<uint8_t>> tallies = {{0x05}};
    std::vector<std::string> choices = {"Alice"};
    std::vector<std::vector<std::vector<uint8_t>>> votes = {{{0x01}}};
    
    board->publishTally(pollId, tallies, choices, votes);
    
    auto proof = board->getTallyProof(pollId);
    ASSERT_NE(proof, nullptr);
    EXPECT_EQ(proof->pollId, pollId);
}

TEST_F(BulletinBoardTest, VerifyTallyProof) {
    std::vector<uint8_t> pollId = {1};
    std::vector<std::vector<uint8_t>> tallies = {{0x05}};
    std::vector<std::string> choices = {"Alice"};
    std::vector<std::vector<std::vector<uint8_t>>> votes = {{{0x01}}};
    
    auto proof = board->publishTally(pollId, tallies, choices, votes);
    
    EXPECT_TRUE(board->verifyTallyProof(proof));
}

TEST_F(BulletinBoardTest, MerkleTreeVerification) {
    std::vector<uint8_t> pollId = {1};
    std::vector<std::vector<uint8_t>> vote = {{0x01}};
    std::vector<uint8_t> voterHash(32);
    
    board->publishVote(pollId, vote, voterHash);
    board->publishVote(pollId, vote, voterHash);
    board->publishVote(pollId, vote, voterHash);
    
    EXPECT_TRUE(board->verifyMerkleTree());
}

TEST_F(BulletinBoardTest, ComputeMerkleRoot) {
    std::vector<uint8_t> pollId = {1};
    std::vector<std::vector<uint8_t>> vote = {{0x01}};
    std::vector<uint8_t> voterHash(32);
    
    auto root1 = board->computeMerkleRoot();
    EXPECT_EQ(root1.length(), 64); // Empty board = 32 bytes of zeros as hex
    
    board->publishVote(pollId, vote, voterHash);
    auto root2 = board->computeMerkleRoot();
    EXPECT_NE(root2, root1);
}

TEST_F(BulletinBoardTest, ExportBoard) {
    std::vector<uint8_t> pollId = {1};
    std::vector<std::vector<uint8_t>> vote = {{0x01}};
    std::vector<uint8_t> voterHash(32);
    
    board->publishVote(pollId, vote, voterHash);
    board->publishVote(pollId, vote, voterHash);
    
    auto exported = board->exportBoard();
    EXPECT_GT(exported.size(), 0);
}

TEST_F(BulletinBoardTest, LargeVolumePublishing) {
    std::vector<uint8_t> pollId = {1};
    std::vector<std::vector<uint8_t>> vote = {{0x01}};
    std::vector<uint8_t> voterHash(32);
    
    for (int i = 0; i < 100; i++) {
        board->publishVote(pollId, vote, voterHash);
    }
    
    EXPECT_EQ(board->getAllEntries().size(), 100);
    EXPECT_TRUE(board->verifyMerkleTree());
}
