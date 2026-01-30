#include <gtest/gtest.h>
#include <brightchain/poll.hpp>
#include <brightchain/vote_encoder.hpp>
#include <brightchain/member.hpp>

using namespace brightchain;

class PollTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Generate test keys
        auto keyPair = deriveVotingKeysFromECDH(
            std::vector<uint8_t>(32, 0x01),
            std::vector<uint8_t>(33, 0x02),
            512,
            16
        );
        publicKey_ = keyPair.publicKey;
        privateKey_ = keyPair.privateKey;

        // Create authority member
        authority_ = std::make_shared<Member>(Member::generate(MemberType::Admin, "Authority", "authority@test.com"));
        authority_->loadVotingKeys(publicKey_, privateKey_);

        // Create voter members
        for (int i = 0; i < 5; i++) {
            auto voter = Member::generate(MemberType::User, "Voter" + std::to_string(i), "voter" + std::to_string(i) + "@test.com");
            voter.loadVotingKeys(publicKey_, privateKey_);
            voters_.push_back(voter);
        }

        // Create poll
        pollId_ = {1, 2, 3};
        choices_ = {"A", "B", "C"};
    }

    std::vector<uint8_t> intToBytes(int64_t value) {
        std::vector<uint8_t> result;
        if (value == 0) {
            result.push_back(0);
            return result;
        }
        
        uint64_t absValue = value;
        while (absValue > 0) {
            result.push_back(static_cast<uint8_t>(absValue & 0xFF));
            absValue >>= 8;
        }
        return result;
    }

    std::shared_ptr<PaillierPublicKey> publicKey_;
    std::shared_ptr<PaillierPrivateKey> privateKey_;
    std::shared_ptr<Member> authority_;  // Use shared_ptr to avoid copy issues
    std::vector<Member> voters_;
    std::vector<uint8_t> pollId_;
    std::vector<std::string> choices_;
};

TEST_F(PollTest, Construction_CreatesWithValidParameters) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);

    EXPECT_EQ(poll.id(), pollId_);
    EXPECT_EQ(poll.choices(), choices_);
    EXPECT_EQ(poll.method(), VotingMethod::Plurality);
    EXPECT_FALSE(poll.isClosed());
    EXPECT_EQ(poll.voterCount(), 0);
}

TEST_F(PollTest, Construction_RejectsLessThan2Choices) {
    EXPECT_THROW(
        Poll(pollId_, {"Only One"}, VotingMethod::Plurality, *authority_, publicKey_),
        std::invalid_argument
    );
}

TEST_F(PollTest, Construction_RejectsAuthorityWithoutVotingKeys) {
    Member badAuthority = Member::generate(MemberType::Admin, "Bad", "bad@test.com");
    
    EXPECT_THROW(
        Poll(pollId_, choices_, VotingMethod::Plurality, badAuthority, publicKey_),
        std::invalid_argument
    );
}

TEST_F(PollTest, Voting_AcceptsValidVote) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    
    VoteEncoder encoder(publicKey_);
    auto vote = encoder.encodePlurality(0, 3);

    auto receipt = poll.vote(voters_[0], vote);

    EXPECT_EQ(receipt.voterId, voters_[0].idBytes());
    EXPECT_EQ(receipt.pollId, pollId_);
}

TEST_F(PollTest, Voting_IncrementsVoterCount) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);
    auto vote = encoder.encodePlurality(0, 3);

    poll.vote(voters_[0], vote);
    EXPECT_EQ(poll.voterCount(), 1);

    poll.vote(voters_[1], vote);
    EXPECT_EQ(poll.voterCount(), 2);
}

TEST_F(PollTest, Voting_PreventsDoubleVoting) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);
    auto vote = encoder.encodePlurality(0, 3);

    poll.vote(voters_[0], vote);

    EXPECT_THROW(
        poll.vote(voters_[0], vote),
        std::runtime_error
    );
}

TEST_F(PollTest, Voting_PreventsVotingAfterClose) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);
    auto vote = encoder.encodePlurality(0, 3);

    poll.close();

    EXPECT_THROW(
        poll.vote(voters_[0], vote),
        std::runtime_error
    );
}

TEST_F(PollTest, Voting_ValidatesVoteStructureForPlurality) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    
    EncryptedVote invalidVote;
    invalidVote.encrypted = {intToBytes(1), intToBytes(2), intToBytes(3)};

    EXPECT_THROW(
        poll.vote(voters_[0], invalidVote),
        std::invalid_argument
    );
}

TEST_F(PollTest, Voting_ValidatesChoiceIndexBounds) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    
    EncryptedVote invalidVote;
    invalidVote.choiceIndex = 5;
    invalidVote.encrypted = {intToBytes(1), intToBytes(2), intToBytes(3)};

    EXPECT_THROW(
        poll.vote(voters_[0], invalidVote),
        std::invalid_argument
    );
}

TEST_F(PollTest, Voting_ValidatesNegativeChoiceIndex) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    
    EncryptedVote invalidVote;
    invalidVote.choiceIndex = -1;
    invalidVote.encrypted = {intToBytes(1), intToBytes(2), intToBytes(3)};

    EXPECT_THROW(
        poll.vote(voters_[0], invalidVote),
        std::invalid_argument
    );
}

TEST_F(PollTest, Receipt_GeneratesUniqueReceipts) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);
    auto vote = encoder.encodePlurality(0, 3);

    auto receipt1 = poll.vote(voters_[0], vote);
    auto receipt2 = poll.vote(voters_[1], vote);

    EXPECT_NE(receipt1.signature, receipt2.signature);
    EXPECT_NE(receipt1.nonce, receipt2.nonce);
}

