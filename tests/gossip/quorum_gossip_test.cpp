// Feature: cpp-gossip-protocol
// Task 15.1: Implement quorum gossip in GossipEngine
// Tests for announceBrightTrustProposal(), announceBrightTrustVote(),
// handleAnnouncement() for quorum_proposal → trigger proposal handlers,
// quorum_vote → trigger vote handlers, and encryptedShare preservation.
// **Validates: Requirements 9.1–9.5**

#include <gtest/gtest.h>

#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/head_registry.hpp>
#include <brightchain/member.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace brightchain::gossip;

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        auto tmp = std::filesystem::temp_directory_path() / "gossip_quorum_test";
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

} // namespace

// ── Test fixture ───────────────────────────────────────────────────────────

class QuorumGossipTest : public ::testing::Test {
protected:
    void SetUp() override {
        localMember_ = std::make_unique<brightchain::Member>(
            brightchain::Member::generate(
                brightchain::MemberType::User, "test-node", "[email]"));
        ioc_ = std::make_unique<boost::asio::io_context>();
        pm_ = std::make_unique<PeerManager>(*ioc_, *localMember_);
        blockStore_ = std::make_unique<brightchain::DiskBlockStore>(
            storeTmp_.path.string(), brightchain::BlockSize::Small);
        headRegistry_ = std::make_unique<brightchain::db::HeadRegistry>(
            headTmp_.path.string());

        GossipConfig config;
        config.fanout = 3;
        config.defaultTtl = 5;
        config.messagePriority.high = {7, 7};
        engine_ = std::make_unique<GossipEngine>(
            *pm_, *blockStore_, *headRegistry_, *localMember_, config);
        addConnectedPeers(*pm_, 5);
    }

    TempDir storeTmp_;
    TempDir headTmp_;
    std::unique_ptr<boost::asio::io_context> ioc_;
    std::unique_ptr<brightchain::Member> localMember_;
    std::unique_ptr<PeerManager> pm_;
    std::unique_ptr<brightchain::DiskBlockStore> blockStore_;
    std::unique_ptr<brightchain::db::HeadRegistry> headRegistry_;
    std::unique_ptr<GossipEngine> engine_;
};

// ── Req 9.1: announceBrightTrustProposal creates quorum_proposal with high-priority TTL ──

TEST_F(QuorumGossipTest, ProposalUsesHighPriorityTtl) {
    QuorumProposalMetadata meta;
    meta.proposalId = "proposal-001";
    meta.description = "Add new member to network";
    meta.actionType = "AddMember";
    meta.actionPayload = R"({"memberId":"new-member"})";
    meta.proposerMemberId = "member-abc";
    meta.expiresAt = "2026-12-31T23:59:59.000Z";
    meta.requiredThreshold = 3;

    engine_->announceBrightTrustProposal(meta);

    auto pending = engine_->getPendingAnnouncements();
    ASSERT_EQ(pending.size(), 1u);

    const auto& ann = pending[0];
    EXPECT_EQ(ann.type, AnnouncementType::QuorumProposal);
    EXPECT_EQ(ann.blockId, "proposal-001");
    EXPECT_EQ(ann.ttl, engine_->getConfig().messagePriority.high.ttl);
    ASSERT_TRUE(ann.quorumProposal.has_value());
    EXPECT_EQ(ann.quorumProposal->proposalId, "proposal-001");
    EXPECT_EQ(ann.quorumProposal->description, "Add new member to network");
    EXPECT_EQ(ann.quorumProposal->proposerMemberId, "member-abc");
    EXPECT_EQ(ann.quorumProposal->requiredThreshold, 3);
}

// ── Req 9.2: announceBrightTrustVote creates quorum_vote with high-priority TTL ──

TEST_F(QuorumGossipTest, VoteUsesHighPriorityTtl) {
    QuorumVoteMetadata meta;
    meta.proposalId = "proposal-001";
    meta.voterMemberId = "voter-xyz";
    meta.decision = "approve";
    meta.comment = "Looks good";

    engine_->announceBrightTrustVote(meta);

    auto pending = engine_->getPendingAnnouncements();
    ASSERT_EQ(pending.size(), 1u);

    const auto& ann = pending[0];
    EXPECT_EQ(ann.type, AnnouncementType::QuorumVote);
    EXPECT_EQ(ann.blockId, "proposal-001");
    EXPECT_EQ(ann.ttl, engine_->getConfig().messagePriority.high.ttl);
    ASSERT_TRUE(ann.quorumVote.has_value());
    EXPECT_EQ(ann.quorumVote->proposalId, "proposal-001");
    EXPECT_EQ(ann.quorumVote->voterMemberId, "voter-xyz");
    EXPECT_EQ(ann.quorumVote->decision, "approve");
    ASSERT_TRUE(ann.quorumVote->comment.has_value());
    EXPECT_EQ(*ann.quorumVote->comment, "Looks good");
}

