#include <gtest/gtest.h>
#include <brightchain/poll_factory.hpp>
#include <brightchain/member.hpp>
#include <brightchain/paillier.hpp>

using namespace brightchain;

class PollFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto keyPair = deriveVotingKeysFromECDH(
            std::vector<uint8_t>(32, 0x01),
            std::vector<uint8_t>(33, 0x02),
            512, 16
        );
        authority_ = std::make_shared<Member>(Member::generate(MemberType::Admin, "Authority", "auth@test.com"));
        authority_->loadVotingKeys(keyPair.publicKey, keyPair.privateKey);
        choices_ = {"A", "B", "C"};
    }

    std::shared_ptr<Member> authority_;
    std::vector<std::string> choices_;
};

TEST_F(PollFactoryTest, CreatesPlurality) {
    auto poll = PollFactory::createPlurality(choices_, *authority_);
    EXPECT_EQ(poll->method(), VotingMethod::Plurality);
    EXPECT_EQ(poll->choices(), choices_);
}

TEST_F(PollFactoryTest, CreatesApproval) {
    auto poll = PollFactory::createApproval(choices_, *authority_);
    EXPECT_EQ(poll->method(), VotingMethod::Approval);
}

TEST_F(PollFactoryTest, CreatesWeighted) {
    std::vector<uint8_t> maxWeight = {100, 0};
    auto poll = PollFactory::createWeighted(choices_, *authority_, maxWeight);
    EXPECT_EQ(poll->method(), VotingMethod::Weighted);
}

TEST_F(PollFactoryTest, CreatesBorda) {
    auto poll = PollFactory::createBorda(choices_, *authority_);
    EXPECT_EQ(poll->method(), VotingMethod::Borda);
}

TEST_F(PollFactoryTest, CreatesRankedChoice) {
    auto poll = PollFactory::createRankedChoice(choices_, *authority_);
    EXPECT_EQ(poll->method(), VotingMethod::RankedChoice);
}

TEST_F(PollFactoryTest, GeneratesUniquePollIds) {
    auto poll1 = PollFactory::createPlurality(choices_, *authority_);
    auto poll2 = PollFactory::createPlurality(choices_, *authority_);
    EXPECT_NE(poll1->id(), poll2->id());
}

TEST_F(PollFactoryTest, ThrowsWithoutVotingKeys) {
    Member badAuth = Member::generate(MemberType::Admin, "Bad", "bad@test.com");
    EXPECT_THROW(
        PollFactory::createPlurality(choices_, badAuth),
        std::invalid_argument
    );
}