TEST_F(PollTest, Receipt_IncludesTimestamp) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);
    auto vote = encoder.encodePlurality(0, 3);

    auto before = std::chrono::system_clock::now();
    auto receipt = poll.vote(voters_[0], vote);
    auto after = std::chrono::system_clock::now();

    auto beforeMs = std::chrono::duration_cast<std::chrono::milliseconds>(before.time_since_epoch()).count();
    auto afterMs = std::chrono::duration_cast<std::chrono::milliseconds>(after.time_since_epoch()).count();

    EXPECT_GE(receipt.timestamp, beforeMs);
    EXPECT_LE(receipt.timestamp, afterMs);
}

TEST_F(PollTest, ReceiptVerification_VerifiesValidReceipt) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);
    auto vote = encoder.encodePlurality(0, 3);

    auto receipt = poll.vote(voters_[0], vote);
    bool isValid = poll.verifyReceipt(voters_[0], receipt);

    EXPECT_TRUE(isValid);
}

TEST_F(PollTest, ReceiptVerification_RejectsReceiptFromNonVoter) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);
    auto vote = encoder.encodePlurality(0, 3);

    auto receipt = poll.vote(voters_[0], vote);
    bool isValid = poll.verifyReceipt(voters_[1], receipt);

    EXPECT_FALSE(isValid);
}

TEST_F(PollTest, Lifecycle_StartsAsOpen) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);

    EXPECT_FALSE(poll.isClosed());
    EXPECT_FALSE(poll.closedAt().has_value());
}

TEST_F(PollTest, Lifecycle_ClosesPoll) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);

    auto before = std::chrono::system_clock::now();
    poll.close();
    auto after = std::chrono::system_clock::now();

    EXPECT_TRUE(poll.isClosed());
    EXPECT_TRUE(poll.closedAt().has_value());

    auto beforeMs = std::chrono::duration_cast<std::chrono::milliseconds>(before.time_since_epoch()).count();
    auto afterMs = std::chrono::duration_cast<std::chrono::milliseconds>(after.time_since_epoch()).count();

    EXPECT_GE(poll.closedAt().value(), beforeMs);
    EXPECT_LE(poll.closedAt().value(), afterMs);
}

TEST_F(PollTest, Lifecycle_PreventsDoubleClosing) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);

    poll.close();

    EXPECT_THROW(
        poll.close(),
        std::runtime_error
    );
}

TEST_F(PollTest, EncryptedVotesAccess_ReturnsEncryptedVotes) {
    Poll poll(pollId_, choices_, VotingMethod::Plurality, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);
    auto vote = encoder.encodePlurality(0, 3);

    poll.vote(voters_[0], vote);
    poll.vote(voters_[1], vote);

    auto encryptedVotes = poll.getEncryptedVotes();

    EXPECT_EQ(encryptedVotes.size(), 2);
}

TEST_F(PollTest, ApprovalVoting_ValidatesVoteStructure) {
    Poll poll(pollId_, choices_, VotingMethod::Approval, *authority_, publicKey_);

    EncryptedVote invalidVote;
    invalidVote.encrypted = {intToBytes(1), intToBytes(2), intToBytes(3)};

    EXPECT_THROW(
        poll.vote(voters_[0], invalidVote),
        std::invalid_argument
    );
}

TEST_F(PollTest, ApprovalVoting_ValidatesChoiceIndices) {
    Poll poll(pollId_, choices_, VotingMethod::Approval, *authority_, publicKey_);

    EncryptedVote invalidVote;
    invalidVote.choices = {0, 5};
    invalidVote.encrypted = {intToBytes(1), intToBytes(2), intToBytes(3)};

    EXPECT_THROW(
        poll.vote(voters_[0], invalidVote),
        std::invalid_argument
    );
}

TEST_F(PollTest, WeightedVoting_ValidatesWeightPresence) {
    Poll poll(pollId_, {"A", "B"}, VotingMethod::Weighted, *authority_, publicKey_, intToBytes(1000));

    EncryptedVote invalidVote;
    invalidVote.choiceIndex = 0;
    invalidVote.encrypted = {intToBytes(1), intToBytes(2)};

    EXPECT_THROW(
        poll.vote(voters_[0], invalidVote),
        std::invalid_argument
    );
}

TEST_F(PollTest, RankedVoting_ValidatesRankingsPresence) {
    Poll poll(pollId_, choices_, VotingMethod::Borda, *authority_, publicKey_);

    EncryptedVote invalidVote;
    invalidVote.encrypted = {intToBytes(1), intToBytes(2), intToBytes(3)};

    EXPECT_THROW(
        poll.vote(voters_[0], invalidVote),
        std::invalid_argument
    );
}

TEST_F(PollTest, RankedVoting_ValidatesRankingIndices) {
    Poll poll(pollId_, choices_, VotingMethod::Borda, *authority_, publicKey_);

    EncryptedVote invalidVote;
    invalidVote.rankings = {0, 5};
    invalidVote.encrypted = {intToBytes(1), intToBytes(2), intToBytes(3)};

    EXPECT_THROW(
        poll.vote(voters_[0], invalidVote),
        std::invalid_argument
    );
}

TEST_F(PollTest, RankedVoting_RejectsDuplicateRankings) {
    Poll poll(pollId_, choices_, VotingMethod::Borda, *authority_, publicKey_);

    EncryptedVote invalidVote;
    invalidVote.rankings = {0, 1, 0};
    invalidVote.encrypted = {intToBytes(1), intToBytes(2), intToBytes(3)};

    EXPECT_THROW(
        poll.vote(voters_[0], invalidVote),
        std::invalid_argument
    );
}
