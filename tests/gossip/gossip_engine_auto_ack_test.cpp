// Feature: cpp-gossip-protocol
// Property 9: Auto-ack on delivery with ackRequired
// **Validates: Requirements 5.6**
//
// For any message announcement delivered to a local recipient where
// ackRequired == true, the engine shall queue exactly one ack-type
// BlockAnnouncement with DeliveryAckMetadata containing the correct
// messageId, recipientId, status == "delivered", and originalSenderNode.

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
        auto tmp = std::filesystem::temp_directory_path() / "gossip_auto_ack_test";
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

void addConnectedPeers(PeerManager& pm, int count) {
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
    }
}

std::string userId(int n) {
    return "user-" + std::to_string(n);
}

/// Build a valid Add-type announcement with messageDelivery metadata.
BlockAnnouncement makeMessageAnnouncement(
    const std::vector<std::string>& recipientIds,
    int ttl,
    bool ackRequired,
    const std::string& messageId = "msg-001",
    const std::string& senderNode = "remote-node-001") {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Add;
    ann.blockId = "msg-block-001";
    ann.nodeId = senderNode;
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = ttl;

    MessageDeliveryMetadata md;
    md.messageId = messageId;
    md.recipientIds = recipientIds;
    md.priority = "normal";
    md.blockIds = {"data-block-1"};
    md.cblBlockId = "cbl-block-1";
    md.ackRequired = ackRequired;
    ann.messageDelivery = std::move(md);

    return ann;
}

/// Count ack announcements in the pending queue and return them.
std::vector<BlockAnnouncement> getAckAnnouncements(const GossipEngine& engine) {
    auto pending = engine.getPendingAnnouncements();
    std::vector<BlockAnnouncement> acks;
    for (const auto& ann : pending) {
        if (ann.type == AnnouncementType::Ack && ann.deliveryAck.has_value()) {
            acks.push_back(ann);
        }
    }
    return acks;
}

} // namespace

// ── Property 9a: ackRequired=true queues exactly one ack announcement ─────
// When a message with ackRequired=true is delivered to a local recipient,
// exactly one ack-type BlockAnnouncement is queued with correct metadata.

RC_GTEST_PROP(AutoAckOnDelivery,
              AckRequiredQueuesExactlyOneAck,
              ()) {
    // Generate 1–5 recipient IDs with guaranteed local overlap
    int numRecipients = *rc::gen::inRange(1, 6);
    int numLocalUsers = *rc::gen::inRange(1, 6);
    int overlapCount = *rc::gen::inRange(1, std::min(numRecipients, numLocalUsers) + 1);

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

    // Generate unique message ID and sender node
    int msgNum = *rc::gen::inRange(1, 10000);
    std::string messageId = "msg-" + std::to_string(msgNum);
    int senderNum = *rc::gen::inRange(1, 10000);
    std::string senderNode = "sender-node-" + std::to_string(senderNum);

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

    auto ann = makeMessageAnnouncement(recipientIds, ttl, true, messageId, senderNode);
    engine.handleAnnouncement(ann);

    // Exactly one ack announcement should be queued
    auto acks = getAckAnnouncements(engine);
    RC_ASSERT(acks.size() == 1);

    // Verify ack metadata
    const auto& ack = acks[0];
    RC_ASSERT(ack.deliveryAck->messageId == messageId);
    RC_ASSERT(ack.deliveryAck->status == "delivered");
    RC_ASSERT(ack.deliveryAck->originalSenderNode == senderNode);

    // The recipientId in the ack must be one of the local users that is also a recipient
    RC_ASSERT(localUserIds.count(ack.deliveryAck->recipientId) > 0);
    RC_ASSERT(std::find(recipientIds.begin(), recipientIds.end(),
                        ack.deliveryAck->recipientId) != recipientIds.end());
}

// ── Property 9b: ackRequired=false queues no ack announcement ─────────────
// When a message with ackRequired=false is delivered to a local recipient,
// no ack announcement is queued.

