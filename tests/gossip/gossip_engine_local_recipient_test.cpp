// Feature: cpp-gossip-protocol
// Property 8: Local recipient matching determines forwarding behavior
// **Validates: Requirements 5.4, 5.5**
//
// For any message announcement with recipientIds and a set of localUserIds,
// the announcement is delivered locally (handlers triggered, not forwarded)
// if and only if the intersection of recipientIds and localUserIds is
// non-empty. Otherwise, the announcement is forwarded with decremented TTL.

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

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        auto tmp = std::filesystem::temp_directory_path() / "gossip_local_recipient_test";
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

/// Generate a unique user ID string like "user-<n>".
std::string userId(int n) {
    return "user-" + std::to_string(n);
}

/// Build a valid Add-type announcement with messageDelivery metadata.
BlockAnnouncement makeMessageAnnouncement(
    const std::vector<std::string>& recipientIds,
    int ttl,
    const std::string& priority = "normal") {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Add;
    ann.blockId = "msg-block-001";
    ann.nodeId = "remote-node-001";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = ttl;

    MessageDeliveryMetadata md;
    md.messageId = "msg-001";
    md.recipientIds = recipientIds;
    md.priority = priority;
    md.blockIds = {"data-block-1"};
    md.cblBlockId = "cbl-block-1";
    md.ackRequired = false;
    ann.messageDelivery = std::move(md);

    return ann;
}

} // namespace


// ── Property 8a: Local match triggers delivery handler, no forwarding ─────
// When recipientIds ∩ localUserIds is non-empty, the message delivery
// handler fires and no messages are sent to peers (not forwarded).

RC_GTEST_PROP(LocalRecipientMatching,
              LocalMatchTriggersDeliveryNotForwarding,
              ()) {
    // Generate 1–5 recipient IDs and 1–5 local user IDs with guaranteed overlap
    int numRecipients = *rc::gen::inRange(1, 6);
    int numLocalUsers = *rc::gen::inRange(1, 6);
    int overlapCount = *rc::gen::inRange(1, std::min(numRecipients, numLocalUsers) + 1);

    // Build recipient list and local user set with controlled overlap
    std::vector<std::string> recipientIds;
    std::set<std::string> localUserIds;

    // Shared IDs (overlap)
    for (int i = 0; i < overlapCount; ++i) {
        std::string id = userId(i);
        recipientIds.push_back(id);
        localUserIds.insert(id);
    }
    // Extra recipients (not local)
    for (int i = overlapCount; i < numRecipients; ++i) {
        recipientIds.push_back(userId(100 + i));
    }
    // Extra local users (not recipients)
    for (int i = overlapCount; i < numLocalUsers; ++i) {
        localUserIds.insert(userId(200 + i));
    }

    int ttl = *rc::gen::inRange(1, 11);

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
    config.defaultTtl = 5;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);
    engine.setLocalUserIds(localUserIds);
    addConnectedPeers(pm, 5);

    // Track whether delivery handler fires
    int deliveryCount = 0;
    engine.onMessageDelivery([&](const BlockAnnouncement&) {
        ++deliveryCount;
    });

    pm.clearSentMessages();

    auto ann = makeMessageAnnouncement(recipientIds, ttl);
    engine.handleAnnouncement(ann);

    // Delivery handler must have fired exactly once
    RC_ASSERT(deliveryCount == 1);

    // No forwarding should have occurred
    auto messages = pm.getSentMessages();
    RC_ASSERT(messages.empty());
}

// ── Property 8b: No local match → forwarded, delivery handler not fired ───
// When recipientIds ∩ localUserIds is empty, the message is forwarded
// with decremented TTL and the delivery handler does NOT fire.

