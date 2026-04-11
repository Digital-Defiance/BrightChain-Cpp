// Feature: cpp-gossip-protocol
// Property 7: Priority-based fanout and TTL assignment
// **Validates: Requirements 1.2, 5.2, 5.3**
//
// For any message announcement with priority p and GossipConfig c, the created
// BlockAnnouncement shall have ttl == c.messagePriority[p].ttl and be forwarded
// to c.messagePriority[p].fanout peers. For block-level announcements
// (non-message), ttl == c.defaultTtl and fanout equals c.fanout.

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/head_registry.hpp>
#include <brightchain/member.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

using namespace brightchain::gossip;

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

/// RAII temp directory that cleans up on destruction.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        auto tmp = std::filesystem::temp_directory_path() / "gossip_priority_test";
        tmp /= std::to_string(std::rand()) + "_" +
               std::to_string(reinterpret_cast<uintptr_t>(this));
        std::filesystem::create_directories(tmp);
        path = tmp;
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

/// Generate a non-empty alphanumeric string.
rc::Gen<std::string> genNonEmptyAlphaNum(int /*maxLen*/ = 32) {
    return rc::gen::nonEmpty(
        rc::gen::container<std::string>(
            rc::gen::inRange(static_cast<char>('a'), static_cast<char>('z' + 1))));
}

/// Generate a valid GossipConfig with randomised but valid priority values.
rc::Gen<GossipConfig> genValidGossipConfig() {
    return rc::gen::exec([] {
        GossipConfig c;
        c.fanout = *rc::gen::inRange(1, 10);
        c.defaultTtl = *rc::gen::inRange(1, 15);
        c.batchIntervalMs = 1000;
        c.maxBatchSize = 100;
        c.messagePriority.normal.fanout = *rc::gen::inRange(1, 12);
        c.messagePriority.normal.ttl = *rc::gen::inRange(1, 15);
        c.messagePriority.high.fanout = *rc::gen::inRange(1, 12);
        c.messagePriority.high.ttl = *rc::gen::inRange(1, 15);
        return c;
    });
}

/// Build a MessageDeliveryMetadata with the given priority.
MessageDeliveryMetadata makeMessageMetadata(const std::string& priority,
                                            const std::string& messageId,
                                            const std::string& cblBlockId) {
    MessageDeliveryMetadata md;
    md.messageId = messageId;
    md.recipientIds = {"recipient-1"};
    md.priority = priority;
    md.blockIds = {"block-a"};
    md.cblBlockId = cblBlockId;
    md.ackRequired = false;
    return md;
}

/// Add N connected peers to a PeerManager, returning their nodeIds.
std::vector<std::string> addConnectedPeers(PeerManager& pm, int count) {
    std::vector<std::string> ids;
    ids.reserve(count);
    for (int i = 0; i < count; ++i) {
        PeerInfo info;
        info.nodeId = "peer-" + std::to_string(i);
        info.address = "127.0.0.1";
        info.httpPort = static_cast<uint16_t>(3000 + i);
        info.wsPort = static_cast<uint16_t>(4000 + i);
        info.lastSeen = "2025-01-28T12:00:00.000Z";
        info.connected = true;
        info.latencyMs = 10.0;
        pm.addPeer(info);
        ids.push_back(info.nodeId);
    }
    return ids;
}

/// Extract distinct peer IDs from sent messages.
std::set<std::string> extractDistinctPeerIds(
    const std::vector<std::pair<std::string, std::string>>& messages) {
    std::set<std::string> peers;
    for (const auto& [peerId, msg] : messages) {
        peers.insert(peerId);
    }
    return peers;
}

/// Extract the TTL from the first sent message's JSON payload.
std::optional<int> extractForwardedTtl(
    const std::vector<std::pair<std::string, std::string>>& messages) {
    if (messages.empty()) return std::nullopt;
    try {
        auto j = nlohmann::json::parse(messages[0].second);
        if (j.is_array() && !j.empty()) {
            return j[0].value("ttl", -1);
        }
        if (j.contains("ttl")) {
            return j.value("ttl", -1);
        }
    } catch (...) {}
    return std::nullopt;
}

} // namespace


// ── Property 7a: Normal-priority message gets normal TTL ──────────────────
// announceMessage with priority "normal" shall produce an announcement whose
// TTL equals config.messagePriority.normal.ttl.

