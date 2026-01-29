#include <brightchain/hierarchical_aggregator.hpp>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace brightchain {

// PrecinctAggregator
PrecinctAggregator::PrecinctAggregator(Poll& poll, const JurisdictionConfig& config)
    : poll_(poll), config_(config) {
    if (config.level != JurisdictionLevel::Precinct) {
        throw std::invalid_argument("PrecinctAggregator requires precinct-level config");
    }
}

void PrecinctAggregator::vote(const Member& voter, const EncryptedVote& vote) {
    poll_.vote(voter, vote);
}

AggregatedTally PrecinctAggregator::getTally() const {
    const auto& votes = poll_.getEncryptedVotes();
    size_t choiceCount = poll_.choices().size();
    
    std::vector<std::string> encryptedTallies(choiceCount);
    for (const auto& [voterId, encryptedVote] : votes) {
        for (size_t i = 0; i < choiceCount && i < encryptedVote.size(); ++i) {
            encryptedTallies[i] = std::string(encryptedVote[i].begin(), encryptedVote[i].end());
        }
    }
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    return AggregatedTally{
        config_.id,
        JurisdictionLevel::Precinct,
        encryptedTallies,
        static_cast<size_t>(poll_.voterCount()),
        static_cast<uint64_t>(timestamp),
        {}
    };
}

void PrecinctAggregator::close() {
    poll_.close();
}

// CountyAggregator
CountyAggregator::CountyAggregator(const JurisdictionConfig& config, std::shared_ptr<PaillierPublicKey> publicKey)
    : config_(config), votingPublicKey_(publicKey) {
    if (config.level != JurisdictionLevel::County) {
        throw std::invalid_argument("CountyAggregator requires county-level config");
    }
}

void CountyAggregator::addPrecinctTally(const AggregatedTally& tally) {
    precinctTallies_[toKey(tally.jurisdictionId)] = tally;
}

AggregatedTally CountyAggregator::getTally() const {
    if (precinctTallies_.empty()) {
        throw std::runtime_error("No precinct tallies to aggregate");
    }
    
    std::vector<AggregatedTally> tallies;
    for (const auto& [_, tally] : precinctTallies_) {
        tallies.push_back(tally);
    }
    
    size_t choiceCount = tallies[0].encryptedTallies.size();
    std::vector<std::string> encryptedTallies(choiceCount);
    size_t totalVoters = 0;
    std::vector<std::vector<uint8_t>> childJurisdictions;
    
    for (const auto& tally : tallies) {
        for (size_t i = 0; i < choiceCount; ++i) {
            if (encryptedTallies[i].empty()) {
                encryptedTallies[i] = tally.encryptedTallies[i];
            } else {
                std::vector<uint8_t> t1(encryptedTallies[i].begin(), encryptedTallies[i].end());
                std::vector<uint8_t> t2(tally.encryptedTallies[i].begin(), tally.encryptedTallies[i].end());
                auto result = votingPublicKey_->addition({t1, t2});
                encryptedTallies[i] = std::string(result.begin(), result.end());
            }
        }
        totalVoters += tally.voterCount;
        childJurisdictions.push_back(tally.jurisdictionId);
    }
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    return AggregatedTally{
        config_.id,
        JurisdictionLevel::County,
        encryptedTallies,
        totalVoters,
        static_cast<uint64_t>(timestamp),
        childJurisdictions
    };
}

