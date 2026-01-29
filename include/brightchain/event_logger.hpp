#pragma once

#include "event_type.hpp"
#include "event_log_entry.hpp"
#include "member.hpp"
#include <vector>
#include <memory>
#include <chrono>

namespace brightchain {

/**
 * Comprehensive event logger with sequence tracking
 * Implements requirement 1.3: Comprehensive event logging with microsecond timestamps
 * Matches TypeScript PollEventLogger class
 */
class EventLogger {
public:
    /**
     * Constructor
     */
    EventLogger();
    
    /**
     * Create from Member (uses member's ID for signing)
     */
    static std::unique_ptr<EventLogger> fromMember(const Member& member);
    
    /**
     * Log poll creation event
     */
    EventLogEntry logPollCreated(
        const std::vector<uint8_t>& pollId,
        const std::vector<uint8_t>& creatorId,
        const EventLogEntry::PollConfiguration& configuration
    );
    
    /**
     * Log vote cast event
     */
    EventLogEntry logVoteCast(
        const std::vector<uint8_t>& pollId,
        const std::vector<uint8_t>& voterToken,
        const std::optional<std::map<std::string, nlohmann::json>>& metadata = std::nullopt
    );
    
    /**
     * Log poll closure event
     */
    EventLogEntry logPollClosed(
        const std::vector<uint8_t>& pollId,
        const std::vector<uint8_t>& tallyHash,
        const std::optional<std::map<std::string, nlohmann::json>>& metadata = std::nullopt
    );
    
    /**
     * Log generic event
     */
    EventLogEntry logEvent(
        EventType eventType,
        const std::vector<uint8_t>& pollId,
        const std::optional<std::vector<uint8_t>>& creatorId = std::nullopt,
        const std::optional<std::vector<uint8_t>>& voterToken = std::nullopt,
        const std::optional<EventLogEntry::PollConfiguration>& configuration = std::nullopt,
        const std::optional<std::vector<uint8_t>>& tallyHash = std::nullopt,
        const std::optional<std::map<std::string, nlohmann::json>>& metadata = std::nullopt
    );
    
    /**
     * Get all events
     */
    std::vector<EventLogEntry> getEvents() const;
    
    /**
     * Get events for specific poll
     */
    std::vector<EventLogEntry> getEventsForPoll(const std::vector<uint8_t>& pollId) const;
    
    /**
     * Get events by type
     */
    std::vector<EventLogEntry> getEventsByType(EventType eventType) const;
    
    /**
     * Verify sequence integrity
     */
    bool verifySequence() const;
    
    /**
     * Export events for archival
     */
    std::vector<uint8_t> exportEvents() const;

private:
    std::vector<EventLogEntry> events_;
    uint64_t sequence_;
    
    /**
     * Append event to log
     */
    EventLogEntry appendEvent(
        EventType eventType,
        const std::vector<uint8_t>& pollId,
        const std::optional<std::vector<uint8_t>>& creatorId,
        const std::optional<std::vector<uint8_t>>& voterToken,
        const std::optional<EventLogEntry::PollConfiguration>& configuration,
        const std::optional<std::vector<uint8_t>>& tallyHash,
        const std::optional<std::map<std::string, nlohmann::json>>& metadata
    );
    
    /**
     * Get microsecond timestamp
     */
    uint64_t getMicrosecondTimestamp() const;
    
    /**
     * Serialize event for export
     */
    std::vector<uint8_t> serializeEvent(const EventLogEntry& event) const;
    
    /**
     * Encode number as 8 bytes (big-endian)
     */
    std::vector<uint8_t> encodeNumber(uint64_t n) const;
    
    /**
     * Encode string as UTF-8 bytes
     */
    std::vector<uint8_t> encodeString(const std::string& s) const;
    
    /**
     * Concatenate byte arrays
     */
    std::vector<uint8_t> concat(const std::vector<std::vector<uint8_t>>& arrays) const;
    
    /**
     * Convert bytes to hex string
     */
    std::string toHex(const std::vector<uint8_t>& bytes) const;
};

} // namespace brightchain
