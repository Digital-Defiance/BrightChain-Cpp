#include <gtest/gtest.h>
#include <brightchain/hierarchical_aggregator.hpp>
#include <brightchain/poll_factory.hpp>
#include <brightchain/vote_encoder.hpp>
#include <brightchain/member.hpp>
#include <brightchain/paillier.hpp>

using namespace brightchain;

class HierarchicalAggregatorTest : public ::testing::Test {
protected:
    HierarchicalAggregatorTest() : authority(Member::generate(MemberType::System, "Authority", "auth@test.com")) {
        authority.deriveVotingKeys(512, 16);  // Small keys for fast tests
    }
    
    Member authority;
};

TEST_F(HierarchicalAggregatorTest, PrecinctAggregatorBasic) {
    auto pollPtr = PollFactory::createPlurality({"Alice", "Bob"}, authority);
    
    JurisdictionConfig config{
        {0x01, 0x02, 0x03},
        "Precinct 1",
        JurisdictionLevel::Precinct,
        std::nullopt
    };
    
    PrecinctAggregator aggregator(*pollPtr, config);
    
    VoteEncoder encoder(pollPtr->votingPublicKey());
    auto voter1 = Member::generate(MemberType::User, "V1", "v1@test.com");
    auto voter2 = Member::generate(MemberType::User, "V2", "v2@test.com");
    
    aggregator.vote(voter1, encoder.encodePlurality(0, 2));
    aggregator.vote(voter2, encoder.encodePlurality(1, 2));
    
    auto tally = aggregator.getTally();
    
    EXPECT_EQ(tally.jurisdictionId, config.id);
    EXPECT_EQ(tally.level, JurisdictionLevel::Precinct);
    EXPECT_EQ(tally.voterCount, 2);
    EXPECT_EQ(tally.encryptedTallies.size(), 2);
}

TEST_F(HierarchicalAggregatorTest, PrecinctAggregatorWrongLevel) {
    auto pollPtr = PollFactory::createPlurality({"Alice", "Bob"}, authority);
    
    JurisdictionConfig config{
        {0x01},
        "County 1",
        JurisdictionLevel::County,
        std::nullopt
    };
    
    EXPECT_THROW(PrecinctAggregator(*pollPtr, config), std::invalid_argument);
}

TEST_F(HierarchicalAggregatorTest, CountyAggregatorBasic) {
    JurisdictionConfig config{
        {0x10, 0x20},
        "County 1",
        JurisdictionLevel::County,
        std::nullopt
    };
    
    CountyAggregator aggregator(config, authority.votingPublicKey());
    
    AggregatedTally precinct1{
        {0x01},
        JurisdictionLevel::Precinct,
        {"100", "50"},
        150,
        1234567890,
        {}
    };
    
    AggregatedTally precinct2{
        {0x02},
        JurisdictionLevel::Precinct,
        {"75", "80"},
        155,
        1234567891,
        {}
    };
    
    aggregator.addPrecinctTally(precinct1);
    aggregator.addPrecinctTally(precinct2);
    
    auto tally = aggregator.getTally();
    
    EXPECT_EQ(tally.jurisdictionId, config.id);
    EXPECT_EQ(tally.level, JurisdictionLevel::County);
    EXPECT_EQ(tally.voterCount, 305);
    EXPECT_EQ(tally.childJurisdictions.size(), 2);
}

TEST_F(HierarchicalAggregatorTest, CountyAggregatorWrongLevel) {
    JurisdictionConfig config{
        {0x10},
        "State 1",
        JurisdictionLevel::State,
        std::nullopt
    };
    
    EXPECT_THROW(CountyAggregator(config, authority.votingPublicKey()), std::invalid_argument);
}

TEST_F(HierarchicalAggregatorTest, CountyAggregatorEmpty) {
    JurisdictionConfig config{
        {0x10},
        "County 1",
        JurisdictionLevel::County,
        std::nullopt
    };
    
    CountyAggregator aggregator(config, authority.votingPublicKey());
    
    EXPECT_THROW(aggregator.getTally(), std::runtime_error);
}

