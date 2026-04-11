// Feature: cpp-gossip-protocol
// Property 28: Bloom filter pre-check filtering
// **Validates: Requirements 4.3**
//
// For any set of peers with Bloom filters, discoverBlock() shall only query
// peers whose Bloom filter indicates the block might be present
// (mightContain == true). Peers whose Bloom filter definitively says the
// block is NOT present shall never appear in the results.

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/discovery_protocol.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/block_size.hpp>
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
        auto tmp = std::filesystem::temp_directory_path() / "bloom_precheck_test";
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

/// Generate a hex block ID string (8–64 hex chars).
rc::Gen<std::string> genBlockId() {
    return rc::gen::exec([] {
        static const char hexChars[] = "0123456789abcdef";
        int len = *rc::gen::inRange(8, 65);
        std::string id;
        id.reserve(static_cast<size_t>(len));
        for (int i = 0; i < len; ++i) {
            id += hexChars[*rc::gen::inRange(0, 16)];
        }
        return id;
    });
}

/// Add N connected peers to a PeerManager, returning their nodeIds.
std::vector<std::string> addPeers(PeerManager& pm, int count) {
    std::vector<std::string> ids;
    for (int i = 0; i < count; ++i) {
        PeerInfo info;
        info.nodeId = "peer-" + std::to_string(i);
        info.address = "127.0.0." + std::to_string(i + 1);
        info.httpPort = 3000;
        info.wsPort = 3000;
        info.lastSeen = "2025-01-28T12:00:00.000Z";
        info.capabilities = {"blocks", "gossip"};
        info.connected = true;
        info.latencyMs = static_cast<double>(i + 1);
        info.publicKey = {0x02};
        pm.addPeer(info);
        ids.push_back(info.nodeId);
    }
    return ids;
}

} // namespace

// ── Property 28: Bloom filter pre-check filtering ──────────────────────────
// Generate peer sets where each peer has a Bloom filter. Some filters contain
// the target block, others do not. Verify that only peers whose filter says
// mightContain==true are queried (appear in results).

RC_GTEST_PROP(BloomFilterPreCheckFiltering,
              OnlyPeersWithMightContainAreQueried,
              ()) {
    // Generate 3–15 peers and a target block ID
    int peerCount = *rc::gen::inRange(3, 16);
    auto targetBlockId = *genBlockId();

    // Generate a set of "extra" block IDs that some peers will have
    int extraBlockCount = *rc::gen::inRange(1, 20);
    std::vector<std::string> extraBlocks;
    extraBlocks.reserve(static_cast<size_t>(extraBlockCount));
    for (int i = 0; i < extraBlockCount; ++i) {
        extraBlocks.push_back(*genBlockId());
    }

    TempDir storeTmp;
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);

    auto peerIds = addPeers(pm, peerCount);

    DiscoveryConfig config;
    config.maxConcurrentQueries = peerCount; // allow querying all candidates
    DiscoveryProtocol discovery(pm, blockStore, config);

    // For each peer, decide whether its Bloom filter contains the target block.
    // Build a Bloom filter accordingly and register it.
    std::set<std::string> peersWithBlock; // peers whose filter contains target

    for (int i = 0; i < peerCount; ++i) {
        bool shouldContainTarget = *rc::gen::arbitrary<bool>();

        // Create a Bloom filter with some extra blocks
        BloomFilter filter(100, 0.01, 7);

        // Add some random extra blocks to make the filter non-trivial
        int extrasToAdd = *rc::gen::inRange(0, static_cast<int>(extraBlocks.size()) + 1);
        for (int e = 0; e < extrasToAdd; ++e) {
            int idx = *rc::gen::inRange(0, static_cast<int>(extraBlocks.size()));
            filter.add(extraBlocks[static_cast<size_t>(idx)]);
        }

        if (shouldContainTarget) {
            filter.add(targetBlockId);
            peersWithBlock.insert(peerIds[static_cast<size_t>(i)]);
        }

        discovery.setPeerBloomFilter(peerIds[static_cast<size_t>(i)], std::move(filter));

        // All peers respond positively if queried — this way, any peer that
        // gets past the Bloom filter pre-check will appear in results.
        discovery.setPeerQueryResponse(peerIds[static_cast<size_t>(i)],
                                       targetBlockId, true);
    }

    auto result = discovery.discoverBlock(targetBlockId);

    // Collect the nodeIds that appeared in the results
    std::set<std::string> queriedPeerIds;
    for (const auto& loc : result.locations) {
        queriedPeerIds.insert(loc.nodeId);
    }

    // Every peer in the results must have had mightContain==true
    for (const auto& nodeId : queriedPeerIds) {
        RC_ASSERT(peersWithBlock.count(nodeId) > 0);
    }

    // Every peer whose filter contained the target must appear in results
    // (since all respond positively and maxConcurrentQueries >= peerCount)
    for (const auto& nodeId : peersWithBlock) {
        RC_ASSERT(queriedPeerIds.count(nodeId) > 0);
    }

    // The number of queried peers should equal the number with the block
    // in their Bloom filter (no extras, no missing)
    RC_ASSERT(result.queriedPeers == static_cast<int>(peersWithBlock.size()));
}

// ── Property: Peers without Bloom filters are still queried (conservative) ─
// When some peers have no Bloom filter set, they should be included as
// candidates (conservative behavior: query when we can't pre-filter).

