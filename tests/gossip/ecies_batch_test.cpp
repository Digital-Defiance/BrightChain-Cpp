// Feature: cpp-gossip-protocol
// Task 12.1: ECIES batch encryption in GossipEngine
// **Validates: Requirements 5.7, 5.8, 13.4**
//
// Tests that encryptAndSendBatch() correctly encrypts sensitive batches
// (containing messageDelivery or deliveryAck metadata) using ECIES when
// the peer has a public key, falls back to plaintext with warning when
// the peer has no public key, and sends non-sensitive batches as plaintext.

#include <gtest/gtest.h>

#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/head_registry.hpp>
#include <brightchain/member.hpp>
#include <brightchain/ecies.hpp>
#include <brightchain/ec_key_pair.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace brightchain::gossip;

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        auto tmp = std::filesystem::temp_directory_path() / "ecies_batch_test";
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

/// Add a connected peer with a valid secp256k1 public key.
/// Returns the EcKeyPair so the test can decrypt.
brightchain::EcKeyPair addPeerWithKey(PeerManager& pm,
                                       const std::string& nodeId) {
    auto kp = brightchain::EcKeyPair::generate();
    PeerInfo info;
    info.nodeId = nodeId;
    info.address = "127.0.0.1";
    info.httpPort = 3000;
    info.wsPort = 4000;
    info.lastSeen = "2025-01-28T12:00:00.000Z";
    info.connected = true;
    info.latencyMs = 10.0;
    info.publicKey = kp.publicKey(); // 33-byte compressed
    pm.addPeer(info);
    return std::move(kp);
}

/// Add a connected peer WITHOUT a public key.
void addPeerWithoutKey(PeerManager& pm, const std::string& nodeId) {
    PeerInfo info;
    info.nodeId = nodeId;
    info.address = "127.0.0.1";
    info.httpPort = 3001;
    info.wsPort = 4001;
    info.lastSeen = "2025-01-28T12:00:00.000Z";
    info.connected = true;
    info.latencyMs = 10.0;
    // publicKey left empty
    pm.addPeer(info);
}

/// Build a message-delivery announcement (sensitive).
BlockAnnouncement makeMessageDeliveryAnn(const std::string& blockId = "msg-block-001") {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Add;
    ann.blockId = blockId;
    ann.nodeId = "remote-node-001";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 5;

    MessageDeliveryMetadata md;
    md.messageId = "msg-001";
    md.recipientIds = {"user-1", "user-2"};
    md.priority = "normal";
    md.blockIds = {"data-block-1"};
    md.cblBlockId = "cbl-block-1";
    md.ackRequired = true;
    ann.messageDelivery = std::move(md);
    return ann;
}

/// Build a delivery-ack announcement (sensitive).
BlockAnnouncement makeDeliveryAckAnn(const std::string& blockId = "ack-block-001") {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Ack;
    ann.blockId = blockId;
    ann.nodeId = "remote-node-002";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 3;

    DeliveryAckMetadata ack;
    ack.messageId = "msg-001";
    ack.recipientId = "user-1";
    ack.status = "delivered";
    ack.originalSenderNode = "remote-node-001";
    ann.deliveryAck = std::move(ack);
    return ann;
}

/// Build a plain block announcement (non-sensitive).
BlockAnnouncement makePlainAnn(const std::string& blockId = "plain-block-001") {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Add;
    ann.blockId = blockId;
    ann.nodeId = "remote-node-003";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 3;
    return ann;
}

} // namespace

// ── Test fixture ───────────────────────────────────────────────────────────

class EciesBatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        localMember_ = std::make_unique<brightchain::Member>(
            brightchain::Member::generate(
                brightchain::MemberType::User, "test-node", "[email]"));
        pm_ = std::make_unique<PeerManager>(ioc_, *localMember_);
        blockStore_ = std::make_unique<brightchain::DiskBlockStore>(
            storeTmp_.path.string(), brightchain::BlockSize::Small);
        headRegistry_ = std::make_unique<brightchain::db::HeadRegistry>(
            headTmp_.path.string());

        GossipConfig config;
        config.fanout = 1; // deterministic: each announcement → 1 peer
        config.defaultTtl = 3;
        engine_ = std::make_unique<GossipEngine>(
            *pm_, *blockStore_, *headRegistry_, *localMember_, config);
    }

    boost::asio::io_context ioc_;
    TempDir storeTmp_;
    TempDir headTmp_;
    std::unique_ptr<brightchain::Member> localMember_;
    std::unique_ptr<PeerManager> pm_;
    std::unique_ptr<brightchain::DiskBlockStore> blockStore_;
    std::unique_ptr<brightchain::db::HeadRegistry> headRegistry_;
    std::unique_ptr<GossipEngine> engine_;
};