TEST_F(HierarchicalAggregatorTest, StateAggregatorBasic) {
    JurisdictionConfig config{
        {0x20, 0x30},
        "State 1",
        JurisdictionLevel::State,
        std::nullopt
    };
    
    StateAggregator aggregator(config, authority.votingPublicKey());
    
    AggregatedTally county1{
        {0x10},
        JurisdictionLevel::County,
        {"200", "150"},
        350,
        1234567890,
        {{0x01}, {0x02}}
    };
    
    AggregatedTally county2{
        {0x11},
        JurisdictionLevel::County,
        {"180", "170"},
        350,
        1234567891,
        {{0x03}, {0x04}}
    };
    
    aggregator.addCountyTally(county1);
    aggregator.addCountyTally(county2);
    
    auto tally = aggregator.getTally();
    
    EXPECT_EQ(tally.jurisdictionId, config.id);
    EXPECT_EQ(tally.level, JurisdictionLevel::State);
    EXPECT_EQ(tally.voterCount, 700);
    EXPECT_EQ(tally.childJurisdictions.size(), 2);
}

TEST_F(HierarchicalAggregatorTest, NationalAggregatorBasic) {
    JurisdictionConfig config{
        {0x30, 0x40},
        "National",
        JurisdictionLevel::National,
        std::nullopt
    };
    
    NationalAggregator aggregator(config, authority.votingPublicKey());
    
    AggregatedTally state1{
        {0x20},
        JurisdictionLevel::State,
        {"1000", "900"},
        1900,
        1234567890,
        {{0x10}, {0x11}}
    };
    
    AggregatedTally state2{
        {0x21},
        JurisdictionLevel::State,
        {"1100", "950"},
        2050,
        1234567891,
        {{0x12}, {0x13}}
    };
    
    aggregator.addStateTally(state1);
    aggregator.addStateTally(state2);
    
    auto tally = aggregator.getTally();
    
    EXPECT_EQ(tally.jurisdictionId, config.id);
    EXPECT_EQ(tally.level, JurisdictionLevel::National);
    EXPECT_EQ(tally.voterCount, 3950);
    EXPECT_EQ(tally.childJurisdictions.size(), 2);
}

TEST_F(HierarchicalAggregatorTest, FullHierarchyIntegration) {
    auto poll1 = PollFactory::createPlurality({"Alice", "Bob"}, authority);
    auto poll2 = PollFactory::createPlurality({"Alice", "Bob"}, authority);
    
    JurisdictionConfig precinct1Config{{0x01}, "P1", JurisdictionLevel::Precinct, std::nullopt};
    JurisdictionConfig precinct2Config{{0x02}, "P2", JurisdictionLevel::Precinct, std::nullopt};
    
    PrecinctAggregator p1(*poll1, precinct1Config);
    PrecinctAggregator p2(*poll2, precinct2Config);
    
    VoteEncoder encoder1(poll1->votingPublicKey());
    VoteEncoder encoder2(poll2->votingPublicKey());
    
    for (int i = 0; i < 3; ++i) {
        auto voter = Member::generate(MemberType::User, "V" + std::to_string(i), "v@test.com");
        p1.vote(voter, encoder1.encodePlurality(0, 2));
    }
    
    for (int i = 0; i < 2; ++i) {
        auto voter = Member::generate(MemberType::User, "V" + std::to_string(i), "v@test.com");
        p2.vote(voter, encoder2.encodePlurality(1, 2));
    }
    
    JurisdictionConfig countyConfig{{0x10}, "County1", JurisdictionLevel::County, std::nullopt};
    CountyAggregator county(countyConfig, authority.votingPublicKey());
    
    county.addPrecinctTally(p1.getTally());
    county.addPrecinctTally(p2.getTally());
    
    auto countyTally = county.getTally();
    EXPECT_EQ(countyTally.voterCount, 5);
    EXPECT_EQ(countyTally.childJurisdictions.size(), 2);
}
