#include <gtest/gtest.h>
#include <brightchain/audit_log.hpp>
#include <brightchain/member.hpp>

using namespace brightchain;

class AuditLogTest : public ::testing::Test {
protected:
    void SetUp() override {
        authority_ = std::make_shared<Member>(Member::generate(MemberType::Admin, "Authority", "auth@test.com"));
        pollId_ = {1, 2, 3, 4};
    }

    std::shared_ptr<Member> authority_;
    std::vector<uint8_t> pollId_;
};

TEST_F(AuditLogTest, RecordsPollCreation) {
    AuditLog log(*authority_);
    auto entry = log.recordPollCreated(pollId_);
    
    EXPECT_EQ(entry.sequence, 0);
    EXPECT_EQ(entry.eventType, AuditEventType::PollCreated);
    EXPECT_EQ(entry.pollId, pollId_);
    EXPECT_TRUE(entry.authorityId.has_value());
}

TEST_F(AuditLogTest, RecordsVoteCast) {
    AuditLog log(*authority_);
    log.recordPollCreated(pollId_);
    
    std::vector<uint8_t> voterHash = {5, 6, 7, 8};
    auto entry = log.recordVoteCast(pollId_, voterHash);
    
    EXPECT_EQ(entry.sequence, 1);
    EXPECT_EQ(entry.eventType, AuditEventType::VoteCast);
    EXPECT_TRUE(entry.voterIdHash.has_value());
    EXPECT_EQ(*entry.voterIdHash, voterHash);
}

TEST_F(AuditLogTest, RecordsPollClosure) {
    AuditLog log(*authority_);
    log.recordPollCreated(pollId_);
    auto entry = log.recordPollClosed(pollId_);
    
    EXPECT_EQ(entry.sequence, 1);
    EXPECT_EQ(entry.eventType, AuditEventType::PollClosed);
}

TEST_F(AuditLogTest, MaintainsHashChain) {
    AuditLog log(*authority_);
    auto entry1 = log.recordPollCreated(pollId_);
    auto entry2 = log.recordVoteCast(pollId_, {1, 2, 3});
    
    EXPECT_EQ(entry2.previousHash, entry1.entryHash);
}

TEST_F(AuditLogTest, VerifiesChain) {
    AuditLog log(*authority_);
    log.recordPollCreated(pollId_);
    log.recordVoteCast(pollId_, {1, 2, 3});
    log.recordPollClosed(pollId_);
    
    EXPECT_TRUE(log.verifyChain());
}

TEST_F(AuditLogTest, FiltersEntriesByPoll) {
    AuditLog log(*authority_);
    std::vector<uint8_t> poll1 = {1, 2, 3};
    std::vector<uint8_t> poll2 = {4, 5, 6};
    
    log.recordPollCreated(poll1);
    log.recordPollCreated(poll2);
    log.recordVoteCast(poll1, {7, 8, 9});
    
    auto entries = log.getEntriesForPoll(poll1);
    EXPECT_EQ(entries.size(), 2);
}

TEST_F(AuditLogTest, IncrementsSequence) {
    AuditLog log(*authority_);
    auto e1 = log.recordPollCreated(pollId_);
    auto e2 = log.recordVoteCast(pollId_, {1});
    auto e3 = log.recordPollClosed(pollId_);
    
    EXPECT_EQ(e1.sequence, 0);
    EXPECT_EQ(e2.sequence, 1);
    EXPECT_EQ(e3.sequence, 2);
}
