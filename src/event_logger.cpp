#include "brightchain/event_logger.hpp"
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace brightchain {

EventLogger::EventLogger() : sequence_(0) {}

std::unique_ptr<EventLogger> EventLogger::fromMember(const Member& member) {
    return std::make_unique<EventLogger>();
}

EventLogEntry EventLogger::logPollCreated(
    const std::vector<uint8_t>& pollId,
    const std::vector<uint8_t>& creatorId,
    const EventLogEntry::PollConfiguration& configuration
) {
    return appendEvent(
        EventType::PollCreated,
        pollId,
        creatorId,
        std::nullopt,
        configuration,
        std::nullopt,
        std::nullopt
    );
}

EventLogEntry EventLogger::logVoteCast(
    const std::vector<uint8_t>& pollId,
    const std::vector<uint8_t>& voterToken,
    const std::optional<std::map<std::string, nlohmann::json>>& metadata
) {
    return appendEvent(
        EventType::VoteCast,
        pollId,
        std::nullopt,
        voterToken,
        std::nullopt,
        std::nullopt,
        metadata
    );
}

EventLogEntry EventLogger::logPollClosed(
    const std::vector<uint8_t>& pollId,
    const std::vector<uint8_t>& tallyHash,
    const std::optional<std::map<std::string, nlohmann::json>>& metadata
) {
    return appendEvent(
        EventType::PollClosed,
        pollId,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        tallyHash,
        metadata
    );
}

EventLogEntry EventLogger::logEvent(
    EventType eventType,
    const std::vector<uint8_t>& pollId,
    const std::optional<std::vector<uint8_t>>& creatorId,
    const std::optional<std::vector<uint8_t>>& voterToken,
    const std::optional<EventLogEntry::PollConfiguration>& configuration,
    const std::optional<std::vector<uint8_t>>& tallyHash,
    const std::optional<std::map<std::string, nlohmann::json>>& metadata
) {
    return appendEvent(eventType, pollId, creatorId, voterToken, configuration, tallyHash, metadata);
}

std::vector<EventLogEntry> EventLogger::getEvents() const {
    return events_;
}

std::vector<EventLogEntry> EventLogger::getEventsForPoll(const std::vector<uint8_t>& pollId) const {
    std::vector<EventLogEntry> result;
    std::string pollIdHex = toHex(pollId);
    
    for (const auto& event : events_) {
        if (toHex(event.pollId) == pollIdHex) {
            result.push_back(event);
        }
    }
    
    return result;
}

std::vector<EventLogEntry> EventLogger::getEventsByType(EventType eventType) const {
    std::vector<EventLogEntry> result;
    
    for (const auto& event : events_) {
        if (event.eventType == eventType) {
            result.push_back(event);
        }
    }
    
    return result;
}

bool EventLogger::verifySequence() const {
    for (size_t i = 0; i < events_.size(); i++) {
        if (events_[i].sequence != i) {
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> EventLogger::exportEvents() const {
    std::vector<std::vector<uint8_t>> parts;
    
    // Encode number of events
    parts.push_back(encodeNumber(events_.size()));
    
    // Serialize each event
    for (const auto& event : events_) {
        parts.push_back(serializeEvent(event));
    }
    
    return concat(parts);
}

EventLogEntry EventLogger::appendEvent(
    EventType eventType,
    const std::vector<uint8_t>& pollId,
    const std::optional<std::vector<uint8_t>>& creatorId,
    const std::optional<std::vector<uint8_t>>& voterToken,
    const std::optional<EventLogEntry::PollConfiguration>& configuration,
    const std::optional<std::vector<uint8_t>>& tallyHash,
    const std::optional<std::map<std::string, nlohmann::json>>& metadata
) {
    EventLogEntry entry;
    entry.sequence = sequence_++;
    entry.timestamp = getMicrosecondTimestamp();
    entry.eventType = eventType;
    entry.pollId = pollId;
    entry.creatorId = creatorId;
    entry.voterToken = voterToken;
    entry.configuration = configuration;
    entry.tallyHash = tallyHash;
    entry.metadata = metadata;
    
    events_.push_back(entry);
    return entry;
}

uint64_t EventLogger::getMicrosecondTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    return static_cast<uint64_t>(micros.count());
}

std::vector<uint8_t> EventLogger::serializeEvent(const EventLogEntry& event) const {
    std::vector<std::vector<uint8_t>> parts;
    
    // Sequence and timestamp
    parts.push_back(encodeNumber(event.sequence));
    parts.push_back(encodeNumber(event.timestamp));
    
    // Event type
    parts.push_back(encodeString(eventTypeToString(event.eventType)));
    
    // Poll ID
    parts.push_back(encodeNumber(event.pollId.size()));
    parts.push_back(event.pollId);
    
    // Creator ID (optional)
    if (event.creatorId) {
        parts.push_back(encodeNumber(1));
        parts.push_back(encodeNumber(event.creatorId->size()));
        parts.push_back(*event.creatorId);
    } else {
        parts.push_back(encodeNumber(0));
    }
    
    // Voter token (optional)
    if (event.voterToken) {
        parts.push_back(encodeNumber(1));
        parts.push_back(encodeNumber(event.voterToken->size()));
        parts.push_back(*event.voterToken);
    } else {
        parts.push_back(encodeNumber(0));
    }
    
    // Configuration (optional)
    if (event.configuration) {
        std::string configStr = event.configuration->toJson().dump();
        auto encoded = encodeString(configStr);
        parts.push_back(encodeNumber(1));
        parts.push_back(encodeNumber(encoded.size()));
        parts.push_back(encoded);
    } else {
        parts.push_back(encodeNumber(0));
    }
    
    // Tally hash (optional)
    if (event.tallyHash) {
        parts.push_back(encodeNumber(1));
        parts.push_back(encodeNumber(event.tallyHash->size()));
        parts.push_back(*event.tallyHash);
    } else {
        parts.push_back(encodeNumber(0));
    }
    
    // Metadata (optional)
    if (event.metadata) {
        nlohmann::json metaJson(*event.metadata);
        std::string metaStr = metaJson.dump();
        auto encoded = encodeString(metaStr);
        parts.push_back(encodeNumber(1));
        parts.push_back(encodeNumber(encoded.size()));
        parts.push_back(encoded);
    } else {
        parts.push_back(encodeNumber(0));
    }
    
    return concat(parts);
}

std::vector<uint8_t> EventLogger::encodeNumber(uint64_t n) const {
    std::vector<uint8_t> result(8);
    for (int i = 7; i >= 0; i--) {
        result[7 - i] = (n >> (i * 8)) & 0xFF;
    }
    return result;
}

std::vector<uint8_t> EventLogger::encodeString(const std::string& s) const {
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::vector<uint8_t> EventLogger::concat(const std::vector<std::vector<uint8_t>>& arrays) const {
    size_t totalSize = 0;
    for (const auto& arr : arrays) {
        totalSize += arr.size();
    }
    
    std::vector<uint8_t> result;
    result.reserve(totalSize);
    
    for (const auto& arr : arrays) {
        result.insert(result.end(), arr.begin(), arr.end());
    }
    
    return result;
}

std::string EventLogger::toHex(const std::vector<uint8_t>& bytes) const {
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

} // namespace brightchain
