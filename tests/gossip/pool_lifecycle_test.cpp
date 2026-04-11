// Feature: cpp-gossip-protocol
// Task 14.1: Pool lifecycle in GossipEngine and PeerManager
// Tests for announcePoolCreation(), announcePoolRemoval(), announcePoolDeletion(),
// handleAnnouncement() for pool_announce → PeerManager::updatePoolCache(),
// pool_deleted → PeerManager::removePoolFromCache(), and ECIES-encrypted metadata.
// **Validates: Requirements 8.1–8.5**

#include <gtest/gtest.h>

#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/head_registry.hpp>
#include <brightchain/member.hpp>
#include <brightchain/ecies.hpp>
#include <brightchain/ec_key_pair.hpp>

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
        auto tmp = std::filesystem::temp_directory_path() / "gossip_pool_lifecycle_test";
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

class PoolLifecycleTest : public ::testing::Test {
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

// ── Req 8.1: announcePoolCreation creates pool_announce announcement ──────

TEST_F(PoolLifecycleTest, AnnouncePoolCreationCreatesCorrectAnnouncement) {
    PoolAnnouncementMetadata meta;
    meta.blockCount = 1024;
    meta.totalSize = 1073741824;
    meta.encrypted = false;

    engine_->announcePoolCreation("pool-abc", meta);

    auto pending = engine_->getPendingAnnouncements();
    ASSERT_EQ(pending.size(), 1u);

    const auto& ann = pending[0];
    EXPECT_EQ(ann.type, AnnouncementType::PoolAnnounce);
    EXPECT_EQ(ann.blockId, "pool-abc");
    EXPECT_EQ(ann.ttl, engine_->getConfig().defaultTtl);
    ASSERT_TRUE(ann.poolId.has_value());
    EXPECT_EQ(*ann.poolId, "pool-abc");
    ASSERT_TRUE(ann.poolAnnouncement.has_value());
    EXPECT_EQ(ann.poolAnnouncement->blockCount, 1024);
    EXPECT_EQ(ann.poolAnnouncement->totalSize, 1073741824);
    EXPECT_FALSE(ann.poolAnnouncement->encrypted);
    EXPECT_FALSE(ann.poolAnnouncement->encryptedMetadata.has_value());
}

// ── Req 8.2: announcePoolRemoval creates pool_remove announcement ─────────

TEST_F(PoolLifecycleTest, AnnouncePoolRemovalCreatesCorrectAnnouncement) {
    engine_->announcePoolRemoval("pool-xyz");

    auto pending = engine_->getPendingAnnouncements();
    ASSERT_EQ(pending.size(), 1u);

    const auto& ann = pending[0];
    EXPECT_EQ(ann.type, AnnouncementType::PoolRemove);
    EXPECT_EQ(ann.blockId, "pool-xyz");
    ASSERT_TRUE(ann.poolId.has_value());
    EXPECT_EQ(*ann.poolId, "pool-xyz");
    EXPECT_EQ(ann.ttl, engine_->getConfig().defaultTtl);
}

// ── Req 8.2: announcePoolDeletion creates pool_deleted announcement ───────

TEST_F(PoolLifecycleTest, AnnouncePoolDeletionCreatesCorrectAnnouncement) {
    engine_->announcePoolDeletion("pool-del");

    auto pending = engine_->getPendingAnnouncements();
    ASSERT_EQ(pending.size(), 1u);

    const auto& ann = pending[0];
    EXPECT_EQ(ann.type, AnnouncementType::PoolDeleted);
    EXPECT_EQ(ann.blockId, "pool-del");
    ASSERT_TRUE(ann.poolId.has_value());
    EXPECT_EQ(*ann.poolId, "pool-del");
    EXPECT_EQ(ann.ttl, engine_->getConfig().defaultTtl);
}

// ── Req 8.3: handleAnnouncement pool_announce updates PeerManager cache ───

TEST_F(PoolLifecycleTest, HandlePoolAnnounceUpdatesPeerManagerCache) {
    PoolAnnouncementMetadata meta;
    meta.blockCount = 500;
    meta.totalSize = 500000;
    meta.encrypted = false;

    BlockAnnouncement ann;
    ann.type = AnnouncementType::PoolAnnounce;
    ann.blockId = "pool-cache-1";
    ann.nodeId = "remote-node-001";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 3;
    ann.poolId = "pool-cache-1";
    ann.poolAnnouncement = meta;

    engine_->handleAnnouncement(ann);

    auto cache = pm_->getPoolCache();
    ASSERT_EQ(cache.count("pool-cache-1"), 1u);
    EXPECT_EQ(cache["pool-cache-1"].metadata.blockCount, 500);
    EXPECT_EQ(cache["pool-cache-1"].metadata.totalSize, 500000);
    EXPECT_FALSE(cache["pool-cache-1"].metadata.encrypted);
    EXPECT_EQ(cache["pool-cache-1"].hostNodeId, "remote-node-001");
}

// ── Req 8.4: handleAnnouncement pool_deleted removes from cache ───────────

TEST_F(PoolLifecycleTest, HandlePoolDeletedRemovesFromCache) {
    // First add a pool to the cache
    PoolAnnouncementMetadata meta;
    meta.blockCount = 100;
    meta.totalSize = 10000;
    meta.encrypted = false;
    pm_->updatePoolCache("pool-to-delete", meta, "remote-node-002");

    auto cacheBefore = pm_->getPoolCache();
    ASSERT_EQ(cacheBefore.count("pool-to-delete"), 1u);

    // Now handle a pool_deleted announcement
    BlockAnnouncement ann;
    ann.type = AnnouncementType::PoolDeleted;
    ann.blockId = "pool-to-delete";
    ann.nodeId = "remote-node-002";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 3;
    ann.poolId = "pool-to-delete";

    engine_->handleAnnouncement(ann);

    auto cacheAfter = pm_->getPoolCache();
    EXPECT_EQ(cacheAfter.count("pool-to-delete"), 0u);
}

// ── Req 8.5: Encrypted pool metadata has encryptedMetadata field ──────────

TEST_F(PoolLifecycleTest, EncryptedPoolHasEncryptedMetadata) {
    PoolAnnouncementMetadata meta;
    meta.blockCount = 256;
    meta.totalSize = 65536;
    meta.encrypted = true;

    engine_->announcePoolCreation("pool-enc", meta);

    auto pending = engine_->getPendingAnnouncements();
    ASSERT_EQ(pending.size(), 1u);

    const auto& ann = pending[0];
    EXPECT_EQ(ann.type, AnnouncementType::PoolAnnounce);
    ASSERT_TRUE(ann.poolAnnouncement.has_value());
    EXPECT_TRUE(ann.poolAnnouncement->encrypted);
    // encryptedMetadata should be populated with a non-empty base64 string
    ASSERT_TRUE(ann.poolAnnouncement->encryptedMetadata.has_value());
    EXPECT_FALSE(ann.poolAnnouncement->encryptedMetadata->empty());
}

// ── Req 8.5: Non-encrypted pool does NOT have encryptedMetadata ───────────

TEST_F(PoolLifecycleTest, NonEncryptedPoolHasNoEncryptedMetadata) {
    PoolAnnouncementMetadata meta;
    meta.blockCount = 256;
    meta.totalSize = 65536;
    meta.encrypted = false;

    engine_->announcePoolCreation("pool-plain", meta);

    auto pending = engine_->getPendingAnnouncements();
    ASSERT_EQ(pending.size(), 1u);

    const auto& ann = pending[0];
    ASSERT_TRUE(ann.poolAnnouncement.has_value());
    EXPECT_FALSE(ann.poolAnnouncement->encrypted);
    EXPECT_FALSE(ann.poolAnnouncement->encryptedMetadata.has_value());
}

// ── Req 8.5: Encrypted metadata is decryptable by the local node ──────────

TEST_F(PoolLifecycleTest, EncryptedMetadataIsDecryptableByLocalNode) {
    PoolAnnouncementMetadata meta;
    meta.blockCount = 42;
    meta.totalSize = 9999;
    meta.encrypted = true;

    engine_->announcePoolCreation("pool-decrypt-test", meta);

    auto pending = engine_->getPendingAnnouncements();
    ASSERT_EQ(pending.size(), 1u);

    const auto& ann = pending[0];
    ASSERT_TRUE(ann.poolAnnouncement.has_value());
    ASSERT_TRUE(ann.poolAnnouncement->encryptedMetadata.has_value());

    // Decode base64 to bytes
    const std::string& b64 = *ann.poolAnnouncement->encryptedMetadata;
    // Simple base64 decode
    auto b64Decode = [](const std::string& encoded) -> std::vector<uint8_t> {
        static const std::string chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<uint8_t> result;
        int val = 0, bits = -8;
        for (char c : encoded) {
            if (c == '=') break;
            auto pos = chars.find(c);
            if (pos == std::string::npos) continue;
            val = (val << 6) + static_cast<int>(pos);
            bits += 6;
            if (bits >= 0) {
                result.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
                bits -= 8;
            }
        }
        return result;
    };

    auto encryptedBytes = b64Decode(b64);
    ASSERT_FALSE(encryptedBytes.empty());

    // Decrypt using the local node's key pair
    auto privKeyBytes = localMember_->privateKey();
    auto pubKeyBytes = localMember_->publicKey();
    auto keyPair = brightchain::EcKeyPair::fromPrivateKey(privKeyBytes);

    auto decrypted = brightchain::Ecies::decrypt(encryptedBytes, keyPair);
    std::string decryptedStr(decrypted.begin(), decrypted.end());

    // Parse the decrypted JSON and verify contents
    auto j = nlohmann::json::parse(decryptedStr);
    EXPECT_EQ(j["blockCount"].get<int64_t>(), 42);
    EXPECT_EQ(j["totalSize"].get<int64_t>(), 9999);
    EXPECT_TRUE(j["encrypted"].get<bool>());
}

// ── Pre-supplied encryptedMetadata is preserved (not overwritten) ─────────

TEST_F(PoolLifecycleTest, PreSuppliedEncryptedMetadataPreserved) {
    PoolAnnouncementMetadata meta;
    meta.blockCount = 10;
    meta.totalSize = 1000;
    meta.encrypted = true;
    meta.encryptedMetadata = "pre-existing-base64-data";

    engine_->announcePoolCreation("pool-presupplied", meta);

    auto pending = engine_->getPendingAnnouncements();
    ASSERT_EQ(pending.size(), 1u);

    const auto& ann = pending[0];
    ASSERT_TRUE(ann.poolAnnouncement.has_value());
    ASSERT_TRUE(ann.poolAnnouncement->encryptedMetadata.has_value());
    // Should keep the pre-supplied value, not overwrite it
    EXPECT_EQ(*ann.poolAnnouncement->encryptedMetadata, "pre-existing-base64-data");
}

// ── Multiple pool_announce updates overwrite cache correctly ──────────────

TEST_F(PoolLifecycleTest, MultiplePoolAnnouncesOverwriteCache) {
    PoolAnnouncementMetadata meta1;
    meta1.blockCount = 100;
    meta1.totalSize = 10000;
    meta1.encrypted = false;

    BlockAnnouncement ann1;
    ann1.type = AnnouncementType::PoolAnnounce;
    ann1.blockId = "pool-update";
    ann1.nodeId = "node-A";
    ann1.timestamp = "2025-01-28T12:00:00.000Z";
    ann1.ttl = 3;
    ann1.poolId = "pool-update";
    ann1.poolAnnouncement = meta1;

    engine_->handleAnnouncement(ann1);

    auto cache1 = pm_->getPoolCache();
    ASSERT_EQ(cache1["pool-update"].metadata.blockCount, 100);
    ASSERT_EQ(cache1["pool-update"].hostNodeId, "node-A");

    // Second announcement with updated metadata from a different node
    PoolAnnouncementMetadata meta2;
    meta2.blockCount = 200;
    meta2.totalSize = 20000;
    meta2.encrypted = true;

    BlockAnnouncement ann2;
    ann2.type = AnnouncementType::PoolAnnounce;
    ann2.blockId = "pool-update";
    ann2.nodeId = "node-B";
    ann2.timestamp = "2025-01-28T12:01:00.000Z";
    ann2.ttl = 3;
    ann2.poolId = "pool-update";
    ann2.poolAnnouncement = meta2;

    engine_->handleAnnouncement(ann2);

    auto cache2 = pm_->getPoolCache();
    EXPECT_EQ(cache2["pool-update"].metadata.blockCount, 200);
    EXPECT_EQ(cache2["pool-update"].metadata.totalSize, 20000);
    EXPECT_TRUE(cache2["pool-update"].metadata.encrypted);
    EXPECT_EQ(cache2["pool-update"].hostNodeId, "node-B");
}

// ── pool_deleted for non-existent pool is a no-op ─────────────────────────

TEST_F(PoolLifecycleTest, PoolDeletedForNonExistentPoolIsNoOp) {
    auto cacheBefore = pm_->getPoolCache();
    EXPECT_EQ(cacheBefore.count("nonexistent-pool"), 0u);

    BlockAnnouncement ann;
    ann.type = AnnouncementType::PoolDeleted;
    ann.blockId = "nonexistent-pool";
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 3;
    ann.poolId = "nonexistent-pool";

    // Should not throw or crash
    engine_->handleAnnouncement(ann);

    auto cacheAfter = pm_->getPoolCache();
    EXPECT_EQ(cacheAfter.count("nonexistent-pool"), 0u);
}

// ── pool_announce without poolId is discarded by validation ───────────────

TEST_F(PoolLifecycleTest, PoolAnnounceWithoutPoolIdDiscarded) {
    PoolAnnouncementMetadata meta;
    meta.blockCount = 10;
    meta.totalSize = 1000;
    meta.encrypted = false;

    BlockAnnouncement ann;
    ann.type = AnnouncementType::PoolAnnounce;
    ann.blockId = "pool-no-id";
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 3;
    // No poolId set
    ann.poolAnnouncement = meta;

    engine_->handleAnnouncement(ann);

    // Should not be in cache since validation fails
    auto cache = pm_->getPoolCache();
    EXPECT_TRUE(cache.empty());
}

// ── pool_announce with TTL > 0 is forwarded ───────────────────────────────

TEST_F(PoolLifecycleTest, PoolAnnounceWithTtlIsForwarded) {
    pm_->clearSentMessages();

    PoolAnnouncementMetadata meta;
    meta.blockCount = 50;
    meta.totalSize = 5000;
    meta.encrypted = false;

    BlockAnnouncement ann;
    ann.type = AnnouncementType::PoolAnnounce;
    ann.blockId = "pool-forward";
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 3;
    ann.poolId = "pool-forward";
    ann.poolAnnouncement = meta;

    engine_->handleAnnouncement(ann);

    // Should have been forwarded to peers (fanout = 3)
    auto sent = pm_->getSentMessages();
    EXPECT_GE(sent.size(), 1u);
}

// ── pool_announce with TTL = 0 is NOT forwarded ───────────────────────────

TEST_F(PoolLifecycleTest, PoolAnnounceWithZeroTtlNotForwarded) {
    pm_->clearSentMessages();

    PoolAnnouncementMetadata meta;
    meta.blockCount = 50;
    meta.totalSize = 5000;
    meta.encrypted = false;

    BlockAnnouncement ann;
    ann.type = AnnouncementType::PoolAnnounce;
    ann.blockId = "pool-no-forward";
    ann.nodeId = "remote-node";
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = 0;
    ann.poolId = "pool-no-forward";
    ann.poolAnnouncement = meta;

    engine_->handleAnnouncement(ann);

    // Cache should still be updated
    auto cache = pm_->getPoolCache();
    EXPECT_EQ(cache.count("pool-no-forward"), 1u);

    // But no forwarding should have occurred
    auto sent = pm_->getSentMessages();
    EXPECT_TRUE(sent.empty());
}