RC_GTEST_PROP(BloomFilterPreCheckFiltering,
              PeersWithoutFilterAreStillQueried,
              ()) {
    int peerCount = *rc::gen::inRange(3, 12);
    auto targetBlockId = *genBlockId();

    TempDir storeTmp;
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);

    auto peerIds = addPeers(pm, peerCount);

    DiscoveryConfig config;
    config.maxConcurrentQueries = peerCount;
    DiscoveryProtocol discovery(pm, blockStore, config);

    // Give Bloom filters to only some peers; leave others without
    std::set<std::string> peersExpectedToBeQueried;

    for (int i = 0; i < peerCount; ++i) {
        // 0 = no filter, 1 = filter with target, 2 = filter without target
        int filterChoice = *rc::gen::inRange(0, 3);

        if (filterChoice == 0) {
            // No Bloom filter — peer should still be queried (conservative)
            peersExpectedToBeQueried.insert(peerIds[static_cast<size_t>(i)]);
        } else if (filterChoice == 1) {
            // Filter contains the target block
            BloomFilter filter(100, 0.01, 7);
            filter.add(targetBlockId);
            discovery.setPeerBloomFilter(peerIds[static_cast<size_t>(i)], std::move(filter));
            peersExpectedToBeQueried.insert(peerIds[static_cast<size_t>(i)]);
        } else {
            // Filter does NOT contain the target block — should be skipped
            BloomFilter filter(100, 0.01, 7);
            filter.add("definitely-not-the-target-block-id-xyz");
            discovery.setPeerBloomFilter(peerIds[static_cast<size_t>(i)], std::move(filter));
        }

        // All peers respond positively if queried
        discovery.setPeerQueryResponse(peerIds[static_cast<size_t>(i)],
                                       targetBlockId, true);
    }

    auto result = discovery.discoverBlock(targetBlockId);

    std::set<std::string> queriedPeerIds;
    for (const auto& loc : result.locations) {
        queriedPeerIds.insert(loc.nodeId);
    }

    // Every expected peer should be in results
    for (const auto& nodeId : peersExpectedToBeQueried) {
        RC_ASSERT(queriedPeerIds.count(nodeId) > 0);
    }

    // No unexpected peers should be in results
    for (const auto& nodeId : queriedPeerIds) {
        RC_ASSERT(peersExpectedToBeQueried.count(nodeId) > 0);
    }
}

// ── Deterministic test: all filters reject → no peers queried ──────────────

TEST(BloomFilterPreCheckFiltering, AllFiltersRejectMeansNoPeersQueried) {
    TempDir storeTmp;
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);

    auto peerIds = addPeers(pm, 5);

    DiscoveryProtocol discovery(pm, blockStore);

    std::string targetBlockId = "aabbccdd11223344";

    // Give every peer a Bloom filter that does NOT contain the target
    for (const auto& peerId : peerIds) {
        BloomFilter filter(100, 0.01, 7);
        filter.add("some-other-block-not-the-target");
        discovery.setPeerBloomFilter(peerId, std::move(filter));
        discovery.setPeerQueryResponse(peerId, targetBlockId, true);
    }

    auto result = discovery.discoverBlock(targetBlockId);

    EXPECT_FALSE(result.found);
    EXPECT_EQ(result.queriedPeers, 0);
    EXPECT_TRUE(result.locations.empty());
}

// ── Deterministic test: mixed filters → only matching peers queried ────────

TEST(BloomFilterPreCheckFiltering, MixedFiltersOnlyMatchingQueried) {
    TempDir storeTmp;
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);

    auto peerIds = addPeers(pm, 4);

    DiscoveryConfig config;
    config.maxConcurrentQueries = 10;
    DiscoveryProtocol discovery(pm, blockStore, config);

    std::string targetBlockId = "deadbeef01234567";

    // Peer 0: filter WITH target → should be queried
    {
        BloomFilter filter(100, 0.01, 7);
        filter.add(targetBlockId);
        discovery.setPeerBloomFilter(peerIds[0], std::move(filter));
    }
    // Peer 1: filter WITHOUT target → should NOT be queried
    {
        BloomFilter filter(100, 0.01, 7);
        filter.add("other-block-id");
        discovery.setPeerBloomFilter(peerIds[1], std::move(filter));
    }
    // Peer 2: NO filter → should be queried (conservative)
    // (don't set a Bloom filter)

    // Peer 3: filter WITH target → should be queried
    {
        BloomFilter filter(100, 0.01, 7);
        filter.add(targetBlockId);
        filter.add("extra-block");
        discovery.setPeerBloomFilter(peerIds[3], std::move(filter));
    }

    // All peers respond positively
    for (const auto& peerId : peerIds) {
        discovery.setPeerQueryResponse(peerId, targetBlockId, true);
    }

    auto result = discovery.discoverBlock(targetBlockId);

    ASSERT_TRUE(result.found);
    // Should have queried peers 0, 2, 3 (not peer 1)
    EXPECT_EQ(result.queriedPeers, 3);
    EXPECT_EQ(result.locations.size(), 3u);

    std::set<std::string> queriedIds;
    for (const auto& loc : result.locations) {
        queriedIds.insert(loc.nodeId);
    }

    EXPECT_TRUE(queriedIds.count(peerIds[0]) > 0);  // filter has target
    EXPECT_FALSE(queriedIds.count(peerIds[1]) > 0);  // filter rejects
    EXPECT_TRUE(queriedIds.count(peerIds[2]) > 0);   // no filter (conservative)
    EXPECT_TRUE(queriedIds.count(peerIds[3]) > 0);   // filter has target
}
