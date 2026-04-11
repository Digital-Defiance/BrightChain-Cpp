// Feature: cpp-gossip-protocol
// Property 18: Pool cache reflects announcements
// **Validates: Requirements 8.3, 8.4**
//
// Generate pool_announce and pool_deleted sequences; verify cache state matches.
// For any sequence of pool_announce and pool_deleted announcements processed
// by the GossipEngine, the PeerManager pool cache shall reflect exactly the
// set of pools that have been announced but not subsequently deleted.

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/head_registry.hpp>
#include <brightchain/member.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

using namespace brightchain::gossip;

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        auto tmp = std::filesystem::temp_directory_path() / "gossip_pool_cache_prop_test";
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

/// Represents a single operation in a pool announcement sequence.
struct PoolOp {
    bool isAnnounce; // true = pool_announce, false = pool_deleted
    std::string poolId;
    // Only meaningful when isAnnounce == true:
    int64_t blockCount;
    int64_t totalSize;
    bool encrypted;
    std::string nodeId;
};

} // namespace

// ── Property 18a: Cache state matches announce/delete sequence ─────────────
// After processing a random sequence of pool_announce and pool_deleted
// announcements, the pool cache contains exactly the pools that were
// announced and not subsequently deleted, with the latest metadata.

RC_GTEST_PROP(PoolCacheReflectsAnnouncements,
              CacheMatchesAnnounceDeleteSequence,
              ()) {
    // Generate a pool of distinct pool IDs
    int numPools = *rc::gen::inRange(1, 8);
    std::vector<std::string> poolIds;
    for (int i = 0; i < numPools; ++i) {
        poolIds.push_back("pool-" + std::to_string(i));
    }

    // Generate a random sequence of operations on those pools
    int numOps = *rc::gen::inRange(1, 30);
    std::vector<PoolOp> ops;
    for (int i = 0; i < numOps; ++i) {
        PoolOp op;
        int poolIdx = *rc::gen::inRange(0, numPools);
        op.poolId = poolIds[static_cast<size_t>(poolIdx)];
        op.isAnnounce = *rc::gen::arbitrary<bool>();
        if (op.isAnnounce) {
            op.blockCount = *rc::gen::inRange(0, 100000);
            op.totalSize = *rc::gen::inRange(0, 10000000);
            op.encrypted = *rc::gen::arbitrary<bool>();
            op.nodeId = "node-" + std::to_string(*rc::gen::inRange(0, 5));
        }
        ops.push_back(op);
    }

    // Set up infrastructure
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
    config.fanout = 1;
    config.defaultTtl = 0; // TTL=0 so no forwarding (no peers needed)
    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    // Track expected cache state as a model
    // Key: poolId → latest PoolCacheEntry (or absent if deleted)
    std::unordered_map<std::string, PoolCacheEntry> expected;

    // Process each operation
    for (const auto& op : ops) {
        BlockAnnouncement ann;
        ann.timestamp = "2025-01-28T12:00:00.000Z";
        ann.blockId = op.poolId;
        ann.poolId = op.poolId;

        if (op.isAnnounce) {
            ann.type = AnnouncementType::PoolAnnounce;
            ann.nodeId = op.nodeId;
            ann.ttl = 0;

            PoolAnnouncementMetadata meta;
            meta.blockCount = op.blockCount;
            meta.totalSize = op.totalSize;
            meta.encrypted = op.encrypted;
            ann.poolAnnouncement = meta;

            engine.handleAnnouncement(ann);

            // Update model
            PoolCacheEntry entry;
            entry.metadata = meta;
            entry.hostNodeId = op.nodeId;
            expected[op.poolId] = entry;
        } else {
            ann.type = AnnouncementType::PoolDeleted;
            ann.nodeId = "deleter-node";
            ann.ttl = 0;

            engine.handleAnnouncement(ann);

            // Update model
            expected.erase(op.poolId);
        }
    }

    // Verify cache matches expected state
    auto cache = pm.getPoolCache();
    RC_ASSERT(cache.size() == expected.size());

    for (const auto& [poolId, expectedEntry] : expected) {
        auto it = cache.find(poolId);
        RC_ASSERT(it != cache.end());
        RC_ASSERT(it->second.metadata.blockCount == expectedEntry.metadata.blockCount);
        RC_ASSERT(it->second.metadata.totalSize == expectedEntry.metadata.totalSize);
        RC_ASSERT(it->second.metadata.encrypted == expectedEntry.metadata.encrypted);
        RC_ASSERT(it->second.hostNodeId == expectedEntry.hostNodeId);
    }

    // Verify no extra entries in cache
    for (const auto& [poolId, _] : cache) {
        RC_ASSERT(expected.count(poolId) > 0);
    }
}

// ── Property 18b: Delete of non-existent pool is a no-op ─────────────────
// Deleting a pool that was never announced should leave the cache unchanged.