// ── Req 9.3: handleAnnouncement quorum_proposal triggers proposal handlers ──

TEST_F(QuorumGossipTest, HandleProposalTriggersHandlers) {
    std::vector<BlockAnnouncement> received;
    engine_->onBrightTrustProposal([&](const BlockAnnouncement& ann) {
        received.push_back(ann);
    });

    QuorumProposalMetadata meta;
    meta.proposalId = "proposal-handler-test";
    meta.description = "Test handler triggering";
    meta.actionType = "RemoveMember";
    meta.actionPayload = "{}";
    meta.proposerMemberId = "member-1";
    meta.expiresAt = "2026-06-01T00:00:00.000Z";
    meta.requiredThreshold = 2;

    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumProposal;
    ann.blockId = "proposal-handler-test";
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 5;
    ann.quorumProposal = meta;

    engine_->handleAnnouncement(ann);

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].type, AnnouncementType::QuorumProposal);
    ASSERT_TRUE(received[0].quorumProposal.has_value());
    EXPECT_EQ(received[0].quorumProposal->proposalId, "proposal-handler-test");
}

// ── Req 9.4: handleAnnouncement quorum_vote triggers vote handlers ──

TEST_F(QuorumGossipTest, HandleVoteTriggersHandlers) {
    std::vector<BlockAnnouncement> received;
    engine_->onBrightTrustVote([&](const BlockAnnouncement& ann) {
        received.push_back(ann);
    });

    QuorumVoteMetadata meta;
    meta.proposalId = "proposal-vote-test";
    meta.voterMemberId = "voter-1";
    meta.decision = "reject";
    meta.comment = "Not convinced";

    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumVote;
    ann.blockId = "proposal-vote-test";
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 5;
    ann.quorumVote = meta;

    engine_->handleAnnouncement(ann);

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].type, AnnouncementType::QuorumVote);
    ASSERT_TRUE(received[0].quorumVote.has_value());
    EXPECT_EQ(received[0].quorumVote->voterMemberId, "voter-1");
    EXPECT_EQ(received[0].quorumVote->decision, "reject");
}

// ── Req 9.5: encryptedShare bytes preserved through serialization round-trip ──

TEST_F(QuorumGossipTest, EncryptedSharePreservedThroughRoundTrip) {
    // Create a vote with encryptedShare containing arbitrary bytes
    std::vector<uint8_t> shareBytes = {0x00, 0x01, 0x42, 0xFF, 0xAB, 0xCD,
                                        0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
                                        0x70, 0x80, 0x90, 0xA0};

    QuorumVoteMetadata meta;
    meta.proposalId = "proposal-share-test";
    meta.voterMemberId = "voter-share";
    meta.decision = "approve";
    meta.encryptedShare = shareBytes;

    // Serialize to JSON and back
    auto json = meta.toJson();
    auto restored = QuorumVoteMetadata::fromJson(json);

    ASSERT_TRUE(restored.encryptedShare.has_value());
    EXPECT_EQ(*restored.encryptedShare, shareBytes);
}

TEST_F(QuorumGossipTest, EncryptedSharePreservedInFullAnnouncement) {
    std::vector<uint8_t> shareBytes = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF};

    QuorumVoteMetadata voteMeta;
    voteMeta.proposalId = "proposal-full-rt";
    voteMeta.voterMemberId = "voter-full";
    voteMeta.decision = "approve";
    voteMeta.encryptedShare = shareBytes;

    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumVote;
    ann.blockId = "proposal-full-rt";
    ann.nodeId = "node-1";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 7;
    ann.quorumVote = voteMeta;

    // Full BlockAnnouncement round-trip
    auto json = ann.toJson();
    auto restored = BlockAnnouncement::fromJson(json);

    ASSERT_TRUE(restored.quorumVote.has_value());
    ASSERT_TRUE(restored.quorumVote->encryptedShare.has_value());
    EXPECT_EQ(*restored.quorumVote->encryptedShare, shareBytes);
}

