#pragma once

#include <string>

namespace brightchain {

/**
 * Event types for voting system operations
 */
enum class EventType {
    PollCreated,
    VoteCast,
    PollClosed,
    VoteVerified,
    TallyComputed,
    AuditRequested
};

/**
 * Convert EventType to string
 */
inline std::string eventTypeToString(EventType type) {
    switch (type) {
        case EventType::PollCreated: return "poll_created";
        case EventType::VoteCast: return "vote_cast";
        case EventType::PollClosed: return "poll_closed";
        case EventType::VoteVerified: return "vote_verified";
        case EventType::TallyComputed: return "tally_computed";
        case EventType::AuditRequested: return "audit_requested";
        default: return "unknown";
    }
}

/**
 * Convert string to EventType
 */
inline EventType stringToEventType(const std::string& str) {
    if (str == "poll_created") return EventType::PollCreated;
    if (str == "vote_cast") return EventType::VoteCast;
    if (str == "poll_closed") return EventType::PollClosed;
    if (str == "vote_verified") return EventType::VoteVerified;
    if (str == "tally_computed") return EventType::TallyComputed;
    if (str == "audit_requested") return EventType::AuditRequested;
    throw std::invalid_argument("Unknown event type: " + str);
}

} // namespace brightchain
