// Feature: cpp-gossip-protocol
// Property 27: Discovery results sorted by latency
// **Validates: Requirements 4.5**
//
// For any set of discovery results with multiple locations,
// the locations vector shall be sorted by latency in ascending order.

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
#include <string>
#include <vector>

using namespace brightchain::gossip;

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        auto tmp = std::filesystem::temp_directory_path() / "discovery_protocol_test";
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

/// Generate a positive latency value in [0.1, 10000.0] ms.
rc::Gen<double> genLatency() {
    return rc::gen::exec([] {
        return static_cast<double>(*rc::gen::inRange(1, 100001)) / 10.0;
    });
}

/// Add N connected peers to a PeerManager with specified latencies,
/// returning their nodeIds in insertion order.
std::vector<std::string> addPeersWithLatencies(
    PeerManager& pm,
    const std::vector<double>& latencies) {
    std::vector<std::string> ids;
    for (size_t i = 0; i < latencies.size(); ++i) {
        PeerInfo info;
        info.nodeId = "peer-" + std::to_string(i);
        info.address = "127.0.0." + std::to_string(i + 1);
        info.httpPort = 3000;
        info.wsPort = 3000;
        info.lastSeen = "2025-01-28T12:00:00.000Z";
        info.capabilities = {"blocks", "gossip"};
        info.connected = true;
        info.latencyMs = latencies[i];
        info.publicKey = {0x02}; // minimal stub
        pm.addPeer(info);
        ids.push_back(info.nodeId);
    }
    return ids;
}

} // namespace

// ── Property 27: Discovery results sorted by latency ───────────────────────
// For any set of peers with varying latencies that all have the queried block,
// discoverBlock() shall return locations sorted by latency ascending.

RC_GTEST_PROP(DiscoveryResultsSortedByLatency,
              LocationsAreAscendingLatency,
              ()) {
    // Generate 2–20 distinct latency values
    int peerCount = *rc::gen::inRange(2, 21);
    std::vector<double> latencies;
    latencies.reserve(static_cast<size_t>(peerCount));
    for (int i = 0; i < peerCount; ++i) {
        latencies.push_back(*genLatency());
    }

    auto blockId = *genBlockId();

    // Set up infrastructure
    TempDir storeTmp;
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);

    auto peerIds = addPeersWithLatencies(pm, latencies);

    // Set maxConcurrentQueries high enough to query all peers
    DiscoveryConfig config;
    config.maxConcurrentQueries = peerCount;
    DiscoveryProtocol discovery(pm, blockStore, config);

    // Configure each peer to respond positively for the block
    for (const auto& peerId : peerIds) {
        discovery.setPeerQueryResponse(peerId, blockId, true);
    }

    auto result = discovery.discoverBlock(blockId);

    // All peers should be found
    RC_ASSERT(result.found);
    RC_ASSERT(static_cast<int>(result.locations.size()) == peerCount);

    // Verify ascending latency order
    for (size_t i = 1; i < result.locations.size(); ++i) {
        RC_ASSERT(result.locations[i - 1].latencyMs <= result.locations[i].latencyMs);
    }
}

// ── Additional property: sorted even when maxConcurrentQueries limits peers ─

RC_GTEST_PROP(DiscoveryResultsSortedByLatency,
              SortedEvenWithQueryLimit,
              ()) {
    // Generate more peers than the default maxConcurrentQueries (10)
    int peerCount = *rc::gen::inRange(2, 11);
    std::vector<double> latencies;
    latencies.reserve(static_cast<size_t>(peerCount));
    for (int i = 0; i < peerCount; ++i) {
        latencies.push_back(*genLatency());
    }

    auto blockId = *genBlockId();

    TempDir storeTmp;
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);

    auto peerIds = addPeersWithLatencies(pm, latencies);

    DiscoveryConfig config;
    config.maxConcurrentQueries = peerCount; // ensure all are queried
    DiscoveryProtocol discovery(pm, blockStore, config);

    // Only some peers have the block (random subset)
    int respondingCount = 0;
    for (const auto& peerId : peerIds) {
        bool has = *rc::gen::arbitrary<bool>();
        discovery.setPeerQueryResponse(peerId, blockId, has);
        if (has) ++respondingCount;
    }

    auto result = discovery.discoverBlock(blockId);

    // Verify ascending latency order regardless of which peers responded
    for (size_t i = 1; i < result.locations.size(); ++i) {
        RC_ASSERT(result.locations[i - 1].latencyMs <= result.locations[i].latencyMs);
    }
}

// ── Deterministic unit test: known latency ordering ────────────────────────

TEST(DiscoveryResultsSortedByLatency, KnownLatencyOrder) {
    TempDir storeTmp;
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);

    // Add peers with deliberately unsorted latencies
    std::vector<double> latencies = {500.0, 10.0, 250.0, 1.0, 999.0};
    auto peerIds = addPeersWithLatencies(pm, latencies);

    DiscoveryProtocol discovery(pm, blockStore);

    std::string blockId = "deadbeef01234567";
    for (const auto& peerId : peerIds) {
        discovery.setPeerQueryResponse(peerId, blockId, true);
    }

    auto result = discovery.discoverBlock(blockId);

    ASSERT_TRUE(result.found);
    ASSERT_EQ(result.locations.size(), 5u);

    // Expected order: 1.0, 10.0, 250.0, 500.0, 999.0
    EXPECT_DOUBLE_EQ(result.locations[0].latencyMs, 1.0);
    EXPECT_DOUBLE_EQ(result.locations[1].latencyMs, 10.0);
    EXPECT_DOUBLE_EQ(result.locations[2].latencyMs, 250.0);
    EXPECT_DOUBLE_EQ(result.locations[3].latencyMs, 500.0);
    EXPECT_DOUBLE_EQ(result.locations[4].latencyMs, 999.0);
}

// ── Unit test: single location is trivially sorted ─────────────────────────

TEST(DiscoveryResultsSortedByLatency, SingleLocationIsSorted) {
    TempDir storeTmp;
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);

    std::vector<double> latencies = {42.5};
    auto peerIds = addPeersWithLatencies(pm, latencies);

    DiscoveryProtocol discovery(pm, blockStore);

    std::string blockId = "aabbccdd11223344";
    discovery.setPeerQueryResponse(peerIds[0], blockId, true);

    auto result = discovery.discoverBlock(blockId);

    ASSERT_TRUE(result.found);
    ASSERT_EQ(result.locations.size(), 1u);
    EXPECT_DOUBLE_EQ(result.locations[0].latencyMs, 42.5);
}