// ── Test: Sensitive batch with messageDelivery is encrypted when peer has key ──

TEST_F(EciesBatchTest, MessageDeliveryBatchEncryptedWithPeerKey) {
    auto peerKp = addPeerWithKey(*pm_, "peer-with-key");
    pm_->clearSentMessages();

    // Queue a message-delivery announcement and flush
    auto ann = makeMessageDeliveryAnn();
    engine_->announceMessage(ann.messageDelivery->blockIds,
                             *ann.messageDelivery);
    engine_->flushAnnouncements();

    auto sent = pm_->getSentMessages();
    ASSERT_FALSE(sent.empty());

    // The sent message should be an encrypted envelope
    auto j = nlohmann::json::parse(sent[0].second);
    EXPECT_TRUE(j.contains("encrypted"));
    EXPECT_TRUE(j["encrypted"].get<bool>());
    EXPECT_TRUE(j.contains("data"));
    EXPECT_TRUE(j["data"].is_array());
    EXPECT_FALSE(j["data"].empty());
}

// ── Test: Sensitive batch with deliveryAck is encrypted when peer has key ──

TEST_F(EciesBatchTest, DeliveryAckBatchEncryptedWithPeerKey) {
    auto peerKp = addPeerWithKey(*pm_, "peer-with-key");
    pm_->clearSentMessages();

    // Queue a delivery-ack announcement and flush
    auto ann = makeDeliveryAckAnn();
    engine_->sendDeliveryAck(*ann.deliveryAck);
    engine_->flushAnnouncements();

    auto sent = pm_->getSentMessages();
    ASSERT_FALSE(sent.empty());

    auto j = nlohmann::json::parse(sent[0].second);
    EXPECT_TRUE(j.contains("encrypted"));
    EXPECT_TRUE(j["encrypted"].get<bool>());
    EXPECT_TRUE(j.contains("data"));
}

// ── Test: Sensitive batch falls back to plaintext when peer has no key ──

TEST_F(EciesBatchTest, SensitiveBatchPlaintextFallbackNoKey) {
    addPeerWithoutKey(*pm_, "peer-no-key");
    pm_->clearSentMessages();

    auto ann = makeMessageDeliveryAnn();
    engine_->announceMessage(ann.messageDelivery->blockIds,
                             *ann.messageDelivery);
    engine_->flushAnnouncements();

    auto sent = pm_->getSentMessages();
    ASSERT_FALSE(sent.empty());

    // Should be plaintext JSON array (no encryption envelope)
    auto j = nlohmann::json::parse(sent[0].second);
    EXPECT_TRUE(j.is_array());
    EXPECT_FALSE(j.empty());
    // Verify it contains the announcement data
    EXPECT_TRUE(j[0].contains("blockId"));
}

// ── Test: Non-sensitive batch is sent as plaintext ──

TEST_F(EciesBatchTest, NonSensitiveBatchSentAsPlaintext) {
    addPeerWithKey(*pm_, "peer-with-key");
    pm_->clearSentMessages();

    // Queue a plain block announcement (no messageDelivery or deliveryAck)
    engine_->announceBlock("plain-block-001");
    engine_->flushAnnouncements();

    auto sent = pm_->getSentMessages();
    ASSERT_FALSE(sent.empty());

    // Should be plaintext JSON array, not encrypted envelope
    auto j = nlohmann::json::parse(sent[0].second);
    EXPECT_TRUE(j.is_array());
    EXPECT_TRUE(j[0].contains("blockId"));
    EXPECT_EQ(j[0]["blockId"].get<std::string>(), "plain-block-001");
}

// ── Test: Encrypted payload can be decrypted back to original batch JSON ──

