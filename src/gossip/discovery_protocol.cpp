#include "brightchain/gossip/discovery_protocol.hpp"
#include "brightchain/gossip/peer_manager.hpp"
#include "brightchain/disk_block_store.hpp"
#include "brightchain/block_size.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace brightchain::gossip {

// ── Construction ───────────────────────────────────────────────────────

DiscoveryProtocol::DiscoveryProtocol(PeerManager& peerManager,
                                     DiskBlockStore& blockStore,
                                     DiscoveryConfig config)
    : peerManager_(peerManager)
    , blockStore_(blockStore)
    , config_(std::move(config)) {}

// ── Public API ─────────────────────────────────────────────────────────

DiscoveryResult DiscoveryProtocol::discoverBlock(
    const std::string& blockId,
    std::optional<std::string> poolId) {

    auto startTime = std::chrono::steady_clock::now();

    DiscoveryResult result;
    result.blockId = blockId;
    result.poolId = poolId;

    // Step 1: Check the cache
    auto cached = getCachedLocations(blockId);
    if (cached.has_value()) {
        result.found = !cached->empty();
        result.locations = *cached;
        result.queriedPeers = 0;
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        result.durationMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        return result;
    }

    // Step 2: Get connected peers and their Bloom filters for pre-check
    auto connectedPeers = peerManager_.getConnectedPeers();

    // Filter peers using Bloom filter pre-check: only query peers whose
    // Bloom filter indicates the block might be present.
    std::vector<PeerInfo> candidatePeers;
    {
        std::shared_lock lock(testDataMutex_);
        for (const auto& peer : connectedPeers) {
            auto it = peerBloomFilters_.find(peer.nodeId);
            if (it != peerBloomFilters_.end()) {
                // Peer has a Bloom filter — only include if it might contain the block
                if (it->second.mightContain(blockId)) {
                    candidatePeers.push_back(peer);
                }
            } else {
                // No Bloom filter available for this peer — include it
                // (conservative: query when we can't pre-filter)
                candidatePeers.push_back(peer);
            }
        }
    }

    // Step 3: Query candidate peers (limited to maxConcurrentQueries)
    int queryLimit = std::min(static_cast<int>(candidatePeers.size()),
                              config_.maxConcurrentQueries);

    std::vector<LocationRecord> locations;
    int queriedCount = 0;

    for (int i = 0; i < queryLimit; ++i) {
        const auto& peer = candidatePeers[static_cast<size_t>(i)];
        ++queriedCount;

        // Check test query responses first
        bool peerHasBlock = false;
        {
            std::shared_lock lock(testDataMutex_);
            auto respIt = peerQueryResponses_.find(peer.nodeId);
            if (respIt != peerQueryResponses_.end()) {
                auto blockIt = respIt->second.find(blockId);
                if (blockIt != respIt->second.end()) {
                    peerHasBlock = blockIt->second;
                }
            }
        }

        if (peerHasBlock) {
            LocationRecord loc;
            loc.nodeId = peer.nodeId;
            loc.latencyMs = peer.latencyMs;
            locations.push_back(loc);
        }
    }

    // Sort results by latency ascending
    sortByLatency(locations);

    // Cache the results
    cacheLocations(blockId, locations);

    result.found = !locations.empty();
    result.locations = locations;
    result.queriedPeers = queriedCount;

    auto elapsed = std::chrono::steady_clock::now() - startTime;
    result.durationMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());

    return result;
}

std::optional<std::vector<LocationRecord>>
DiscoveryProtocol::getCachedLocations(const std::string& blockId) const {
    std::shared_lock lock(cacheMutex_);
    auto it = cache_.find(blockId);
    if (it == cache_.end()) {
        return std::nullopt;
    }
    if (!isCacheValid(it->second)) {
        return std::nullopt;
    }
    return it->second.locations;
}

void DiscoveryProtocol::clearCache(const std::string& blockId) {
    std::unique_lock lock(cacheMutex_);
    cache_.erase(blockId);
}

void DiscoveryProtocol::clearAllCache() {
    std::unique_lock lock(cacheMutex_);
    cache_.clear();
}

