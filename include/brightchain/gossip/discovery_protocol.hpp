#pragma once

#include <brightchain/gossip/block_announcement.hpp>
#include <brightchain/gossip/bloom_filter.hpp>

#include <chrono>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace brightchain {
class DiskBlockStore;
} // namespace brightchain

namespace brightchain::gossip {

class PeerManager;

/// Configuration for the block discovery protocol.
struct DiscoveryConfig {
    int queryTimeoutMs = 5000;
    int maxConcurrentQueries = 10;
    int cacheTtlMs = 60000;
    double bloomFilterFalsePositiveRate = 0.01;
    int bloomFilterHashCount = 7;

    bool operator==(const DiscoveryConfig& other) const = default;
};

/// A single location where a block was found, with latency info.
struct LocationRecord {
    std::string nodeId;
    double latencyMs = 0.0;

    bool operator==(const LocationRecord& other) const = default;
};

/// Result of a block discovery query.
struct DiscoveryResult {
    std::string blockId;
    bool found = false;
    std::vector<LocationRecord> locations; // sorted by latency ascending
    int queriedPeers = 0;
    int durationMs = 0;
    std::optional<std::string> poolId;

    bool operator==(const DiscoveryResult& other) const = default;
};

/// Locates blocks across the network using Bloom filter pre-checks,
/// concurrent query limiting, result caching, and latency-based sorting.
///
/// Thread safety: the result cache is protected by std::shared_mutex.
class DiscoveryProtocol {
public:
    /// Result of a CBL metadata search across peers.
    struct CblSearchResult {
        std::vector<CblIndexEntry> hits;
        int queriedPeers = 0;
        int durationMs = 0;

        bool operator==(const CblSearchResult& other) const = default;
    };

    /// Construct a DiscoveryProtocol.
    /// @param peerManager  Reference to the peer manager for querying peers.
    /// @param blockStore   Reference to the local block store.
    /// @param config       Discovery configuration.
    DiscoveryProtocol(PeerManager& peerManager, DiskBlockStore& blockStore,
                      DiscoveryConfig config = {});

    /// Discover a block across the network.
    /// Flow: cache check → Bloom filter pre-check → concurrent peer queries.
    /// Results are sorted by peer latency ascending.
    DiscoveryResult discoverBlock(const std::string& blockId,
                                  std::optional<std::string> poolId = {});

    /// Return cached locations for a block, if present and not expired.
    [[nodiscard]] std::optional<std::vector<LocationRecord>>
    getCachedLocations(const std::string& blockId) const;

    /// Remove a specific block from the cache.
    void clearCache(const std::string& blockId);

    /// Remove all entries from the cache.
    void clearAllCache();

    /// Build a Bloom filter from the local DiskBlockStore contents.
    [[nodiscard]] BloomFilter getLocalBloomFilter() const;

    /// Get the current configuration.
    [[nodiscard]] const DiscoveryConfig& getConfig() const;

    /// Search CBL metadata across peers by file name, MIME type, and tags.
    CblSearchResult searchCblMetadata(const std::string& fileName = "",
                                       const std::string& mimeType = "",
                                       const std::vector<std::string>& tags = {},
                                       std::optional<std::string> poolId = {});

    // ── Testing helpers ────────────────────────────────────────────────

    /// Inject a Bloom filter for a specific peer (for testing).
    void setPeerBloomFilter(const std::string& nodeId, BloomFilter filter);

    /// Inject a query response for a specific peer (for testing).
    /// When the peer is queried for a blockId, it will respond with `has`.
    void setPeerQueryResponse(const std::string& nodeId,
                              const std::string& blockId, bool has);

    /// Inject CBL index entries for a specific peer (for testing).
    void setPeerCblEntries(const std::string& nodeId,
                           const std::vector<CblIndexEntry>& entries);

    /// Get the number of cached entries (for testing).
    [[nodiscard]] size_t getCacheSize() const;

private:
    struct CacheEntry {
        std::vector<LocationRecord> locations;
        std::chrono::steady_clock::time_point expiresAt;
    };

    /// Check if a cache entry is still valid.
    [[nodiscard]] bool isCacheValid(const CacheEntry& entry) const;

    /// Store locations in the cache with the configured TTL.
    void cacheLocations(const std::string& blockId,
                        const std::vector<LocationRecord>& locations);

    /// Sort locations by latency ascending.
    static void sortByLatency(std::vector<LocationRecord>& locations);

    PeerManager& peerManager_;
    DiskBlockStore& blockStore_;
    DiscoveryConfig config_;

    mutable std::shared_mutex cacheMutex_;
    std::unordered_map<std::string, CacheEntry> cache_;

    // Testing support: per-peer Bloom filters and query responses
    mutable std::shared_mutex testDataMutex_;
    std::unordered_map<std::string, BloomFilter> peerBloomFilters_;
    // peerQueryResponses_[nodeId][blockId] = has
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> peerQueryResponses_;
    // peerCblEntries_[nodeId] = entries
    std::unordered_map<std::string, std::vector<CblIndexEntry>> peerCblEntries_;
};

} // namespace brightchain::gossip
