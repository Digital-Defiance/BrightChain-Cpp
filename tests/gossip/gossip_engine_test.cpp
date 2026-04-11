// Feature: cpp-gossip-protocol
// Property 5: TTL decrement and fanout count on forward
// **Validates: Requirements 1.3**
//
// For any BlockAnnouncement with ttl > 0 and any set of connected peers
// of size N >= fanout, forwarding the announcement shall produce a new
// announcement with ttl decremented by exactly 1, sent to exactly
// min(fanout, N) distinct peers.

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
        auto tmp = std::filesystem::temp_directory_path() / "gossip_engine_test";
        tmp /= std::to_string(std::rand()) + "_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        std::filesystem::create_directories(tmp);
        path = tmp;
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

/// Generate a non-empty alphanumeric string.
rc::Gen<std::string> genNonEmptyAlphaNum(int maxLen = 32) {
    return rc::gen::nonEmpty(
        rc::gen::container<std::string>(
            rc::gen::inRange(static_cast<char>('a'), static_cast<char>('z' + 1))));
}

/// Generate a valid "Add"-type BlockAnnouncement with a given TTL.
/// This is the simplest announcement type that will pass validate() and
/// trigger forwarding (non-message-delivery Add announcements are forwarded).
BlockAnnouncement makeAddAnnouncement(int ttl, const std::string& blockId,
                                       const std::string& nodeId) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Add;
    ann.blockId = blockId;
    ann.nodeId = nodeId;
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = ttl;
    // No messageDelivery → will be forwarded (not locally matched)
    return ann;
}

/// Generate a valid "Remove"-type BlockAnnouncement.
BlockAnnouncement makeRemoveAnnouncement(int ttl, const std::string& blockId,
                                          const std::string& nodeId) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Remove;
    ann.blockId = blockId;
    ann.nodeId = nodeId;
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = ttl;
    return ann;
}

/// Generate a valid "HeadUpdate"-type BlockAnnouncement.
BlockAnnouncement makeHeadUpdateAnnouncement(int ttl, const std::string& blockId,
                                              const std::string& nodeId) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::HeadUpdate;
    ann.blockId = blockId;
    ann.nodeId = nodeId;
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = ttl;
    ann.headUpdate = HeadUpdateMetadata{"testdb", "testcol"};
    return ann;
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
/// The payload is either a JSON array of announcements or an encrypted envelope.
/// For non-sensitive announcements, it's a JSON array.
std::optional<int> extractForwardedTtl(
    const std::vector<std::pair<std::string, std::string>>& messages) {
    if (messages.empty()) return std::nullopt;
    try {
        auto j = nlohmann::json::parse(messages[0].second);
        // forwardAnnouncement sends as: [announcement_json]
        if (j.is_array() && !j.empty()) {
            return j[0].value("ttl", -1);
        }
        // Could also be a single announcement
        if (j.contains("ttl")) {
            return j.value("ttl", -1);
        }
    } catch (...) {
        // Encrypted or malformed — skip
    }
    return std::nullopt;
}

} // namespace


// ── Property 5a: TTL is decremented by exactly 1 on forward ───────────────
// For any Add-type announcement with TTL in [1, 20], after handleAnnouncement
// the forwarded messages shall contain TTL = original - 1.

RC_GTEST_PROP(TtlDecrementAndFanout,
              TtlDecrementedByExactlyOne,
              ()) {
    int ttl = *rc::gen::inRange(1, 21);
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

    GossipConfig config;
    config.fanout = 3;
    config.defaultTtl = 3;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    // Add enough peers so forwarding can happen
    int peerCount = *rc::gen::inRange(config.fanout, config.fanout + 10);
    addConnectedPeers(pm, peerCount);

    pm.clearSentMessages();

    auto ann = makeAddAnnouncement(ttl, blockId, nodeId);
    engine.handleAnnouncement(ann);

    auto messages = pm.getSentMessages();
    RC_ASSERT(!messages.empty());

    auto forwardedTtl = extractForwardedTtl(messages);
    RC_ASSERT(forwardedTtl.has_value());
    RC_ASSERT(forwardedTtl.value() == ttl - 1);
}

// ── Property 5b: Fanout count equals min(fanout, N) ──────────────────────
// For any announcement with TTL > 0 and N connected peers, the number of
// distinct peers that receive the forwarded announcement equals min(fanout, N).

