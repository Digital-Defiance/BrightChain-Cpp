// Feature: cpp-gossip-protocol
// Task 15.2: Property 20 — Quorum announcements use high-priority config
// **Validates: Requirements 9.1, 9.2**
//
// For any GossipConfig and quorum announcement (proposal or vote), the created
// BlockAnnouncement shall have TTL == config.messagePriority.high.ttl, and
// forwarding shall target min(config.messagePriority.high.fanout, N) peers.

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
#include <set>
#include <string>
#include <vector>

using namespace brightchain::gossip;

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        auto tmp = std::filesystem::temp_directory_path() / "gossip_quorum_hp_test";
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

rc::Gen<std::string> genNonEmptyAlphaNum() {
    return rc::gen::nonEmpty(
        rc::gen::container<std::string>(
            rc::gen::inRange(static_cast<char>('a'), static_cast<char>('z' + 1))));
}

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

std::set<std::string> extractDistinctPeerIds(
    const std::vector<std::pair<std::string, std::string>>& messages) {
    std::set<std::string> peers;
    for (const auto& [peerId, msg] : messages) {
        peers.insert(peerId);
    }
    return peers;
}

} // namespace

// ── Property 20a: Proposal creation uses high-priority TTL ────────────────

RC_GTEST_PROP(QuorumHighPriority,
              ProposalCreationUsesHighPriorityTtl,
              ()) {
    auto config = *genValidGossipConfig();
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
    RC_ASSERT(pending[0].type == AnnouncementType::QuorumProposal);
    RC_ASSERT(pending[0].ttl == config.messagePriority.high.ttl);
}

// ── Property 20b: Vote creation uses high-priority TTL ────────────────────

RC_GTEST_PROP(QuorumHighPriority,
              VoteCreationUsesHighPriorityTtl,
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
    qv.decision = *rc::gen::element(std::string("approve"), std::string("reject"));

    engine.announceBrightTrustVote(qv);

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(pending.size() == 1);
    RC_ASSERT(pending[0].type == AnnouncementType::QuorumVote);
    RC_ASSERT(pending[0].ttl == config.messagePriority.high.ttl);
}

// ── Property 20c: Proposal forwarding uses high-priority fanout ───────────

RC_GTEST_PROP(QuorumHighPriority,
              ProposalForwardingUsesHighPriorityFanout,
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
    addConnectedPeers(pm, peerCount);
    pm.clearSentMessages();

    QuorumProposalMetadata qp;
    qp.proposalId = proposalId;
    qp.description = "forward test";
    qp.actionType = "AddMember";
    qp.actionPayload = "{}";
    qp.proposerMemberId = "member-1";
    qp.expiresAt = "2026-12-31T23:59:59.000Z";
    qp.requiredThreshold = 1;

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

// ── Property 20d: Vote forwarding uses high-priority fanout ───────────────

RC_GTEST_PROP(QuorumHighPriority,
              VoteForwardingUsesHighPriorityFanout,
              ()) {
    auto config = *genValidGossipConfig();
    int peerCount = *rc::gen::inRange(1, 20);
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
    addConnectedPeers(pm, peerCount);
    pm.clearSentMessages();

    QuorumVoteMetadata qv;
    qv.proposalId = proposalId;
    qv.voterMemberId = voterId;
    qv.decision = "approve";

    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumVote;
    ann.blockId = proposalId;
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = config.messagePriority.high.ttl;
    ann.quorumVote = qv;

    engine.handleAnnouncement(ann);

    auto messages = pm.getSentMessages();
    auto distinctPeers = extractDistinctPeerIds(messages);

    int expectedFanout = std::min(config.messagePriority.high.fanout, peerCount);
    RC_ASSERT(static_cast<int>(distinctPeers.size()) == expectedFanout);
}