RC_GTEST_PROP(PoolCacheReflectsAnnouncements,
              DeleteNonExistentPoolIsNoOp,
              ()) {
    // First announce some pools
    int numPools = *rc::gen::inRange(0, 6);

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
    config.fanout = 1;
    config.defaultTtl = 0;
    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    // Announce some pools
    for (int i = 0; i < numPools; ++i) {
        BlockAnnouncement ann;
        ann.type = AnnouncementType::PoolAnnounce;
        ann.blockId = "existing-pool-" + std::to_string(i);
        ann.nodeId = "node-0";
        ann.timestamp = "2025-01-28T12:00:00.000Z";
        ann.ttl = 0;
        ann.poolId = "existing-pool-" + std::to_string(i);
        PoolAnnouncementMetadata meta;
        meta.blockCount = 100;
        meta.totalSize = 1000;
        meta.encrypted = false;
        ann.poolAnnouncement = meta;
        engine.handleAnnouncement(ann);
    }

    auto cacheBefore = pm.getPoolCache();
    RC_ASSERT(static_cast<int>(cacheBefore.size()) == numPools);

    // Delete a pool that doesn't exist
    int numDeletes = *rc::gen::inRange(1, 5);
    for (int i = 0; i < numDeletes; ++i) {
        BlockAnnouncement ann;
        ann.type = AnnouncementType::PoolDeleted;
        ann.blockId = "nonexistent-pool-" + std::to_string(i);
        ann.nodeId = "deleter";
        ann.timestamp = "2025-01-28T12:00:00.000Z";
        ann.ttl = 0;
        ann.poolId = "nonexistent-pool-" + std::to_string(i);
        engine.handleAnnouncement(ann);
    }

    auto cacheAfter = pm.getPoolCache();
    RC_ASSERT(cacheAfter.size() == cacheBefore.size());

    // All original pools still present with same data
    for (const auto& [poolId, entry] : cacheBefore) {
        auto it = cacheAfter.find(poolId);
        RC_ASSERT(it != cacheAfter.end());
        RC_ASSERT(it->second.metadata.blockCount == entry.metadata.blockCount);
        RC_ASSERT(it->second.metadata.totalSize == entry.metadata.totalSize);
        RC_ASSERT(it->second.hostNodeId == entry.hostNodeId);
    }
}

// ── Property 18c: Re-announce after delete restores pool in cache ─────────
// Announcing a pool, deleting it, then re-announcing should result in the
// pool being present with the new metadata.

RC_GTEST_PROP(PoolCacheReflectsAnnouncements,
              ReAnnounceAfterDeleteRestoresPool,
              ()) {
    int numPools = *rc::gen::inRange(1, 6);

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
    config.fanout = 1;
    config.defaultTtl = 0;
    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    for (int i = 0; i < numPools; ++i) {
        std::string poolId = "pool-" + std::to_string(i);

        // Announce
        {
            BlockAnnouncement ann;
            ann.type = AnnouncementType::PoolAnnounce;
            ann.blockId = poolId;
            ann.nodeId = "node-A";
            ann.timestamp = "2025-01-28T12:00:00.000Z";
            ann.ttl = 0;
            ann.poolId = poolId;
            PoolAnnouncementMetadata meta;
            meta.blockCount = 10;
            meta.totalSize = 100;
            meta.encrypted = false;
            ann.poolAnnouncement = meta;
            engine.handleAnnouncement(ann);
        }

        RC_ASSERT(pm.getPoolCache().count(poolId) == 1u);

        // Delete
        {
            BlockAnnouncement ann;
            ann.type = AnnouncementType::PoolDeleted;
            ann.blockId = poolId;
            ann.nodeId = "deleter";
            ann.timestamp = "2025-01-28T12:01:00.000Z";
            ann.ttl = 0;
            ann.poolId = poolId;
            engine.handleAnnouncement(ann);
        }

        RC_ASSERT(pm.getPoolCache().count(poolId) == 0u);

        // Re-announce with different metadata
        int64_t newBlockCount = *rc::gen::inRange(100, 10000);
        int64_t newTotalSize = *rc::gen::inRange(1000, 1000000);
        {
            BlockAnnouncement ann;
            ann.type = AnnouncementType::PoolAnnounce;
            ann.blockId = poolId;
            ann.nodeId = "node-B";
            ann.timestamp = "2025-01-28T12:02:00.000Z";
            ann.ttl = 0;
            ann.poolId = poolId;
            PoolAnnouncementMetadata meta;
            meta.blockCount = newBlockCount;
            meta.totalSize = newTotalSize;
            meta.encrypted = true;
            ann.poolAnnouncement = meta;
            engine.handleAnnouncement(ann);
        }

        auto cache = pm.getPoolCache();
        RC_ASSERT(cache.count(poolId) == 1u);
        RC_ASSERT(cache.at(poolId).metadata.blockCount == newBlockCount);
        RC_ASSERT(cache.at(poolId).metadata.totalSize == newTotalSize);
        RC_ASSERT(cache.at(poolId).metadata.encrypted == true);
        RC_ASSERT(cache.at(poolId).hostNodeId == "node-B");
    }
}