std::string CountyAggregator::toKey(const std::vector<uint8_t>& id) const {
    std::ostringstream oss;
    for (uint8_t byte : id) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

// StateAggregator
StateAggregator::StateAggregator(const JurisdictionConfig& config, std::shared_ptr<PaillierPublicKey> publicKey)
    : config_(config), votingPublicKey_(publicKey) {
    if (config.level != JurisdictionLevel::State) {
        throw std::invalid_argument("StateAggregator requires state-level config");
    }
}

void StateAggregator::addCountyTally(const AggregatedTally& tally) {
    countyTallies_[toKey(tally.jurisdictionId)] = tally;
}

AggregatedTally StateAggregator::getTally() const {
    if (countyTallies_.empty()) {
        throw std::runtime_error("No county tallies to aggregate");
    }
    
    std::vector<AggregatedTally> tallies;
    for (const auto& [_, tally] : countyTallies_) {
        tallies.push_back(tally);
    }
    
    size_t choiceCount = tallies[0].encryptedTallies.size();
    std::vector<std::string> encryptedTallies(choiceCount);
    size_t totalVoters = 0;
    std::vector<std::vector<uint8_t>> childJurisdictions;
    
    for (const auto& tally : tallies) {
        for (size_t i = 0; i < choiceCount; ++i) {
            if (encryptedTallies[i].empty()) {
                encryptedTallies[i] = tally.encryptedTallies[i];
            } else {
                std::vector<uint8_t> t1(encryptedTallies[i].begin(), encryptedTallies[i].end());
                std::vector<uint8_t> t2(tally.encryptedTallies[i].begin(), tally.encryptedTallies[i].end());
                auto result = votingPublicKey_->addition({t1, t2});
                encryptedTallies[i] = std::string(result.begin(), result.end());
            }
        }
        totalVoters += tally.voterCount;
        childJurisdictions.push_back(tally.jurisdictionId);
    }
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    return AggregatedTally{
        config_.id,
        JurisdictionLevel::State,
        encryptedTallies,
        totalVoters,
        static_cast<uint64_t>(timestamp),
        childJurisdictions
    };
}

std::string StateAggregator::toKey(const std::vector<uint8_t>& id) const {
    std::ostringstream oss;
    for (uint8_t byte : id) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

// NationalAggregator
NationalAggregator::NationalAggregator(const JurisdictionConfig& config, std::shared_ptr<PaillierPublicKey> publicKey)
    : config_(config), votingPublicKey_(publicKey) {
    if (config.level != JurisdictionLevel::National) {
        throw std::invalid_argument("NationalAggregator requires national-level config");
    }
}

void NationalAggregator::addStateTally(const AggregatedTally& tally) {
    stateTallies_[toKey(tally.jurisdictionId)] = tally;
}

AggregatedTally NationalAggregator::getTally() const {
    if (stateTallies_.empty()) {
        throw std::runtime_error("No state tallies to aggregate");
    }
    
    std::vector<AggregatedTally> tallies;
    for (const auto& [_, tally] : stateTallies_) {
        tallies.push_back(tally);
    }
    
    size_t choiceCount = tallies[0].encryptedTallies.size();
    std::vector<std::string> encryptedTallies(choiceCount);
    size_t totalVoters = 0;
    std::vector<std::vector<uint8_t>> childJurisdictions;
    
    for (const auto& tally : tallies) {
        for (size_t i = 0; i < choiceCount; ++i) {
            if (encryptedTallies[i].empty()) {
                encryptedTallies[i] = tally.encryptedTallies[i];
            } else {
                std::vector<uint8_t> t1(encryptedTallies[i].begin(), encryptedTallies[i].end());
                std::vector<uint8_t> t2(tally.encryptedTallies[i].begin(), tally.encryptedTallies[i].end());
                auto result = votingPublicKey_->addition({t1, t2});
                encryptedTallies[i] = std::string(result.begin(), result.end());
            }
        }
        totalVoters += tally.voterCount;
        childJurisdictions.push_back(tally.jurisdictionId);
    }
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    return AggregatedTally{
        config_.id,
        JurisdictionLevel::National,
        encryptedTallies,
        totalVoters,
        static_cast<uint64_t>(timestamp),
        childJurisdictions
    };
}

std::string NationalAggregator::toKey(const std::vector<uint8_t>& id) const {
    std::ostringstream oss;
    for (uint8_t byte : id) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

} // namespace brightchain