RC_GTEST_PROP(PriorityFanoutAndTtl,
              NormalPriorityMessageGetNormalTtl,
              ()) {
    auto config = *genValidGossipConfig();
    std::string msgId = *genNonEmptyAlphaNum();
    std::string cblId = *genNonEmptyAlphaNum();

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    auto metadata = makeMessageMetadata("normal", msgId, cblId);
    engine.announceMessage({"block-a"}, metadata);

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(pending.size() == 1);
    RC_ASSERT(pending[0].ttl == config.messagePriority.normal.ttl);
    RC_ASSERT(pending[0].type == AnnouncementType::Add);
    RC_ASSERT(pending[0].messageDelivery.has_value());
}

// ── Property 7b: High-priority message gets high TTL ──────────────────────
// announceMessage with priority "high" shall produce an announcement whose
// TTL equals config.messagePriority.high.ttl.

RC_GTEST_PROP(PriorityFanoutAndTtl,
              HighPriorityMessageGetHighTtl,
              ()) {
    auto config = *genValidGossipConfig();
    std::string msgId = *genNonEmptyAlphaNum();
    std::string cblId = *genNonEmptyAlphaNum();

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    auto metadata = makeMessageMetadata("high", msgId, cblId);
    engine.announceMessage({"block-a"}, metadata);

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(pending.size() == 1);
    RC_ASSERT(pending[0].ttl == config.messagePriority.high.ttl);
    RC_ASSERT(pending[0].type == AnnouncementType::Add);
    RC_ASSERT(pending[0].messageDelivery.has_value());
}

// ── Property 7c: Block-level announcement gets default TTL ────────────────
// announceBlock (non-message) shall produce an announcement whose TTL equals
// config.defaultTtl.

RC_GTEST_PROP(PriorityFanoutAndTtl,
              BlockLevelAnnouncementGetDefaultTtl,
              ()) {
    auto config = *genValidGossipConfig();
    std::string blockId = *genNonEmptyAlphaNum();

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    engine.announceBlock(blockId);

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(pending.size() == 1);
    RC_ASSERT(pending[0].ttl == config.defaultTtl);
    RC_ASSERT(!pending[0].messageDelivery.has_value());
}


// ── Property 7d: Normal-priority forwarding uses normal fanout ────────────
// When a normal-priority message announcement with TTL > 0 is handled, it
// shall be forwarded to exactly min(normalFanout, N) distinct peers.

RC_GTEST_PROP(PriorityFanoutAndTtl,
              NormalPriorityForwardingUsesNormalFanout,
              ()) {
    auto config = *genValidGossipConfig();
    int peerCount = *rc::gen::inRange(1, 20);
    std::string blockId = *genNonEmptyAlphaNum();
    std::string nodeId = *genNonEmptyAlphaNum();

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    addConnectedPeers(pm, peerCount);
    pm.clearSentMessages();

    // Build a normal-priority message announcement with TTL > 0
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Add;
    ann.blockId = blockId;
    ann.nodeId = nodeId;
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = config.messagePriority.normal.ttl;

    MessageDeliveryMetadata md;
    md.messageId = "msg-1";
    md.recipientIds = {"remote-user"};  // not a local user → will forward
    md.priority = "normal";
    md.blockIds = {blockId};
    md.cblBlockId = blockId;
    md.ackRequired = false;
    ann.messageDelivery = md;

    engine.handleAnnouncement(ann);

    auto messages = pm.getSentMessages();
    auto distinctPeers = extractDistinctPeerIds(messages);

    int expectedFanout = std::min(config.messagePriority.normal.fanout, peerCount);
    RC_ASSERT(static_cast<int>(distinctPeers.size()) == expectedFanout);
}

// ── Property 7e: High-priority forwarding uses high fanout ────────────────
// When a high-priority message announcement with TTL > 0 is handled, it
// shall be forwarded to exactly min(highFanout, N) distinct peers.