RC_GTEST_PROP(AutoAckOnDelivery,
              NoAckRequiredQueuesNoAck,
              ()) {
    int numRecipients = *rc::gen::inRange(1, 6);
    int numLocalUsers = *rc::gen::inRange(1, 6);
    int overlapCount = *rc::gen::inRange(1, std::min(numRecipients, numLocalUsers) + 1);

    std::vector<std::string> recipientIds;
    std::set<std::string> localUserIds;

    for (int i = 0; i < overlapCount; ++i) {
        std::string id = userId(i);
        recipientIds.push_back(id);
        localUserIds.insert(id);
    }
    for (int i = overlapCount; i < numRecipients; ++i) {
        recipientIds.push_back(userId(100 + i));
    }
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

    auto ann = makeMessageAnnouncement(recipientIds, ttl, false);
    engine.handleAnnouncement(ann);

    // No ack announcements should be queued
    auto acks = getAckAnnouncements(engine);
    RC_ASSERT(acks.empty());
}

// ── Property 9c: Ack recipientId is the first matching local user ─────────
// The ack's recipientId should be the first recipient in the list that
// matches a local user ID (per the implementation's iteration order).

RC_GTEST_PROP(AutoAckOnDelivery,
              AckRecipientIsFirstMatchingLocalUser,
              ()) {
    // Create a recipient list where the first local match is at a known position
    int prefixSize = *rc::gen::inRange(0, 4); // non-local recipients before the match
    int numLocalUsers = *rc::gen::inRange(1, 4);

    std::vector<std::string> recipientIds;
    std::set<std::string> localUserIds;

    // Non-local prefix
    for (int i = 0; i < prefixSize; ++i) {
        recipientIds.push_back(userId(100 + i));
    }

    // First local match
    std::string expectedMatch = userId(0);
    recipientIds.push_back(expectedMatch);
    localUserIds.insert(expectedMatch);

    // Additional local users that also appear in recipients (should not be the ack target)
    for (int i = 1; i < numLocalUsers; ++i) {
        std::string id = userId(i);
        recipientIds.push_back(id);
        localUserIds.insert(id);
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
    config.defaultTtl = 5;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);
    engine.setLocalUserIds(localUserIds);
    addConnectedPeers(pm, 5);

    auto ann = makeMessageAnnouncement(recipientIds, 5, true);
    engine.handleAnnouncement(ann);

    auto acks = getAckAnnouncements(engine);
    RC_ASSERT(acks.size() == 1);
    RC_ASSERT(acks[0].deliveryAck->recipientId == expectedMatch);
}

// ── Property 9d: No local match means no ack regardless of ackRequired ────
// When recipientIds ∩ localUserIds is empty, no ack is queued even if
// ackRequired is true (the message is forwarded, not delivered locally).

RC_GTEST_PROP(AutoAckOnDelivery,
              NoLocalMatchNoAckEvenIfRequired,
              ()) {
    int numRecipients = *rc::gen::inRange(1, 6);
    int numLocalUsers = *rc::gen::inRange(1, 6);

    std::vector<std::string> recipientIds;
    std::set<std::string> localUserIds;

    // Disjoint sets
    for (int i = 0; i < numRecipients; ++i) {
        recipientIds.push_back(userId(i));
    }
    for (int i = 0; i < numLocalUsers; ++i) {
        localUserIds.insert(userId(100 + i));
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

    auto ann = makeMessageAnnouncement(recipientIds, ttl, true);
    engine.handleAnnouncement(ann);

    // No ack should be queued (message was forwarded, not delivered locally)
    auto acks = getAckAnnouncements(engine);
    RC_ASSERT(acks.empty());
}

// ── Property 9e: Ack announcement has correct type and blockId ────────────
// The queued ack announcement must be of type Ack with blockId matching
// the messageId from the delivery metadata.

RC_GTEST_PROP(AutoAckOnDelivery,
              AckHasCorrectTypeAndBlockId,
              ()) {
    std::string localUser = userId(0);
    int msgNum = *rc::gen::inRange(1, 10000);
    std::string messageId = "msg-" + std::to_string(msgNum);

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipEngine engine(pm, blockStore, headRegistry, localMember);
    engine.setLocalUserIds({localUser});
    addConnectedPeers(pm, 3);

    auto ann = makeMessageAnnouncement({localUser}, 5, true, messageId);
    engine.handleAnnouncement(ann);

    auto acks = getAckAnnouncements(engine);
    RC_ASSERT(acks.size() == 1);
    RC_ASSERT(acks[0].type == AnnouncementType::Ack);
    RC_ASSERT(acks[0].blockId == messageId);
    RC_ASSERT(acks[0].deliveryAck.has_value());
}
