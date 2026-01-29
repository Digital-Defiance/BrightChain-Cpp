#include <gtest/gtest.h>
#include <brightchain/poll_tallier.hpp>
#include <brightchain/poll.hpp>
#include <brightchain/vote_encoder.hpp>
#include <brightchain/member.hpp>

using namespace brightchain;

class PollTallierTest : public ::testing::Test {
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
        for (int i = 0; i < 10; i++) {
            auto voter = Member::generate(MemberType::User, "Voter" + std::to_string(i), "voter" + std::to_string(i) + "@test.com");
            voter.loadVotingKeys(publicKey_, privateKey_);
            voters_.push_back(voter);
        }

        tallier_ = std::make_unique<PollTallier>(*authority_, privateKey_, publicKey_);
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

    int64_t bytesToInt(const std::vector<uint8_t>& bytes) {
        if (bytes.empty()) return 0;
        
        int64_t result = 0;
        for (size_t i = 0; i < bytes.size() && i < 8; i++) {
            result |= (static_cast<int64_t>(bytes[i]) << (i * 8));
        }
        return result;
    }

    std::shared_ptr<PaillierPublicKey> publicKey_;
    std::shared_ptr<PaillierPrivateKey> privateKey_;
    std::shared_ptr<Member> authority_;  // Use shared_ptr
    std::vector<Member> voters_;
    std::unique_ptr<PollTallier> tallier_;
};

TEST_F(PollTallierTest, PluralityTally_DeterminesWinner) {
    Poll poll({1}, {"A", "B", "C"}, VotingMethod::Plurality, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);

    // 5 votes for A, 3 for B, 2 for C
    for (int i = 0; i < 5; i++) {
        poll.vote(voters_[i], encoder.encodePlurality(0, 3));
    }
    for (int i = 5; i < 8; i++) {
        poll.vote(voters_[i], encoder.encodePlurality(1, 3));
    }
    for (int i = 8; i < 10; i++) {
        poll.vote(voters_[i], encoder.encodePlurality(2, 3));
    }

    poll.close();
    auto results = tallier_->tally(poll);

    EXPECT_EQ(results.method, VotingMethod::Plurality);
    EXPECT_TRUE(results.winner.has_value());
    EXPECT_EQ(results.winner.value(), 0);
    EXPECT_EQ(bytesToInt(results.tallies[0]), 5);
    EXPECT_EQ(bytesToInt(results.tallies[1]), 3);
    EXPECT_EQ(bytesToInt(results.tallies[2]), 2);
}

TEST_F(PollTallierTest, ApprovalTally_CountsMultipleApprovals) {
    Poll poll({1}, {"A", "B", "C"}, VotingMethod::Approval, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);

    // Voters approve multiple candidates
    poll.vote(voters_[0], encoder.encodeApproval({0, 1}, 3));
    poll.vote(voters_[1], encoder.encodeApproval({0, 2}, 3));
    poll.vote(voters_[2], encoder.encodeApproval({1, 2}, 3));

    poll.close();
    auto results = tallier_->tally(poll);

    EXPECT_EQ(results.method, VotingMethod::Approval);
    EXPECT_EQ(bytesToInt(results.tallies[0]), 2);
    EXPECT_EQ(bytesToInt(results.tallies[1]), 2);
    EXPECT_EQ(bytesToInt(results.tallies[2]), 2);
}

TEST_F(PollTallierTest, WeightedTally_SumsWeights) {
    Poll poll({1}, {"A", "B"}, VotingMethod::Weighted, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);

    poll.vote(voters_[0], encoder.encodeWeighted(0, intToBytes(100), 2));
    poll.vote(voters_[1], encoder.encodeWeighted(0, intToBytes(200), 2));
    poll.vote(voters_[2], encoder.encodeWeighted(1, intToBytes(150), 2));

    poll.close();
    auto results = tallier_->tally(poll);

    EXPECT_EQ(results.method, VotingMethod::Weighted);
    EXPECT_TRUE(results.winner.has_value());
    EXPECT_EQ(results.winner.value(), 0);
    EXPECT_EQ(bytesToInt(results.tallies[0]), 300);
    EXPECT_EQ(bytesToInt(results.tallies[1]), 150);
}

TEST_F(PollTallierTest, BordaTally_AssignsPointsByRank) {
    Poll poll({1}, {"A", "B", "C"}, VotingMethod::Borda, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);

    // Voter 1: A=3, B=2, C=1
    poll.vote(voters_[0], encoder.encodeBorda({0, 1, 2}, 3));
    // Voter 2: B=3, A=2, C=1
    poll.vote(voters_[1], encoder.encodeBorda({1, 0, 2}, 3));
    // Voter 3: C=3, A=2, B=1
    poll.vote(voters_[2], encoder.encodeBorda({2, 0, 1}, 3));

    poll.close();
    auto results = tallier_->tally(poll);

    EXPECT_EQ(results.method, VotingMethod::Borda);
    // A: 3+2+2 = 7
    // B: 2+3+1 = 6
    // C: 1+1+3 = 5
    EXPECT_EQ(bytesToInt(results.tallies[0]), 7);
    EXPECT_EQ(bytesToInt(results.tallies[1]), 6);
    EXPECT_EQ(bytesToInt(results.tallies[2]), 5);
    EXPECT_TRUE(results.winner.has_value());
    EXPECT_EQ(results.winner.value(), 0);
}

