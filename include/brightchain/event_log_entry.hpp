#pragma once

#include "event_type.hpp"
#include "voting_method.hpp"
#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <map>
#include <nlohmann/json.hpp>

namespace brightchain {

/**
 * Event log entry structure for voting system events
 * Matches TypeScript EventLogEntry interface
 */
struct EventLogEntry {
    /** Sequence number (monotonically increasing) */
    uint64_t sequence;
    
    /** Event type */
    EventType eventType;
    
    /** Microsecond-precision timestamp */
    uint64_t timestamp;
    
    /** Poll identifier */
    std::vector<uint8_t> pollId;
    
    /** Creator/authority ID (for creation/closure events) */
    std::optional<std::vector<uint8_t>> creatorId;
    
    /** Anonymized voter token (for vote events) */
    std::optional<std::vector<uint8_t>> voterToken;
    
    /** Poll configuration (for creation events) */
    struct PollConfiguration {
        VotingMethod method;
        std::vector<std::string> choices;
        std::optional<int64_t> maxWeight;
        std::optional<int> threshold;
        
        nlohmann::json toJson() const {
            nlohmann::json j;
            j["method"] = votingMethodToString(method);
            j["choices"] = choices;
            if (maxWeight) j["maxWeight"] = *maxWeight;
            if (threshold) j["threshold"] = *threshold;
            return j;
        }
    };
    std::optional<PollConfiguration> configuration;
    
    /** Final tally hash (for closure events) */
    std::optional<std::vector<uint8_t>> tallyHash;
    
    /** Additional metadata */
    std::optional<std::map<std::string, nlohmann::json>> metadata;
};

} // namespace brightchain