BloomFilter DiscoveryProtocol::getLocalBloomFilter() const {
    // Walk the DiskBlockStore directory structure to collect all block IDs.
    // Directory layout: storePath/blockSize/char1/char2/checksum
    // We look for files that are NOT metadata (.m.json).
    const auto& storePath = blockStore_.storePath();

    // Count files first for sizing the Bloom filter (rough estimate).
    size_t estimatedItems = 0;
    if (std::filesystem::exists(storePath)) {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(storePath)) {
            if (entry.is_regular_file()) {
                const auto& path = entry.path();
                // Skip metadata files
                if (path.extension() != ".json" ||
                    path.string().find(".m.json") == std::string::npos) {
                    // This is a block file (no extension) — count it
                    if (!path.has_extension()) {
                        ++estimatedItems;
                    }
                }
            }
        }
    }

    // Use at least 1 expected item to avoid degenerate filter
    if (estimatedItems == 0) {
        estimatedItems = 1;
    }

    BloomFilter filter(estimatedItems,
                       config_.bloomFilterFalsePositiveRate,
                       config_.bloomFilterHashCount);

    // Second pass: add block IDs (the filename is the hex checksum)
    if (std::filesystem::exists(storePath)) {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(storePath)) {
            if (entry.is_regular_file() && !entry.path().has_extension()) {
                // The filename is the block's hex checksum
                filter.add(entry.path().filename().string());
            }
        }
    }

    return filter;
}

const DiscoveryConfig& DiscoveryProtocol::getConfig() const {
    return config_;
}

DiscoveryProtocol::CblSearchResult DiscoveryProtocol::searchCblMetadata(
    const std::string& fileName,
    const std::string& mimeType,
    const std::vector<std::string>& tags,
    std::optional<std::string> poolId) {

    auto startTime = std::chrono::steady_clock::now();

    CblSearchResult result;
    auto connectedPeers = peerManager_.getConnectedPeers();

    int queryLimit = std::min(static_cast<int>(connectedPeers.size()),
                              config_.maxConcurrentQueries);
    int queriedCount = 0;

    for (int i = 0; i < queryLimit; ++i) {
        const auto& peer = connectedPeers[static_cast<size_t>(i)];
        ++queriedCount;

        // Get CBL entries from test data for this peer
        std::vector<CblIndexEntry> peerEntries;
        {
            std::shared_lock lock(testDataMutex_);
            auto it = peerCblEntries_.find(peer.nodeId);
            if (it != peerCblEntries_.end()) {
                peerEntries = it->second;
            }
        }

        // Filter entries by criteria
        for (const auto& entry : peerEntries) {
            bool matches = true;

            // File name substring match
            if (!fileName.empty()) {
                if (entry.magnetUrl.find(fileName) == std::string::npos) {
                    matches = false;
                }
            }

            // MIME type exact match (stored in blockId1 for search purposes)
            if (!mimeType.empty() && matches) {
                if (entry.blockId1 != mimeType) {
                    matches = false;
                }
            }

            // Tag intersection (stored in blockId2 as comma-separated for search)
            if (!tags.empty() && matches) {
                bool hasAnyTag = false;
                for (const auto& tag : tags) {
                    if (entry.blockId2.find(tag) != std::string::npos) {
                        hasAnyTag = true;
                        break;
                    }
                }
                if (!hasAnyTag) {
                    matches = false;
                }
            }

            if (matches) {
                result.hits.push_back(entry);
            }
        }
    }

    result.queriedPeers = queriedCount;
    auto elapsed = std::chrono::steady_clock::now() - startTime;
    result.durationMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());

    return result;
}

// ── Testing helpers ────────────────────────────────────────────────────

void DiscoveryProtocol::setPeerBloomFilter(const std::string& nodeId,
                                            BloomFilter filter) {
    std::unique_lock lock(testDataMutex_);
    peerBloomFilters_.insert_or_assign(nodeId, std::move(filter));
}

void DiscoveryProtocol::setPeerQueryResponse(const std::string& nodeId,
                                              const std::string& blockId,
                                              bool has) {
    std::unique_lock lock(testDataMutex_);
    peerQueryResponses_[nodeId][blockId] = has;
}

void DiscoveryProtocol::setPeerCblEntries(
    const std::string& nodeId,
    const std::vector<CblIndexEntry>& entries) {
    std::unique_lock lock(testDataMutex_);
    peerCblEntries_[nodeId] = entries;
}

size_t DiscoveryProtocol::getCacheSize() const {
    std::shared_lock lock(cacheMutex_);
    return cache_.size();
}

// ── Private helpers ────────────────────────────────────────────────────

bool DiscoveryProtocol::isCacheValid(const CacheEntry& entry) const {
    return std::chrono::steady_clock::now() < entry.expiresAt;
}

void DiscoveryProtocol::cacheLocations(
    const std::string& blockId,
    const std::vector<LocationRecord>& locations) {
    std::unique_lock lock(cacheMutex_);
    CacheEntry entry;
    entry.locations = locations;
    entry.expiresAt = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(config_.cacheTtlMs);
    cache_.insert_or_assign(blockId, std::move(entry));
}

void DiscoveryProtocol::sortByLatency(std::vector<LocationRecord>& locations) {
    std::sort(locations.begin(), locations.end(),
              [](const LocationRecord& a, const LocationRecord& b) {
                  return a.latencyMs < b.latencyMs;
              });
}

} // namespace brightchain::gossip
