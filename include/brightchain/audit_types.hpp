#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <map>

namespace brightchain {

enum class AuditEventType {
    PollCreated,
    VoteCast,
    PollClosed
};

struct AuditEntry {
    int sequence;
    AuditEventType eventType;
    int64_t timestamp;
    std::vector<uint8_t> pollId;
    std::optional<std::vector<uint8_t>> voterIdHash;
    std::optional<std::vector<uint8_t>> authorityId;
    std::vector<uint8_t> previousHash;
    std::vector<uint8_t> entryHash;
    std::vector<uint8_t> signature;
    std::optional<std::map<std::string, std::string>> metadata;
};

} // namespace brightchain
