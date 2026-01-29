#pragma once

#include <brightchain/audit_types.hpp>
#include <brightchain/member.hpp>
#include <vector>
#include <memory>

namespace brightchain {

class AuditLog {
public:
    explicit AuditLog(const Member& authority);
    
    AuditEntry recordPollCreated(const std::vector<uint8_t>& pollId, const std::map<std::string, std::string>& metadata = {});
    AuditEntry recordVoteCast(const std::vector<uint8_t>& pollId, const std::vector<uint8_t>& voterIdHash);
    AuditEntry recordPollClosed(const std::vector<uint8_t>& pollId, const std::map<std::string, std::string>& metadata = {});
    
    const std::vector<AuditEntry>& getEntries() const { return entries_; }
    std::vector<AuditEntry> getEntriesForPoll(const std::vector<uint8_t>& pollId) const;
    
    bool verifyChain() const;
    bool verifyEntry(const AuditEntry& entry) const;

private:
    AuditEntry appendEntry(AuditEventType type, const std::vector<uint8_t>& pollId,
                          const std::optional<std::vector<uint8_t>>& voterIdHash = std::nullopt,
                          const std::optional<std::vector<uint8_t>>& authorityId = std::nullopt,
                          const std::map<std::string, std::string>& metadata = {});
    
    std::vector<uint8_t> computeEntryHash(const AuditEntry& entry) const;
    std::vector<uint8_t> serializeForHashing(const AuditEntry& entry) const;
    std::vector<uint8_t> serializeForSigning(const AuditEntry& entry) const;
    int64_t getMicrosecondTimestamp() const;
    
    const Member& authority_;
    std::vector<AuditEntry> entries_;
    int sequence_;
};

} // namespace brightchain