TEST_F(EciesBatchTest, EncryptedPayloadDecryptsToOriginalBatch) {
    auto peerKp = addPeerWithKey(*pm_, "peer-with-key");
    pm_->clearSentMessages();

    auto ann = makeMessageDeliveryAnn("decrypt-test-block");
    engine_->announceMessage(ann.messageDelivery->blockIds,
                             *ann.messageDelivery);
    engine_->flushAnnouncements();

    auto sent = pm_->getSentMessages();
    ASSERT_FALSE(sent.empty());

    auto envelope = nlohmann::json::parse(sent[0].second);
    ASSERT_TRUE(envelope.contains("encrypted"));
    ASSERT_TRUE(envelope["encrypted"].get<bool>());

    // Extract encrypted bytes from the envelope
    std::vector<uint8_t> ciphertext;
    for (const auto& byte : envelope["data"]) {
        ciphertext.push_back(byte.get<uint8_t>());
    }

    // Decrypt with the peer's key pair
    auto plaintext = brightchain::Ecies::decrypt(ciphertext, peerKp);
    std::string decryptedStr(plaintext.begin(), plaintext.end());

    // Parse the decrypted JSON and verify it contains the original announcement
    auto decryptedJson = nlohmann::json::parse(decryptedStr);
    EXPECT_TRUE(decryptedJson.is_array());
    EXPECT_FALSE(decryptedJson.empty());
    // The announcement should contain messageDelivery metadata
    EXPECT_TRUE(decryptedJson[0].contains("messageDelivery"));
    EXPECT_EQ(decryptedJson[0]["messageDelivery"]["messageId"].get<std::string>(),
              "msg-001");
}

// ── Test: Mixed batch with sensitive + non-sensitive triggers encryption ──

TEST_F(EciesBatchTest, MixedBatchWithSensitiveTriggersEncryption) {
    auto peerKp = addPeerWithKey(*pm_, "peer-with-key");
    pm_->clearSentMessages();

    // Queue both a sensitive and a non-sensitive announcement
    auto msgAnn = makeMessageDeliveryAnn("mixed-msg-block");
    engine_->announceMessage(msgAnn.messageDelivery->blockIds,
                             *msgAnn.messageDelivery);
    engine_->announceBlock("mixed-plain-block");
    engine_->flushAnnouncements();

    auto sent = pm_->getSentMessages();
    ASSERT_GE(sent.size(), 2u);

    // Find the message that is encrypted (the sensitive one)
    bool foundEncrypted = false;
    bool foundPlaintext = false;
    for (const auto& [peerId, msg] : sent) {
        auto j = nlohmann::json::parse(msg);
        if (j.contains("encrypted") && j["encrypted"].get<bool>()) {
            foundEncrypted = true;
        } else if (j.is_array()) {
            foundPlaintext = true;
        }
    }
    EXPECT_TRUE(foundEncrypted) << "Expected at least one encrypted message for sensitive batch";
    EXPECT_TRUE(foundPlaintext) << "Expected at least one plaintext message for non-sensitive batch";
}

// ── Test: Encryption failure falls back to plaintext ──

TEST_F(EciesBatchTest, EncryptionFailureFallsBackToPlaintext) {
    // Add a peer with an invalid public key (wrong length)
    PeerInfo info;
    info.nodeId = "peer-bad-key";
    info.address = "127.0.0.1";
    info.httpPort = 3002;
    info.wsPort = 4002;
    info.lastSeen = "2025-01-28T12:00:00.000Z";
    info.connected = true;
    info.latencyMs = 10.0;
    info.publicKey = {0x01, 0x02, 0x03}; // invalid: not 33 bytes
    pm_->addPeer(info);
    pm_->clearSentMessages();

    auto ann = makeMessageDeliveryAnn("fail-encrypt-block");
    engine_->announceMessage(ann.messageDelivery->blockIds,
                             *ann.messageDelivery);
    engine_->flushAnnouncements();

    auto sent = pm_->getSentMessages();
    ASSERT_FALSE(sent.empty());

    // Should fall back to plaintext JSON array (encryption failed)
    auto j = nlohmann::json::parse(sent[0].second);
    EXPECT_TRUE(j.is_array()) << "Expected plaintext fallback after encryption failure";
    EXPECT_TRUE(j[0].contains("blockId"));
}
