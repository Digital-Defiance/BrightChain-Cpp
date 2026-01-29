#pragma once

#include <brightchain/poll.hpp>
#include <brightchain/paillier.hpp>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace brightchain {

enum class JurisdictionLevel {
    Precinct,
    County,
    State,
    National
};

struct JurisdictionConfig {
    std::vector<uint8_t> id;
    std::string name;
    JurisdictionLevel level;
    std::optional<std::vector<uint8_t>> parentId;
};

struct AggregatedTally {
    std::vector<uint8_t> jurisdictionId;
    JurisdictionLevel level;
    std::vector<std::string> encryptedTallies;
    size_t voterCount;
    uint64_t timestamp;
    std::vector<std::vector<uint8_t>> childJurisdictions;
};

class PrecinctAggregator {
public:
    PrecinctAggregator(Poll& poll, const JurisdictionConfig& config);
    
    void vote(const Member& voter, const EncryptedVote& vote);
    AggregatedTally getTally() const;
    void close();
    
private:
    Poll& poll_;
    JurisdictionConfig config_;
};

class CountyAggregator {
public:
    CountyAggregator(const JurisdictionConfig& config, std::shared_ptr<PaillierPublicKey> publicKey);
    
    void addPrecinctTally(const AggregatedTally& tally);
    AggregatedTally getTally() const;
    
private:
    JurisdictionConfig config_;
    std::shared_ptr<PaillierPublicKey> votingPublicKey_;
    std::map<std::string, AggregatedTally> precinctTallies_;
    
    std::string toKey(const std::vector<uint8_t>& id) const;
};

class StateAggregator {
public:
    StateAggregator(const JurisdictionConfig& config, std::shared_ptr<PaillierPublicKey> publicKey);
    
    void addCountyTally(const AggregatedTally& tally);
    AggregatedTally getTally() const;
    
private:
    JurisdictionConfig config_;
    std::shared_ptr<PaillierPublicKey> votingPublicKey_;
    std::map<std::string, AggregatedTally> countyTallies_;
    
    std::string toKey(const std::vector<uint8_t>& id) const;
};

class NationalAggregator {
public:
    NationalAggregator(const JurisdictionConfig& config, std::shared_ptr<PaillierPublicKey> publicKey);
    
    void addStateTally(const AggregatedTally& tally);
    AggregatedTally getTally() const;
    
private:
    JurisdictionConfig config_;
    std::shared_ptr<PaillierPublicKey> votingPublicKey_;
    std::map<std::string, AggregatedTally> stateTallies_;
    
    std::string toKey(const std::vector<uint8_t>& id) const;
};

} // namespace brightchain
