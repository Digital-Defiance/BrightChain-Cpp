#include <gtest/gtest.h>
#include <brightchain/event_logger.hpp>
#include <brightchain/member.hpp>

using namespace brightchain;

class EventLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger = std::make_unique<EventLogger>();
    }
    
    std::unique_ptr<EventLogger> logger;
};

TEST_F(EventLoggerTest, LogPollCreated) {
    std::vector<uint8_t> pollId = {1, 2, 3};
    std::vector<uint8_t> creatorId = {4, 5, 6};
    EventLogEntry::PollConfiguration config;
    config.method = VotingMethod::Plurality;
    config.choices = {"Alice", "Bob", "Charlie"};
    
    auto entry = logger->logPollCreated(pollId, creatorId, config);
    
    EXPECT_EQ(entry.eventType, EventType::PollCreated);
    EXPECT_EQ(entry.pollId, pollId);
    EXPECT_EQ(entry.creatorId, creatorId);
    EXPECT_TRUE(entry.configuration.has_value());
    EXPECT_EQ(entry.configuration->method, VotingMethod::Plurality);
    EXPECT_EQ(entry.sequence, 0);
    EXPECT_GT(entry.timestamp, 0);
}

TEST_F(EventLoggerTest, LogVoteCast) {
    std::vector<uint8_t> pollId = {1, 2, 3};
    std::vector<uint8_t> voterToken = {7, 8, 9};
    
    auto entry = logger->logVoteCast(pollId, voterToken);
    
    EXPECT_EQ(entry.eventType, EventType::VoteCast);
    EXPECT_EQ(entry.pollId, pollId);
    EXPECT_EQ(entry.voterToken, voterToken);
    EXPECT_EQ(entry.sequence, 0);
}

TEST_F(EventLoggerTest, LogVoteCastWithMetadata) {
    std::vector<uint8_t> pollId = {1};
    std::vector<uint8_t> voterToken = {2};
    std::map<std::string, nlohmann::json> metadata;
    metadata["ipAddress"] = "192.168.1.1";
    metadata["userAgent"] = "Mozilla/5.0";
    
    auto entry = logger->logVoteCast(pollId, voterToken, metadata);
    
    EXPECT_TRUE(entry.metadata.has_value());
    EXPECT_EQ((*entry.metadata)["ipAddress"], "192.168.1.1");
}

TEST_F(EventLoggerTest, SequentialNumbers) {
    std::vector<uint8_t> pollId = {1};
    
    auto entry1 = logger->logVoteCast(pollId, {1});
    auto entry2 = logger->logVoteCast(pollId, {2});
    auto entry3 = logger->logVoteCast(pollId, {3});
    
    EXPECT_EQ(entry1.sequence, 0);
    EXPECT_EQ(entry2.sequence, 1);
    EXPECT_EQ(entry3.sequence, 2);
}

TEST_F(EventLoggerTest, LogPollClosed) {
    std::vector<uint8_t> pollId = {1, 2, 3};
    std::vector<uint8_t> tallyHash(32, 0xab);
    
    auto entry = logger->logPollClosed(pollId, tallyHash);
    
    EXPECT_EQ(entry.eventType, EventType::PollClosed);
    EXPECT_EQ(entry.pollId, pollId);
    EXPECT_EQ(entry.tallyHash, tallyHash);
}

TEST_F(EventLoggerTest, MicrosecondTimestamps) {
    std::vector<uint8_t> pollId = {1};
    
    auto entry1 = logger->logVoteCast(pollId, {1});
    auto entry2 = logger->logVoteCast(pollId, {2});
    
    EXPECT_GE(entry2.timestamp, entry1.timestamp);
    EXPECT_GT(entry1.timestamp, 0);
}

TEST_F(EventLoggerTest, VerifySequence) {
    std::vector<uint8_t> pollId = {1};
    
    logger->logVoteCast(pollId, {1});
    logger->logVoteCast(pollId, {2});
    logger->logVoteCast(pollId, {3});
    
    EXPECT_TRUE(logger->verifySequence());
}

TEST_F(EventLoggerTest, GetAllEvents) {
    std::vector<uint8_t> pollId = {1};
    
    logger->logVoteCast(pollId, {1});
    logger->logVoteCast(pollId, {2});
    logger->logVoteCast(pollId, {3});
    
    auto events = logger->getEvents();
    EXPECT_EQ(events.size(), 3);
}