RC_GTEST_PROP(TtlDecrementAndFanout,
              FanoutCountEqualsMinFanoutN,
              ()) {
    int fanout = *rc::gen::inRange(1, 8);
    int peerCount = *rc::gen::inRange(1, 20);
    int ttl = *rc::gen::inRange(1, 11);
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

    GossipConfig config;
    config.fanout = fanout;
    config.defaultTtl = 3;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    addConnectedPeers(pm, peerCount);
    pm.clearSentMessages();

    auto ann = makeAddAnnouncement(ttl, blockId, nodeId);
    engine.handleAnnouncement(ann);

    auto messages = pm.getSentMessages();
    auto distinctPeers = extractDistinctPeerIds(messages);

    int expectedCount = std::min(fanout, peerCount);
    RC_ASSERT(static_cast<int>(distinctPeers.size()) == expectedCount);
}

// ── Property 5c: All forwarded peers are distinct ─────────────────────────
// The set of peers receiving the forwarded announcement shall contain no
// duplicates (each peer receives at most one copy).

RC_GTEST_PROP(TtlDecrementAndFanout,
              ForwardedPeersAreDistinct,
              ()) {
    int fanout = *rc::gen::inRange(1, 8);
    int peerCount = *rc::gen::inRange(fanout, fanout + 15);
    int ttl = *rc::gen::inRange(1, 11);
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

    GossipConfig config;
    config.fanout = fanout;
    config.defaultTtl = 3;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    addConnectedPeers(pm, peerCount);
    pm.clearSentMessages();

    auto ann = makeAddAnnouncement(ttl, blockId, nodeId);
    engine.handleAnnouncement(ann);

    auto messages = pm.getSentMessages();
    auto distinctPeers = extractDistinctPeerIds(messages);

    // Each peer should receive exactly one message
    RC_ASSERT(messages.size() == distinctPeers.size());
}

// ── Property 5d: TTL decrement works for Remove-type announcements ────────
// Verify the property holds for Remove-type announcements as well.

RC_GTEST_PROP(TtlDecrementAndFanout,
              RemoveAnnouncementTtlDecrement,
              ()) {
    int ttl = *rc::gen::inRange(1, 21);
    int fanout = *rc::gen::inRange(1, 6);
    int peerCount = *rc::gen::inRange(1, 15);
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

    GossipConfig config;
    config.fanout = fanout;
    config.defaultTtl = 3;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    addConnectedPeers(pm, peerCount);
    pm.clearSentMessages();

    auto ann = makeRemoveAnnouncement(ttl, blockId, nodeId);
    engine.handleAnnouncement(ann);

    auto messages = pm.getSentMessages();
    auto distinctPeers = extractDistinctPeerIds(messages);

    int expectedCount = std::min(fanout, peerCount);
    RC_ASSERT(static_cast<int>(distinctPeers.size()) == expectedCount);

    auto forwardedTtl = extractForwardedTtl(messages);
    RC_ASSERT(forwardedTtl.has_value());
    RC_ASSERT(forwardedTtl.value() == ttl - 1);
}

// ── Property 5e: TTL=0 announcements are NOT forwarded ────────────────────
// Announcements with TTL=0 should be processed locally but not forwarded.

RC_GTEST_PROP(TtlDecrementAndFanout,
              TtlZeroNotForwarded,
              ()) {
    std::string blockId = *genNonEmptyAlphaNum();
    std::string nodeId = *genNonEmptyAlphaNum();
    int peerCount = *rc::gen::inRange(1, 10);

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipConfig config;
    config.fanout = 3;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    addConnectedPeers(pm, peerCount);
    pm.clearSentMessages();

    auto ann = makeAddAnnouncement(0, blockId, nodeId);
    engine.handleAnnouncement(ann);

    auto messages = pm.getSentMessages();
    RC_ASSERT(messages.empty());
}

// ── Property 5f: HeadUpdate forwarding with TTL decrement ─────────────────
// HeadUpdate announcements should also be forwarded with TTL-1.

RC_GTEST_PROP(TtlDecrementAndFanout,
              HeadUpdateForwardingWithTtlDecrement,
              ()) {
    int ttl = *rc::gen::inRange(1, 11);
    int fanout = *rc::gen::inRange(1, 6);
    int peerCount = *rc::gen::inRange(1, 15);
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
    headRegistry.load();

    GossipConfig config;
    config.fanout = fanout;
    config.defaultTtl = 3;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    addConnectedPeers(pm, peerCount);
    pm.clearSentMessages();

    auto ann = makeHeadUpdateAnnouncement(ttl, blockId, nodeId);
    engine.handleAnnouncement(ann);

    auto messages = pm.getSentMessages();
    auto distinctPeers = extractDistinctPeerIds(messages);

    int expectedCount = std::min(fanout, peerCount);
    RC_ASSERT(static_cast<int>(distinctPeers.size()) == expectedCount);

    auto forwardedTtl = extractForwardedTtl(messages);
    RC_ASSERT(forwardedTtl.has_value());
    RC_ASSERT(forwardedTtl.value() == ttl - 1);
}