TEST_F(QuorumGossipTest, EmptyEncryptedShareOmittedFromJson) {
    QuorumVoteMetadata meta;
    meta.proposalId = "proposal-no-share";
    meta.voterMemberId = "voter-no-share";
    meta.decision = "reject";
    // No encryptedShare set

    auto json = meta.toJson();
    EXPECT_FALSE(json.contains("encryptedShare"));

    auto restored = QuorumVoteMetadata::fromJson(json);
    EXPECT_FALSE(restored.encryptedShare.has_value());
}

// ── Quorum proposal with attachmentCblId preserved ──

TEST_F(QuorumGossipTest, ProposalAttachmentCblIdPreserved) {
    QuorumProposalMetadata meta;
    meta.proposalId = "proposal-attach";
    meta.description = "Proposal with attachment";
    meta.actionType = "UpdatePolicy";
    meta.actionPayload = "{}";
    meta.proposerMemberId = "member-attach";
    meta.expiresAt = "2026-12-31T23:59:59.000Z";
    meta.requiredThreshold = 1;
    meta.attachmentCblId = "cbl-attachment-id";

    engine_->announceBrightTrustProposal(meta);

    auto pending = engine_->getPendingAnnouncements();
    ASSERT_EQ(pending.size(), 1u);
    ASSERT_TRUE(pending[0].quorumProposal.has_value());
    ASSERT_TRUE(pending[0].quorumProposal->attachmentCblId.has_value());
    EXPECT_EQ(*pending[0].quorumProposal->attachmentCblId, "cbl-attachment-id");
}

// ── Quorum announcements are forwarded with high-priority fanout ──

TEST_F(QuorumGossipTest, ProposalForwardedWithHighPriorityFanout) {
    pm_->clearSentMessages();

    QuorumProposalMetadata meta;
    meta.proposalId = "proposal-fwd";
    meta.description = "Forward test";
    meta.actionType = "AddMember";
    meta.actionPayload = "{}";
    meta.proposerMemberId = "member-fwd";
    meta.expiresAt = "2026-12-31T23:59:59.000Z";
    meta.requiredThreshold = 1;

    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumProposal;
    ann.blockId = "proposal-fwd";
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 5;
    ann.quorumProposal = meta;

    engine_->handleAnnouncement(ann);

    // Should have been forwarded (TTL > 0)
    auto sent = pm_->getSentMessages();
    EXPECT_GE(sent.size(), 1u);
}

TEST_F(QuorumGossipTest, ProposalWithZeroTtlNotForwarded) {
    pm_->clearSentMessages();

    QuorumProposalMetadata meta;
    meta.proposalId = "proposal-no-fwd";
    meta.description = "No forward test";
    meta.actionType = "AddMember";
    meta.actionPayload = "{}";
    meta.proposerMemberId = "member-no-fwd";
    meta.expiresAt = "2026-12-31T23:59:59.000Z";
    meta.requiredThreshold = 1;

    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumProposal;
    ann.blockId = "proposal-no-fwd";
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 0;
    ann.quorumProposal = meta;

    engine_->handleAnnouncement(ann);

    auto sent = pm_->getSentMessages();
    EXPECT_TRUE(sent.empty());
}

// ── Multiple handlers all get called ──

TEST_F(QuorumGossipTest, MultipleProposalHandlersAllCalled) {
    int count1 = 0, count2 = 0;
    engine_->onBrightTrustProposal([&](const BlockAnnouncement&) { count1++; });
    engine_->onBrightTrustProposal([&](const BlockAnnouncement&) { count2++; });

    QuorumProposalMetadata meta;
    meta.proposalId = "proposal-multi";
    meta.description = "Multi handler test";
    meta.actionType = "AddMember";
    meta.actionPayload = "{}";
    meta.proposerMemberId = "member-multi";
    meta.expiresAt = "2026-12-31T23:59:59.000Z";
    meta.requiredThreshold = 1;

    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumProposal;
    ann.blockId = "proposal-multi";
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 3;
    ann.quorumProposal = meta;

    engine_->handleAnnouncement(ann);

    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);
}

// ── Invalid quorum announcement is discarded ──

TEST_F(QuorumGossipTest, InvalidProposalDiscarded) {
    int callCount = 0;
    engine_->onBrightTrustProposal([&](const BlockAnnouncement&) { callCount++; });

    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumProposal;
    ann.blockId = "proposal-invalid";
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 3;
    // Missing quorumProposal metadata → validation fails

    engine_->handleAnnouncement(ann);

    EXPECT_EQ(callCount, 0);
}
