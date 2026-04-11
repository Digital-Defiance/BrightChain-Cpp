#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace brightchain::gossip {

enum class AnnouncementType : uint8_t {
    Add,
    Remove,
    Ack,
    PoolDeleted,
    CblIndexUpdate,
    CblIndexDelete,
    HeadUpdate,
    AclUpdate,
    PoolAnnounce,
    PoolRemove,
    QuorumProposal,
    QuorumVote
};

// AnnouncementType ↔ JSON string mapping
const std::string& announcementTypeToString(AnnouncementType type);
AnnouncementType announcementTypeFromString(const std::string& str);

struct MessageDeliveryMetadata {
    std::string messageId;
    std::vector<std::string> recipientIds;
    std::string priority; // "normal" | "high"
    std::vector<std::string> blockIds;
    std::string cblBlockId;
    bool ackRequired = false;
    bool gatewayOutbound = false;

    bool operator==(const MessageDeliveryMetadata& other) const = default;

    nlohmann::json toJson() const;
    static MessageDeliveryMetadata fromJson(const nlohmann::json& j);
};

struct DeliveryAckMetadata {
    std::string messageId;
    std::string recipientId;
    std::string status; // "delivered" | "read" | "failed" | "bounced"
    std::string originalSenderNode;

    bool operator==(const DeliveryAckMetadata& other) const = default;

    nlohmann::json toJson() const;
    static DeliveryAckMetadata fromJson(const nlohmann::json& j);
};

struct HeadUpdateMetadata {
    std::string dbName;
    std::string collectionName;

    bool operator==(const HeadUpdateMetadata& other) const = default;

    nlohmann::json toJson() const;
    static HeadUpdateMetadata fromJson(const nlohmann::json& j);
};

struct PoolAnnouncementMetadata {
    int64_t blockCount = 0;
    int64_t totalSize = 0;
    bool encrypted = false;
    std::optional<std::string> encryptedMetadata; // base64 ECIES

    bool operator==(const PoolAnnouncementMetadata& other) const = default;

    nlohmann::json toJson() const;
    static PoolAnnouncementMetadata fromJson(const nlohmann::json& j);
};

struct QuorumProposalMetadata {
    std::string proposalId;
    std::string description; // max 4096 chars
    std::string actionType;
    std::string actionPayload;
    std::string proposerMemberId;
    std::string expiresAt; // ISO 8601
    int requiredThreshold = 1; // >= 1
    std::optional<std::string> attachmentCblId;

    bool operator==(const QuorumProposalMetadata& other) const = default;

    nlohmann::json toJson() const;
    static QuorumProposalMetadata fromJson(const nlohmann::json& j);
};

struct QuorumVoteMetadata {
    std::string proposalId;
    std::string voterMemberId;
    std::string decision; // "approve" | "reject"
    std::optional<std::string> comment; // max 1024 chars
    std::optional<std::vector<uint8_t>> encryptedShare;

    bool operator==(const QuorumVoteMetadata& other) const = default;

    nlohmann::json toJson() const;
    static QuorumVoteMetadata fromJson(const nlohmann::json& j);
};

struct WriteProof {
    std::string signerPublicKey; // hex
    std::string signature;       // hex
    std::string dbName;
    std::string collectionName;
    std::string blockId;

    bool operator==(const WriteProof& other) const = default;

    nlohmann::json toJson() const;
    static WriteProof fromJson(const nlohmann::json& j);
};

struct CblIndexEntry {
    std::string magnetUrl;
    std::string blockId1;
    std::string blockId2;

    bool operator==(const CblIndexEntry& other) const = default;

    nlohmann::json toJson() const;
    static CblIndexEntry fromJson(const nlohmann::json& j);
};

struct BlockAnnouncement {
    AnnouncementType type = AnnouncementType::Add;
    std::string blockId;
    std::string nodeId;
    std::string timestamp; // ISO 8601 with millisecond precision
    int ttl = 0;

    std::optional<MessageDeliveryMetadata> messageDelivery;
    std::optional<DeliveryAckMetadata> deliveryAck;
    std::optional<std::string> poolId;
    std::optional<CblIndexEntry> cblIndexEntry;
    std::optional<HeadUpdateMetadata> headUpdate;
    std::optional<std::string> aclBlockId;
    std::optional<PoolAnnouncementMetadata> poolAnnouncement;
    std::optional<QuorumProposalMetadata> quorumProposal;
    std::optional<QuorumVoteMetadata> quorumVote;
    std::optional<WriteProof> writeProof;

    bool operator==(const BlockAnnouncement& other) const = default;

    nlohmann::json toJson() const;
    static BlockAnnouncement fromJson(const nlohmann::json& j);
    bool validate() const;
};

} // namespace brightchain::gossip
