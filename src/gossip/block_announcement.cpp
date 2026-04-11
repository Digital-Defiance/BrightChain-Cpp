#include <brightchain/gossip/block_announcement.hpp>

#include <array>
#include <stdexcept>
#include <utility>

namespace brightchain::gossip {

// ── AnnouncementType ↔ string mapping ──────────────────────────────────────

struct TypeMapping {
    AnnouncementType type;
    const char* str;
};

static constexpr std::array<TypeMapping, 12> kTypeMappings{{
    {AnnouncementType::Add, "add"},
    {AnnouncementType::Remove, "remove"},
    {AnnouncementType::Ack, "ack"},
    {AnnouncementType::PoolDeleted, "pool_deleted"},
    {AnnouncementType::CblIndexUpdate, "cbl_index_update"},
    {AnnouncementType::CblIndexDelete, "cbl_index_delete"},
    {AnnouncementType::HeadUpdate, "head_update"},
    {AnnouncementType::AclUpdate, "acl_update"},
    {AnnouncementType::PoolAnnounce, "pool_announce"},
    {AnnouncementType::PoolRemove, "pool_remove"},
    {AnnouncementType::QuorumProposal, "quorum_proposal"},
    {AnnouncementType::QuorumVote, "quorum_vote"},
}};

const std::string& announcementTypeToString(AnnouncementType type) {
    for (const auto& m : kTypeMappings) {
        if (m.type == type) {
            static std::array<std::string, 12> cache;
            auto idx = static_cast<size_t>(type);
            if (cache[idx].empty()) cache[idx] = m.str;
            return cache[idx];
        }
    }
    throw std::invalid_argument("Unknown AnnouncementType");
}

AnnouncementType announcementTypeFromString(const std::string& str) {
    for (const auto& m : kTypeMappings) {
        if (str == m.str) return m.type;
    }
    throw std::invalid_argument("Unknown announcement type string: " + str);
}

// ── MessageDeliveryMetadata ────────────────────────────────────────────────

nlohmann::json MessageDeliveryMetadata::toJson() const {
    nlohmann::json j;
    j["messageId"] = messageId;
    j["recipientIds"] = recipientIds;
    j["priority"] = priority;
    j["blockIds"] = blockIds;
    j["cblBlockId"] = cblBlockId;
    j["ackRequired"] = ackRequired;
    if (gatewayOutbound) {
        j["gatewayOutbound"] = gatewayOutbound;
    }
    return j;
}

MessageDeliveryMetadata MessageDeliveryMetadata::fromJson(const nlohmann::json& j) {
    MessageDeliveryMetadata m;
    m.messageId = j.at("messageId").get<std::string>();
    m.recipientIds = j.at("recipientIds").get<std::vector<std::string>>();
    m.priority = j.at("priority").get<std::string>();
    m.blockIds = j.at("blockIds").get<std::vector<std::string>>();
    m.cblBlockId = j.at("cblBlockId").get<std::string>();
    m.ackRequired = j.at("ackRequired").get<bool>();
    if (j.contains("gatewayOutbound")) {
        m.gatewayOutbound = j["gatewayOutbound"].get<bool>();
    }
    return m;
}

// ── DeliveryAckMetadata ────────────────────────────────────────────────────

nlohmann::json DeliveryAckMetadata::toJson() const {
    return {
        {"messageId", messageId},
        {"recipientId", recipientId},
        {"status", status},
        {"originalSenderNode", originalSenderNode},
    };
}

DeliveryAckMetadata DeliveryAckMetadata::fromJson(const nlohmann::json& j) {
    return {
        j.at("messageId").get<std::string>(),
        j.at("recipientId").get<std::string>(),
        j.at("status").get<std::string>(),
        j.at("originalSenderNode").get<std::string>(),
    };
}

// ── HeadUpdateMetadata ─────────────────────────────────────────────────────

nlohmann::json HeadUpdateMetadata::toJson() const {
    return {
        {"dbName", dbName},
        {"collectionName", collectionName},
    };
}

HeadUpdateMetadata HeadUpdateMetadata::fromJson(const nlohmann::json& j) {
    return {
        j.at("dbName").get<std::string>(),
        j.at("collectionName").get<std::string>(),
    };
}

// ── PoolAnnouncementMetadata ───────────────────────────────────────────────

nlohmann::json PoolAnnouncementMetadata::toJson() const {
    nlohmann::json j;
    j["blockCount"] = blockCount;
    j["totalSize"] = totalSize;
    j["encrypted"] = encrypted;
    if (encryptedMetadata.has_value()) {
        j["encryptedMetadata"] = *encryptedMetadata;
    }
    return j;
}

PoolAnnouncementMetadata PoolAnnouncementMetadata::fromJson(const nlohmann::json& j) {
    PoolAnnouncementMetadata m;
    m.blockCount = j.at("blockCount").get<int64_t>();
    m.totalSize = j.at("totalSize").get<int64_t>();
    m.encrypted = j.at("encrypted").get<bool>();
    if (j.contains("encryptedMetadata")) {
        m.encryptedMetadata = j["encryptedMetadata"].get<std::string>();
    }
    return m;
}

// ── QuorumProposalMetadata ─────────────────────────────────────────────────

nlohmann::json QuorumProposalMetadata::toJson() const {
    nlohmann::json j;
    j["proposalId"] = proposalId;
    j["description"] = description;
    j["actionType"] = actionType;
    j["actionPayload"] = actionPayload;
    j["proposerMemberId"] = proposerMemberId;
    j["expiresAt"] = expiresAt;
    j["requiredThreshold"] = requiredThreshold;
    if (attachmentCblId.has_value()) {
        j["attachmentCblId"] = *attachmentCblId;
    }
    return j;
}

QuorumProposalMetadata QuorumProposalMetadata::fromJson(const nlohmann::json& j) {
    QuorumProposalMetadata m;
    m.proposalId = j.at("proposalId").get<std::string>();
    m.description = j.at("description").get<std::string>();
    m.actionType = j.at("actionType").get<std::string>();
    m.actionPayload = j.at("actionPayload").get<std::string>();
    m.proposerMemberId = j.at("proposerMemberId").get<std::string>();
    m.expiresAt = j.at("expiresAt").get<std::string>();
    m.requiredThreshold = j.at("requiredThreshold").get<int>();
    if (j.contains("attachmentCblId")) {
        m.attachmentCblId = j["attachmentCblId"].get<std::string>();
    }
    return m;
}

// ── QuorumVoteMetadata ─────────────────────────────────────────────────────

nlohmann::json QuorumVoteMetadata::toJson() const {
    nlohmann::json j;
    j["proposalId"] = proposalId;
    j["voterMemberId"] = voterMemberId;
    j["decision"] = decision;
    if (comment.has_value()) {
        j["comment"] = *comment;
    }
    if (encryptedShare.has_value()) {
        // Serialize as JSON array of integers (matching TypeScript Uint8Array)
        j["encryptedShare"] = nlohmann::json::array();
        for (uint8_t byte : *encryptedShare) {
            j["encryptedShare"].push_back(static_cast<int>(byte));
        }
    }
    return j;
}

QuorumVoteMetadata QuorumVoteMetadata::fromJson(const nlohmann::json& j) {
    QuorumVoteMetadata m;
    m.proposalId = j.at("proposalId").get<std::string>();
    m.voterMemberId = j.at("voterMemberId").get<std::string>();
    m.decision = j.at("decision").get<std::string>();
    if (j.contains("comment")) {
        m.comment = j["comment"].get<std::string>();
    }
    if (j.contains("encryptedShare")) {
        std::vector<uint8_t> share;
        for (const auto& elem : j["encryptedShare"]) {
            share.push_back(static_cast<uint8_t>(elem.get<int>()));
        }
        m.encryptedShare = std::move(share);
    }
    return m;
}

// ── WriteProof ─────────────────────────────────────────────────────────────

nlohmann::json WriteProof::toJson() const {
    return {
        {"signerPublicKey", signerPublicKey},
        {"signature", signature},
        {"dbName", dbName},
        {"collectionName", collectionName},
        {"blockId", blockId},
    };
}

WriteProof WriteProof::fromJson(const nlohmann::json& j) {
    return {
        j.at("signerPublicKey").get<std::string>(),
        j.at("signature").get<std::string>(),
        j.at("dbName").get<std::string>(),
        j.at("collectionName").get<std::string>(),
        j.at("blockId").get<std::string>(),
    };
}

// ── CblIndexEntry ──────────────────────────────────────────────────────────

nlohmann::json CblIndexEntry::toJson() const {
    return {
        {"magnetUrl", magnetUrl},
        {"blockId1", blockId1},
        {"blockId2", blockId2},
    };
}

CblIndexEntry CblIndexEntry::fromJson(const nlohmann::json& j) {
    return {
        j.at("magnetUrl").get<std::string>(),
        j.at("blockId1").get<std::string>(),
        j.at("blockId2").get<std::string>(),
    };
}

// ── BlockAnnouncement toJson / fromJson ────────────────────────────────────

nlohmann::json BlockAnnouncement::toJson() const {
    nlohmann::json j;
    j["type"] = announcementTypeToString(type);
    j["blockId"] = blockId;
    j["nodeId"] = nodeId;
    j["timestamp"] = timestamp;
    j["ttl"] = ttl;

    if (messageDelivery.has_value()) {
        j["messageDelivery"] = messageDelivery->toJson();
    }
    if (deliveryAck.has_value()) {
        j["deliveryAck"] = deliveryAck->toJson();
    }
    if (poolId.has_value()) {
        j["poolId"] = *poolId;
    }
    if (cblIndexEntry.has_value()) {
        j["cblIndexEntry"] = cblIndexEntry->toJson();
    }
    if (headUpdate.has_value()) {
        j["headUpdate"] = headUpdate->toJson();
    }
    if (aclBlockId.has_value()) {
        j["aclBlockId"] = *aclBlockId;
    }
    if (poolAnnouncement.has_value()) {
        j["poolAnnouncement"] = poolAnnouncement->toJson();
    }
    if (quorumProposal.has_value()) {
        j["quorumProposal"] = quorumProposal->toJson();
    }
    if (quorumVote.has_value()) {
        j["quorumVote"] = quorumVote->toJson();
    }
    if (writeProof.has_value()) {
        j["writeProof"] = writeProof->toJson();
    }

    return j;
}

BlockAnnouncement BlockAnnouncement::fromJson(const nlohmann::json& j) {
    BlockAnnouncement a;
    a.type = announcementTypeFromString(j.at("type").get<std::string>());
    a.blockId = j.at("blockId").get<std::string>();
    a.nodeId = j.at("nodeId").get<std::string>();
    a.timestamp = j.at("timestamp").get<std::string>();
    a.ttl = j.at("ttl").get<int>();

    if (j.contains("messageDelivery")) {
        a.messageDelivery = MessageDeliveryMetadata::fromJson(j["messageDelivery"]);
    }
    if (j.contains("deliveryAck")) {
        a.deliveryAck = DeliveryAckMetadata::fromJson(j["deliveryAck"]);
    }
    if (j.contains("poolId")) {
        a.poolId = j["poolId"].get<std::string>();
    }
    if (j.contains("cblIndexEntry")) {
        a.cblIndexEntry = CblIndexEntry::fromJson(j["cblIndexEntry"]);
    }
    if (j.contains("headUpdate")) {
        a.headUpdate = HeadUpdateMetadata::fromJson(j["headUpdate"]);
    }
    if (j.contains("aclBlockId")) {
        a.aclBlockId = j["aclBlockId"].get<std::string>();
    }
    if (j.contains("poolAnnouncement")) {
        a.poolAnnouncement = PoolAnnouncementMetadata::fromJson(j["poolAnnouncement"]);
    }
    if (j.contains("quorumProposal")) {
        a.quorumProposal = QuorumProposalMetadata::fromJson(j["quorumProposal"]);
    }
    if (j.contains("quorumVote")) {
        a.quorumVote = QuorumVoteMetadata::fromJson(j["quorumVote"]);
    }
    if (j.contains("writeProof")) {
        a.writeProof = WriteProof::fromJson(j["writeProof"]);
    }

    return a;
}

// ── BlockAnnouncement validate ─────────────────────────────────────────────

bool BlockAnnouncement::validate() const {
    // messageDelivery present ONLY on Add type
    if (messageDelivery.has_value() && type != AnnouncementType::Add) {
        return false;
    }
    // deliveryAck present ONLY on Ack type
    if (deliveryAck.has_value() && type != AnnouncementType::Ack) {
        return false;
    }

    switch (type) {
    case AnnouncementType::Add:
        // Add type: no extra constraints beyond the above
        return true;

    case AnnouncementType::Remove:
        return true;

    case AnnouncementType::Ack:
        // Ack requires deliveryAck metadata
        return deliveryAck.has_value();

    case AnnouncementType::PoolDeleted:
        // Valid poolId, no messageDelivery/deliveryAck
        if (!poolId.has_value() || poolId->empty()) return false;
        if (messageDelivery.has_value() || deliveryAck.has_value()) return false;
        return true;

    case AnnouncementType::CblIndexUpdate:
    case AnnouncementType::CblIndexDelete:
        // Valid poolId + cblIndexEntry with non-empty magnetUrl, blockId1, blockId2
        if (!poolId.has_value() || poolId->empty()) return false;
        if (!cblIndexEntry.has_value()) return false;
        if (cblIndexEntry->magnetUrl.empty()) return false;
        if (cblIndexEntry->blockId1.empty()) return false;
        if (cblIndexEntry->blockId2.empty()) return false;
        return true;

    case AnnouncementType::HeadUpdate:
        // Non-empty blockId + headUpdate with non-empty dbName/collectionName
        if (blockId.empty()) return false;
        if (!headUpdate.has_value()) return false;
        if (headUpdate->dbName.empty()) return false;
        if (headUpdate->collectionName.empty()) return false;
        return true;

    case AnnouncementType::AclUpdate:
        // Valid poolId + non-empty aclBlockId
        if (!poolId.has_value() || poolId->empty()) return false;
        if (!aclBlockId.has_value() || aclBlockId->empty()) return false;
        return true;

    case AnnouncementType::PoolAnnounce:
        // Valid poolId + poolAnnouncement with numeric blockCount/totalSize and boolean encrypted
        if (!poolId.has_value() || poolId->empty()) return false;
        if (!poolAnnouncement.has_value()) return false;
        // blockCount and totalSize are int64_t (always numeric), encrypted is bool (always boolean)
        return true;

    case AnnouncementType::PoolRemove:
        // pool_remove requires a valid poolId
        if (!poolId.has_value() || poolId->empty()) return false;
        return true;

    case AnnouncementType::QuorumProposal:
        if (!quorumProposal.has_value()) return false;
        if (quorumProposal->proposalId.empty()) return false;
        if (quorumProposal->description.size() > 4096) return false;
        if (quorumProposal->proposerMemberId.empty()) return false;
        if (quorumProposal->requiredThreshold < 1) return false;
        return true;

    case AnnouncementType::QuorumVote:
        if (!quorumVote.has_value()) return false;
        if (quorumVote->proposalId.empty()) return false;
        if (quorumVote->voterMemberId.empty()) return false;
        if (quorumVote->decision != "approve" && quorumVote->decision != "reject") return false;
        if (quorumVote->comment.has_value() && quorumVote->comment->size() > 1024) return false;
        return true;
    }

    return false; // unknown type
}

} // namespace brightchain::gossip