TEST_F(PollTallierTest, RankedChoiceTally_EliminatesLowestUntilMajority) {
    Poll poll({1}, {"A", "B", "C"}, VotingMethod::RankedChoice, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);

    // 4 voters: A > B > C
    for (int i = 0; i < 4; i++) {
        poll.vote(voters_[i], encoder.encodeRankedChoice({0, 1, 2}, 3));
    }
    // 3 voters: B > C > A
    for (int i = 4; i < 7; i++) {
        poll.vote(voters_[i], encoder.encodeRankedChoice({1, 2, 0}, 3));
    }
    // 2 voters: C > B > A
    for (int i = 7; i < 9; i++) {
        poll.vote(voters_[i], encoder.encodeRankedChoice({2, 1, 0}, 3));
    }

    poll.close();
    auto results = tallier_->tally(poll);

    EXPECT_EQ(results.method, VotingMethod::RankedChoice);
    EXPECT_TRUE(results.winner.has_value());
    EXPECT_TRUE(results.rounds.has_value());
    EXPECT_GT(results.rounds.value().size(), 0);
}

TEST_F(PollTallierTest, Tally_ThrowsIfPollNotClosed) {
    Poll poll({1}, {"A", "B"}, VotingMethod::Plurality, *authority_, publicKey_);

    EXPECT_THROW(
        tallier_->tally(poll),
        std::runtime_error
    );
}

TEST_F(PollTallierTest, Tally_HandlesEmptyPoll) {
    Poll poll({1}, {"A", "B"}, VotingMethod::Plurality, *authority_, publicKey_);
    poll.close();

    auto results = tallier_->tally(poll);

    EXPECT_EQ(results.voterCount, 0);
    EXPECT_EQ(bytesToInt(results.tallies[0]), 0);
    EXPECT_EQ(bytesToInt(results.tallies[1]), 0);
}

TEST_F(PollTallierTest, Tally_HandlesTie) {
    Poll poll({1}, {"A", "B"}, VotingMethod::Plurality, *authority_, publicKey_);
    VoteEncoder encoder(publicKey_);

    poll.vote(voters_[0], encoder.encodePlurality(0, 2));
    poll.vote(voters_[1], encoder.encodePlurality(1, 2));

    poll.close();
    auto results = tallier_->tally(poll);

    EXPECT_FALSE(results.winner.has_value());
    EXPECT_TRUE(results.winners.has_value());
    EXPECT_EQ(results.winners.value().size(), 2);
}

TEST_F(PollTallierTest, QuadraticTally_SquaresWeights) {
    Poll poll({1}, {"A", "B"}, VotingMethod::Quadratic, *authority_, publicKey_, std::nullopt, true);
    VoteEncoder encoder(publicKey_);

    // A: 3^2 + 2^2 = 9 + 4 = 13
    poll.vote(voters_[0], encoder.encodeWeighted(0, intToBytes(3), 2));
    poll.vote(voters_[1], encoder.encodeWeighted(0, intToBytes(2), 2));
    // B: 4^2 = 16
    poll.vote(voters_[2], encoder.encodeWeighted(1, intToBytes(4), 2));

    poll.close();
    auto results = tallier_->tally(poll);

    EXPECT_EQ(results.method, VotingMethod::Quadratic);
    EXPECT_TRUE(results.winner.has_value());
    EXPECT_EQ(results.winner.value(), 1);
    EXPECT_EQ(bytesToInt(results.tallies[0]), 13);
    EXPECT_EQ(bytesToInt(results.tallies[1]), 16);
}

TEST_F(PollTallierTest, ConsensusTally_Requires95Percent) {
    Poll poll({1}, {"A", "B"}, VotingMethod::Consensus, *authority_, publicKey_, std::nullopt, true);
    VoteEncoder encoder(publicKey_);

    // 9 out of 10 vote for A (90% - not enough)
    for (int i = 0; i < 9; i++) {
        poll.vote(voters_[i], encoder.encodePlurality(0, 2));
    }
    poll.vote(voters_[9], encoder.encodePlurality(1, 2));

    poll.close();
    auto results = tallier_->tally(poll);

    EXPECT_EQ(results.method, VotingMethod::Consensus);
    // No winner because 90% < 95%
    EXPECT_FALSE(results.winner.has_value());
}

TEST_F(PollTallierTest, ConsentBasedTally_RejectsWithObjections) {
    Poll poll({1}, {"A", "B"}, VotingMethod::ConsentBased, *authority_, publicKey_, std::nullopt, true);
    VoteEncoder encoder(publicKey_);

    // Most support A, but one strong objection
    for (int i = 0; i < 8; i++) {
        poll.vote(voters_[i], encoder.encodeWeighted(0, intToBytes(1), 2));
    }
    // Strong objection to A (negative weight)
    poll.vote(voters_[8], encoder.encodeWeighted(0, intToBytes(-1), 2));
    // Support B with no objections
    poll.vote(voters_[9], encoder.encodeWeighted(1, intToBytes(1), 2));

    poll.close();
    auto results = tallier_->tally(poll);

    EXPECT_EQ(results.method, VotingMethod::ConsentBased);
    // A has objection, B has none
    EXPECT_TRUE(results.winner.has_value());
    EXPECT_EQ(results.winner.value(), 1);
}