TEST_F(EventLoggerTest, GetEventsForPoll) {
    std::vector<uint8_t> poll1 = {1};
    std::vector<uint8_t> poll2 = {2};
    std::vector<uint8_t> creator = {3};
    EventLogEntry::PollConfiguration config;
    config.method = VotingMethod::Plurality;
    config.choices = {"A", "B"};
    
    logger->logPollCreated(poll1, creator, config);
    logger->logVoteCast(poll1, {4});
    logger->logVoteCast(poll1, {5});
    logger->logPollCreated(poll2, creator, config);
    logger->logVoteCast(poll2, {6});
    logger->logPollClosed(poll1, std::vector<uint8_t>(32));
    
    auto events = logger->getEventsForPoll(poll1);
    EXPECT_EQ(events.size(), 4);
    EXPECT_EQ(events[0].eventType, EventType::PollCreated);
    EXPECT_EQ(events[1].eventType, EventType::VoteCast);
    EXPECT_EQ(events[2].eventType, EventType::VoteCast);
    EXPECT_EQ(events[3].eventType, EventType::PollClosed);
}

TEST_F(EventLoggerTest, GetEventsByType) {
    std::vector<uint8_t> poll1 = {1};
    std::vector<uint8_t> poll2 = {2};
    std::vector<uint8_t> creator = {3};
    EventLogEntry::PollConfiguration config;
    config.method = VotingMethod::Plurality;
    config.choices = {"A", "B"};
    
    logger->logPollCreated(poll1, creator, config);
    logger->logVoteCast(poll1, {4});
    logger->logVoteCast(poll1, {5});
    logger->logPollCreated(poll2, creator, config);
    logger->logVoteCast(poll2, {6});
    logger->logPollClosed(poll1, std::vector<uint8_t>(32));
    
    auto voteEvents = logger->getEventsByType(EventType::VoteCast);
    EXPECT_EQ(voteEvents.size(), 3);
    
    auto createEvents = logger->getEventsByType(EventType::PollCreated);
    EXPECT_EQ(createEvents.size(), 2);
    
    auto closeEvents = logger->getEventsByType(EventType::PollClosed);
    EXPECT_EQ(closeEvents.size(), 1);
}

TEST_F(EventLoggerTest, ExportEvents) {
    std::vector<uint8_t> pollId = {1};
    std::vector<uint8_t> creatorId = {2};
    EventLogEntry::PollConfiguration config;
    config.method = VotingMethod::Plurality;
    config.choices = {"A", "B"};
    
    logger->logPollCreated(pollId, creatorId, config);
    logger->logVoteCast(pollId, {3});
    logger->logPollClosed(pollId, std::vector<uint8_t>(32));
    
    auto exported = logger->exportEvents();
    
    EXPECT_GT(exported.size(), 24); // Has data
}

TEST_F(EventLoggerTest, CompletePollLifecycle) {
    std::vector<uint8_t> pollId = {1, 2, 3};
    std::vector<uint8_t> creatorId = {4, 5, 6};
    EventLogEntry::PollConfiguration config;
    config.method = VotingMethod::Plurality;
    config.choices = {"Alice", "Bob", "Charlie"};
    
    // Create poll
    auto createEvent = logger->logPollCreated(pollId, creatorId, config);
    EXPECT_EQ(createEvent.sequence, 0);
    
    // Cast votes
    auto vote1 = logger->logVoteCast(pollId, {7});
    auto vote2 = logger->logVoteCast(pollId, {8});
    auto vote3 = logger->logVoteCast(pollId, {9});
    EXPECT_EQ(vote1.sequence, 1);
    EXPECT_EQ(vote2.sequence, 2);
    EXPECT_EQ(vote3.sequence, 3);
    
    // Close poll
    auto closeEvent = logger->logPollClosed(pollId, std::vector<uint8_t>(32));
    EXPECT_EQ(closeEvent.sequence, 4);
    
    // Verify all events
    auto events = logger->getEventsForPoll(pollId);
    EXPECT_EQ(events.size(), 5);
    EXPECT_TRUE(logger->verifySequence());
}

TEST_F(EventLoggerTest, LargeEventVolumes) {
    std::vector<uint8_t> pollId = {1};
    
    for (int i = 0; i < 1000; i++) {
        logger->logVoteCast(pollId, {static_cast<uint8_t>(i % 256)});
    }
    
    EXPECT_EQ(logger->getEvents().size(), 1000);
    EXPECT_TRUE(logger->verifySequence());
}