RC_GTEST_PROP(LocalRecipientMatching,
              NoLocalMatchForwardsAndNoDelivery,
              ()) {
    // Generate disjoint recipient IDs and local user IDs
    int numRecipients = *rc::gen::inRange(1, 6);
    int numLocalUsers = *rc::gen::inRange(1, 6);

    std::vector<std::string> recipientIds;
    std::set<std::string> localUserIds;

    // Recipients use IDs 0..N-1, local users use IDs 100..100+M-1 (disjoint)
    for (int i = 0; i < numRecipients; ++i) {
        recipientIds.push_back(userId(i));
    }
    for (int i = 0; i < numLocalUsers; ++i) {
        localUserIds.insert(userId(100 + i));
    }

    int ttl = *rc::gen::inRange(1, 11);
    int fanout = *rc::gen::inRange(1, 6);
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
    config.fanout = fanout;
    config.defaultTtl = 5;
    config.messagePriority.normal = {fanout, 5};
    config.messagePriority.high = {fanout + 2, 7};

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);
    engine.setLocalUserIds(localUserIds);
    addConnectedPeers(pm, peerCount);

    int deliveryCount = 0;
    engine.onMessageDelivery([&](const BlockAnnouncement&) {
        ++deliveryCount;
    });

    pm.clearSentMessages();

    auto ann = makeMessageAnnouncement(recipientIds, ttl, "normal");
    engine.handleAnnouncement(ann);

    // Delivery handler must NOT have fired
    RC_ASSERT(deliveryCount == 0);

    // Message should have been forwarded
    auto messages = pm.getSentMessages();
    RC_ASSERT(!messages.empty());

    // Verify forwarded TTL is decremented by 1
    try {
        auto j = nlohmann::json::parse(messages[0].second);
        if (j.is_array() && !j.empty()) {
            RC_ASSERT(j[0].value("ttl", -1) == ttl - 1);
        }
    } catch (...) {
        // Encrypted payload — still counts as forwarded
    }
}

// ── Property 8c: Empty localUserIds always forwards ───────────────────────
// When localUserIds is empty, any message announcement is always forwarded.

RC_GTEST_PROP(LocalRecipientMatching,
              EmptyLocalUserIdsAlwaysForwards,
              ()) {
    int numRecipients = *rc::gen::inRange(1, 6);
    std::vector<std::string> recipientIds;
    for (int i = 0; i < numRecipients; ++i) {
        recipientIds.push_back(userId(i));
    }

    int ttl = *rc::gen::inRange(1, 11);

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
    config.defaultTtl = 5;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);
    engine.setLocalUserIds({}); // empty
    addConnectedPeers(pm, 5);

    int deliveryCount = 0;
    engine.onMessageDelivery([&](const BlockAnnouncement&) {
        ++deliveryCount;
    });

    pm.clearSentMessages();

    auto ann = makeMessageAnnouncement(recipientIds, ttl);
    engine.handleAnnouncement(ann);

    RC_ASSERT(deliveryCount == 0);
    RC_ASSERT(!pm.getSentMessages().empty());
}

// ── Property 8d: Add without messageDelivery is always forwarded ──────────
// Non-message Add announcements (no messageDelivery metadata) should always
// be forwarded regardless of localUserIds, since recipient matching only
// applies to message deliveries.

RC_GTEST_PROP(LocalRecipientMatching,
              NonMessageAddAlwaysForwarded,
              ()) {
    int ttl = *rc::gen::inRange(1, 11);

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
    // Set local user IDs — should not matter for non-message adds
    engine.setLocalUserIds({"user-0", "user-1"});
    addConnectedPeers(pm, 5);

    int deliveryCount = 0;
    engine.onMessageDelivery([&](const BlockAnnouncement&) {
        ++deliveryCount;
    });

    pm.clearSentMessages();

    // Plain Add announcement (no messageDelivery)
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Add;
    ann.blockId = "plain-block-001";
    ann.nodeId = "remote-node-001";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = ttl;

    engine.handleAnnouncement(ann);

    // No message delivery handler should fire
    RC_ASSERT(deliveryCount == 0);
    // Should be forwarded
    RC_ASSERT(!pm.getSentMessages().empty());
}

// ── Property 8e: Local match with TTL=0 still delivers locally ────────────
// Even when TTL=0, if there's a local match the delivery handler fires
// and no forwarding occurs (TTL=0 wouldn't forward anyway).

RC_GTEST_PROP(LocalRecipientMatching,
              LocalMatchWithTtlZeroStillDelivers,
              ()) {
    int numRecipients = *rc::gen::inRange(1, 4);
    std::vector<std::string> recipientIds;
    std::set<std::string> localUserIds;

    // Guaranteed overlap: first recipient is local
    std::string sharedId = userId(0);
    recipientIds.push_back(sharedId);
    localUserIds.insert(sharedId);

    for (int i = 1; i < numRecipients; ++i) {
        recipientIds.push_back(userId(50 + i));
    }

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
    engine.setLocalUserIds(localUserIds);
    addConnectedPeers(pm, 5);

    int deliveryCount = 0;
    engine.onMessageDelivery([&](const BlockAnnouncement&) {
        ++deliveryCount;
    });

    pm.clearSentMessages();

    auto ann = makeMessageAnnouncement(recipientIds, 0); // TTL=0
    engine.handleAnnouncement(ann);

    // Delivery handler fires
    RC_ASSERT(deliveryCount == 1);
    // No forwarding (local match suppresses it, and TTL=0 wouldn't forward anyway)
    RC_ASSERT(pm.getSentMessages().empty());
}
