#pragma once

#include <brightchain/gossip/block_announcement.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

namespace brightchain {
class Member;
class Ecies;
class EcKeyPair;
} // namespace brightchain

namespace brightchain::gossip {

// Forward declaration — full definition lives in gossip_engine.hpp.
class GossipEngine;

/// Information about a connected or known peer.
struct PeerInfo {
    std::string nodeId;
    std::string address;
    uint16_t httpPort = 0;
    uint16_t wsPort = 0;
    std::string lastSeen; // ISO 8601
    std::vector<std::string> capabilities;
    bool connected = false;
    double latencyMs = 0.0;
    std::vector<uint8_t> publicKey; // 33-byte compressed secp256k1

    bool operator==(const PeerInfo& other) const = default;
};

/// Cached pool discovery entry: pool metadata + hosting node.
struct PoolCacheEntry {
    PoolAnnouncementMetadata metadata;
    std::string hostNodeId;
};

/// Manages peer discovery, WebSocket connections, ECIES authentication,
/// heartbeats, and pool discovery caching.
///
/// Thread safety: the peer registry and pool cache are protected by
/// std::shared_mutex for concurrent reads with exclusive writes.
class PeerManager {
public:
    /// Construct a PeerManager.
    /// @param ioc  The Boost.Asio io_context for async operations.
    /// @param localNode  The local node's Member identity (used for ECIES auth).
    PeerManager(boost::asio::io_context& ioc, const Member& localNode);

    ~PeerManager();

    // ── Discovery ──────────────────────────────────────────────────────

    /// Add bootstrap node addresses (host:port strings) for initial discovery.
    void addBootstrapNodes(const std::vector<std::string>& addresses);

    /// Start the peer discovery process (connects to bootstrap nodes).
    void startDiscovery();

    // ── Connection management ──────────────────────────────────────────

    /// Initiate a WebSocket connection to a peer at the given address and port.
    void connectToPeer(const std::string& address, uint16_t wsPort);

    /// Disconnect from a peer by nodeId.
    void disconnectPeer(const std::string& nodeId);

    // ── Peer registry queries ──────────────────────────────────────────

    /// Return all currently connected peers.
    [[nodiscard]] std::vector<PeerInfo> getConnectedPeers() const;

    /// Look up a specific peer by nodeId.
    [[nodiscard]] std::optional<PeerInfo> getPeer(const std::string& nodeId) const;

    /// Return the nodeIds of all connected peers.
    [[nodiscard]] std::vector<std::string> getConnectedPeerIds() const;

    // ── Messaging ──────────────────────────────────────────────────────

    /// Send a JSON message string to a specific peer.
    void sendToPeer(const std::string& nodeId, const std::string& message);

    /// Broadcast a JSON message string to a set of peers.
    void broadcastToPeers(const std::vector<std::string>& peerIds,
                          const std::string& message);

    // ── Pool discovery cache ───────────────────────────────────────────

    /// Update the pool cache with metadata from a pool_announce.
    void updatePoolCache(const std::string& poolId,
                         const PoolAnnouncementMetadata& meta,
                         const std::string& hostNodeId);

    /// Remove a pool from the cache (on pool_deleted).
    void removePoolFromCache(const std::string& poolId);

    /// Get all cached pool entries (for introspection).
    [[nodiscard]] std::unordered_map<std::string, PoolCacheEntry> getPoolCache() const;

    // ── Lifecycle ──────────────────────────────────────────────────────

    /// Start heartbeat timer and other periodic tasks.
    void start();

    /// Stop all timers and close connections.
    void stop();

    // ── Helpers exposed for testing ────────────────────────────────────

    /// Compute reconnection delay in seconds for attempt n (0-based).
    /// Formula: min(1 * 2^n, 60)
    [[nodiscard]] static int calculateReconnectDelay(int attempt);

    /// Perform ECIES challenge/response authentication.
    /// Generates a random challenge, encrypts with peer's public key,
    /// and expects the peer to decrypt and return the original bytes.
    /// Returns true if the peer proves key ownership.
    [[nodiscard]] bool authenticateConnection(
        const std::vector<uint8_t>& peerPublicKey,
        const std::function<std::vector<uint8_t>(const std::vector<uint8_t>& encryptedChallenge)>& peerDecryptFn) const;

    /// Add a peer directly to the registry (for testing).
    void addPeer(const PeerInfo& peer);

    /// Get all messages sent via sendToPeer (for testing).
    /// Returns vector of (peerId, message) pairs.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> getSentMessages() const;

    /// Clear the sent messages log (for testing).
    void clearSentMessages();

private:
    // WebSocket session type alias
    using WsStream = boost::beast::websocket::stream<boost::beast::tcp_stream>;

    /// Internal session state for a connected peer.
    struct PeerSession {
        PeerInfo info;
        std::shared_ptr<WsStream> ws;
        std::chrono::steady_clock::time_point lastPongReceived;
        int reconnectAttempts = 0;
    };

    void scheduleHeartbeat();
    void sendPing(PeerSession& session);
    void handlePongTimeout(const std::string& nodeId);
    void reconnectWithBackoff(const std::string& nodeId);
    void scheduleReconnect(const std::string& nodeId, int attempt);

    boost::asio::io_context& ioc_;
    // Store localNode as a pointer to avoid requiring full Member definition
    // in the header. The caller must ensure the Member outlives PeerManager.
    const Member& localNode_;

    mutable std::shared_mutex peerMutex_;
    std::unordered_map<std::string, PeerSession> sessions_;

    mutable std::shared_mutex poolCacheMutex_;
    std::unordered_map<std::string, PoolCacheEntry> poolCache_;

    std::vector<std::string> bootstrapAddresses_;

    mutable std::shared_mutex sentMessagesMutex_;
    std::vector<std::pair<std::string, std::string>> sentMessages_;

    std::unique_ptr<boost::asio::steady_timer> heartbeatTimer_;
    int heartbeatTimeoutMs_ = 30000; // 30 seconds default
    int heartbeatIntervalMs_ = 10000; // 10 seconds default
    bool running_ = false;
};

} // namespace brightchain::gossip