RC_GTEST_PROP(PriorityFanoutAndTtl,
              HighPriorityForwardingUsesHighFanout,
              ()) {
    auto config = *genValidGossipConfig();
    int peerCount = *rc::gen::inRange(1, 20);
    std::string blockId = *genNonEmptyAlphaNum();
    std::string nodeId = *genNonEmptyAlphaNum();

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    addConnectedPeers(pm, peerCount);
    pm.clearSentMessages();

    // Build a high-priority message announcement with TTL > 0
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Add;
    ann.blockId = blockId;
    ann.nodeId = nodeId;
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = config.messagePriority.high.ttl;

    MessageDeliveryMetadata md;
    md.messageId = "msg-2";
    md.recipientIds = {"remote-user"};
    md.priority = "high";
    md.blockIds = {blockId};
    md.cblBlockId = blockId;
    md.ackRequired = false;
    ann.messageDelivery = md;

    engine.handleAnnouncement(ann);

    auto messages = pm.getSentMessages();
    auto distinctPeers = extractDistinctPeerIds(messages);

    int expectedFanout = std::min(config.messagePriority.high.fanout, peerCount);
    RC_ASSERT(static_cast<int>(distinctPeers.size()) == expectedFanout);
}

// ── Property 7f: Block-level forwarding uses default fanout ───────────────
// When a non-message Add announcement with TTL > 0 is handled, it shall be
// forwarded to exactly min(config.fanout, N) distinct peers.

RC_GTEST_PROP(PriorityFanoutAndTtl,
              BlockLevelForwardingUsesDefaultFanout,
              ()) {
    auto config = *genValidGossipConfig();
    int peerCount = *rc::gen::inRange(1, 20);
    std::string blockId = *genNonEmptyAlphaNum();
    std::string nodeId = *genNonEmptyAlphaNum();

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    addConnectedPeers(pm, peerCount);
    pm.clearSentMessages();

    // Plain Add announcement — no messageDelivery
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Add;
    ann.blockId = blockId;
    ann.nodeId = nodeId;
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = config.defaultTtl;

    engine.handleAnnouncement(ann);

    auto messages = pm.getSentMessages();
    auto distinctPeers = extractDistinctPeerIds(messages);

    int expectedFanout = std::min(config.fanout, peerCount);
    RC_ASSERT(static_cast<int>(distinctPeers.size()) == expectedFanout);
}


// ── Property 7g: Quorum announcements use high-priority fanout ────────────
// QuorumProposal and QuorumVote announcements shall use high-priority TTL
// on creation and high-priority fanout on forwarding.

RC_GTEST_PROP(PriorityFanoutAndTtl,
              QuorumAnnouncementsUseHighPriorityConfig,
              ()) {
    auto config = *genValidGossipConfig();
    int peerCount = *rc::gen::inRange(1, 20);
    std::string proposalId = *genNonEmptyAlphaNum();

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    // Verify TTL assignment on creation
    QuorumProposalMetadata qp;
    qp.proposalId = proposalId;
    qp.description = "test proposal";
    qp.actionType = "AddMember";
    qp.actionPayload = "{}";
    qp.proposerMemberId = "member-1";
    qp.expiresAt = "2026-12-31T23:59:59.000Z";
    qp.requiredThreshold = 1;

    engine.announceBrightTrustProposal(qp);

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(pending.size() == 1);
    RC_ASSERT(pending[0].ttl == config.messagePriority.high.ttl);

    // Now test forwarding fanout for a quorum_proposal
    engine.flushAnnouncements();  // clear pending
    addConnectedPeers(pm, peerCount);
    pm.clearSentMessages();

    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumProposal;
    ann.blockId = proposalId;
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = config.messagePriority.high.ttl;
    ann.quorumProposal = qp;

    engine.handleAnnouncement(ann);

    auto messages = pm.getSentMessages();
    auto distinctPeers = extractDistinctPeerIds(messages);

    int expectedFanout = std::min(config.messagePriority.high.fanout, peerCount);
    RC_ASSERT(static_cast<int>(distinctPeers.size()) == expectedFanout);
}

// ── Property 7h: QuorumVote creation uses high-priority TTL ───────────────

RC_GTEST_PROP(PriorityFanoutAndTtl,
              QuorumVoteCreationUsesHighPriorityTtl,
              ()) {
    auto config = *genValidGossipConfig();
    std::string proposalId = *genNonEmptyAlphaNum();
    std::string voterId = *genNonEmptyAlphaNum();

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    QuorumVoteMetadata qv;
    qv.proposalId = proposalId;
    qv.voterMemberId = voterId;
    qv.decision = "approve";

    engine.announceBrightTrustVote(qv);

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(pending.size() == 1);
    RC_ASSERT(pending[0].ttl == config.messagePriority.high.ttl);
    RC_ASSERT(pending[0].type == AnnouncementType::QuorumVote);
}
