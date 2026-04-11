#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/ecies.hpp>
#include <brightchain/member.hpp>

#include <boost/asio/strand.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace brightchain::gossip {

// ── Construction / Destruction ─────────────────────────────────────────────

PeerManager::PeerManager(boost::asio::io_context& ioc, const Member& localNode)
    : ioc_(ioc), localNode_(localNode) {}

PeerManager::~PeerManager() {
    stop();
}

// ── Discovery ──────────────────────────────────────────────────────────────

void PeerManager::addBootstrapNodes(const std::vector<std::string>& addresses) {
    bootstrapAddresses_.insert(bootstrapAddresses_.end(),
                               addresses.begin(), addresses.end());
}

void PeerManager::startDiscovery() {
    // Connect to each bootstrap address.
    for (const auto& addr : bootstrapAddresses_) {
        // Parse "host:port" format.
        auto colonPos = addr.rfind(':');
        if (colonPos == std::string::npos) {
            continue; // malformed address
        }
        std::string host = addr.substr(0, colonPos);
        uint16_t port = 0;
        try {
            port = static_cast<uint16_t>(std::stoi(addr.substr(colonPos + 1)));
        } catch (...) {
            continue; // invalid port
        }
        connectToPeer(host, port);
    }
}

// ── Connection management ──────────────────────────────────────────────────

void PeerManager::connectToPeer(const std::string& address, uint16_t wsPort) {
    // Resolve and connect asynchronously via Boost.Beast WebSocket.
    auto ws = std::make_shared<WsStream>(boost::asio::make_strand(ioc_));

    boost::asio::ip::tcp::resolver resolver(ioc_);
    boost::system::error_code ec;
    auto results = resolver.resolve(address, std::to_string(wsPort), ec);
    if (ec) {
        return; // DNS resolution failed — will retry via discovery
    }

    auto& tcpStream = boost::beast::get_lowest_layer(*ws);
    tcpStream.connect(results, ec);
    if (ec) {
        return; // connection failed
    }

    // Perform WebSocket handshake to /ws/node/<localNodeId>
    std::string target = "/ws/node/" + localNode_.idHex();
    ws->handshake(address + ":" + std::to_string(wsPort), target, ec);
    if (ec) {
        return; // handshake failed
    }

    // The peer's nodeId will be determined after authentication.
    // For now, use address:port as a temporary key until the peer
    // identifies itself.
    std::string tempId = address + ":" + std::to_string(wsPort);

    PeerSession session;
    session.info.address = address;
    session.info.wsPort = wsPort;
    session.info.connected = true;
    session.info.nodeId = tempId;
    session.ws = ws;
    session.lastPongReceived = std::chrono::steady_clock::now();
    session.reconnectAttempts = 0;

    {
        std::unique_lock lock(peerMutex_);
        sessions_.emplace(tempId, std::move(session));
    }
}

void PeerManager::disconnectPeer(const std::string& nodeId) {
    std::unique_lock lock(peerMutex_);
    auto it = sessions_.find(nodeId);
    if (it == sessions_.end()) {
        return;
    }

    // Close the WebSocket gracefully.
    if (it->second.ws) {
        boost::system::error_code ec;
        it->second.ws->close(boost::beast::websocket::close_code::normal, ec);
        // Ignore close errors — best effort.
    }

    sessions_.erase(it);
}

// ── Peer registry queries ──────────────────────────────────────────────────

std::vector<PeerInfo> PeerManager::getConnectedPeers() const {
    std::shared_lock lock(peerMutex_);
    std::vector<PeerInfo> result;
    result.reserve(sessions_.size());
    for (const auto& [id, session] : sessions_) {
        if (session.info.connected) {
            result.push_back(session.info);
        }
    }
    return result;
}

std::optional<PeerInfo> PeerManager::getPeer(const std::string& nodeId) const {
    std::shared_lock lock(peerMutex_);
    auto it = sessions_.find(nodeId);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return it->second.info;
}

std::vector<std::string> PeerManager::getConnectedPeerIds() const {
    std::shared_lock lock(peerMutex_);
    std::vector<std::string> result;
    result.reserve(sessions_.size());
    for (const auto& [id, session] : sessions_) {
        if (session.info.connected) {
            result.push_back(id);
        }
    }
    return result;
}

// ── Messaging ──────────────────────────────────────────────────────────────

void PeerManager::sendToPeer(const std::string& nodeId,
                             const std::string& message) {
    // Record the message for testing/inspection.
    {
        std::unique_lock lock(sentMessagesMutex_);
        sentMessages_.emplace_back(nodeId, message);
    }

    std::shared_lock lock(peerMutex_);
    auto it = sessions_.find(nodeId);
    if (it == sessions_.end() || !it->second.info.connected || !it->second.ws) {
        return; // peer not found or not connected
    }

    boost::system::error_code ec;
    it->second.ws->write(boost::asio::buffer(message), ec);
    if (ec) {
        // Mark as disconnected — reconnection will be handled by heartbeat.
        // Note: we hold a shared_lock so we can't modify; schedule for later.
    }
}

void PeerManager::broadcastToPeers(const std::vector<std::string>& peerIds,
                                   const std::string& message) {
    for (const auto& peerId : peerIds) {
        sendToPeer(peerId, message);
    }
}

// ── Pool discovery cache ───────────────────────────────────────────────────

void PeerManager::updatePoolCache(const std::string& poolId,
                                  const PoolAnnouncementMetadata& meta,
                                  const std::string& hostNodeId) {
    std::unique_lock lock(poolCacheMutex_);
    poolCache_[poolId] = PoolCacheEntry{meta, hostNodeId};
}

void PeerManager::removePoolFromCache(const std::string& poolId) {
    std::unique_lock lock(poolCacheMutex_);
    poolCache_.erase(poolId);
}

std::unordered_map<std::string, PoolCacheEntry> PeerManager::getPoolCache() const {
    std::shared_lock lock(poolCacheMutex_);
    return poolCache_;
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

void PeerManager::start() {
    if (running_) return;
    running_ = true;
    heartbeatTimer_ = std::make_unique<boost::asio::steady_timer>(ioc_);
    scheduleHeartbeat();
}

void PeerManager::stop() {
    running_ = false;
    if (heartbeatTimer_) {
        heartbeatTimer_->cancel();
        heartbeatTimer_.reset();
    }

    // Close all WebSocket sessions.
    std::unique_lock lock(peerMutex_);
    for (auto& [id, session] : sessions_) {
        if (session.ws) {
            boost::system::error_code ec;
            session.ws->close(boost::beast::websocket::close_code::normal, ec);
        }
    }
    sessions_.clear();
}

// ── Heartbeat ──────────────────────────────────────────────────────────────

void PeerManager::scheduleHeartbeat() {
    if (!running_ || !heartbeatTimer_) return;

    heartbeatTimer_->expires_after(
        std::chrono::milliseconds(heartbeatIntervalMs_));
    heartbeatTimer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec || !running_) return;

        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> timedOut;

        {
            std::shared_lock lock(peerMutex_);
            for (const auto& [id, session] : sessions_) {
                if (!session.info.connected) continue;

                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - session.lastPongReceived).count();
                if (elapsed > heartbeatTimeoutMs_) {
                    timedOut.push_back(id);
                }
            }
        }

        // Handle timeouts outside the shared lock.
        for (const auto& nodeId : timedOut) {
            handlePongTimeout(nodeId);
        }

        // Send pings to all connected peers.
        {
            std::shared_lock lock(peerMutex_);
            for (auto& [id, session] : sessions_) {
                if (session.info.connected && session.ws) {
                    sendPing(session);
                }
            }
        }

        scheduleHeartbeat();
    });
}

void PeerManager::sendPing(PeerSession& session) {
    if (!session.ws) return;
    boost::system::error_code ec;
    session.ws->ping(boost::beast::websocket::ping_data{}, ec);
    // Ignore ping errors — pong timeout will handle disconnection.
}

void PeerManager::handlePongTimeout(const std::string& nodeId) {
    {
        std::unique_lock lock(peerMutex_);
        auto it = sessions_.find(nodeId);
        if (it == sessions_.end()) return;

        it->second.info.connected = false;
        if (it->second.ws) {
            boost::system::error_code ec;
            it->second.ws->close(boost::beast::websocket::close_code::normal, ec);
            it->second.ws.reset();
        }
    }

    // Schedule reconnection with backoff.
    reconnectWithBackoff(nodeId);
}

// ── Reconnection with exponential backoff ──────────────────────────────────

int PeerManager::calculateReconnectDelay(int attempt) {
    // Formula: min(1 * 2^n, 60) seconds where n is 0-based attempt number.
    if (attempt < 0) return 1;
    int delay = 1 << attempt; // 2^n (1 * 2^n since base is 1)
    if (delay > 60 || delay < 0) { // overflow guard
        return 60;
    }
    return std::min(delay, 60);
}

void PeerManager::reconnectWithBackoff(const std::string& nodeId) {
    int attempt = 0;
    {
        std::shared_lock lock(peerMutex_);
        auto it = sessions_.find(nodeId);
        if (it != sessions_.end()) {
            attempt = it->second.reconnectAttempts;
        }
    }
    scheduleReconnect(nodeId, attempt);
}

void PeerManager::scheduleReconnect(const std::string& nodeId, int attempt) {
    if (!running_) return;

    int delaySec = calculateReconnectDelay(attempt);

    auto timer = std::make_shared<boost::asio::steady_timer>(ioc_);
    timer->expires_after(std::chrono::seconds(delaySec));
    timer->async_wait([this, nodeId, attempt, timer](
                          const boost::system::error_code& ec) {
        if (ec || !running_) return;

        std::string address;
        uint16_t port = 0;
        {
            std::shared_lock lock(peerMutex_);
            auto it = sessions_.find(nodeId);
            if (it == sessions_.end()) return;
            address = it->second.info.address;
            port = it->second.info.wsPort;
        }

        if (address.empty() || port == 0) return;

        // Attempt reconnection.
        connectToPeer(address, port);

        // Check if reconnection succeeded.
        bool connected = false;
        {
            std::shared_lock lock(peerMutex_);
            auto it = sessions_.find(nodeId);
            if (it != sessions_.end()) {
                connected = it->second.info.connected;
            }
        }

        if (!connected) {
            // Increment attempt counter and schedule next retry.
            {
                std::unique_lock lock(peerMutex_);
                auto it = sessions_.find(nodeId);
                if (it != sessions_.end()) {
                    it->second.reconnectAttempts = attempt + 1;
                }
            }
            scheduleReconnect(nodeId, attempt + 1);
        } else {
            // Reset attempt counter on success.
            std::unique_lock lock(peerMutex_);
            auto it = sessions_.find(nodeId);
            if (it != sessions_.end()) {
                it->second.reconnectAttempts = 0;
            }
        }
    });
}

// ── ECIES challenge/response authentication ────────────────────────────────

bool PeerManager::authenticateConnection(
    const std::vector<uint8_t>& peerPublicKey,
    const std::function<std::vector<uint8_t>(
        const std::vector<uint8_t>& encryptedChallenge)>& peerDecryptFn) const {

    // Generate a random 32-byte challenge.
    std::vector<uint8_t> challenge(32);
    std::random_device rd;
    for (auto& byte : challenge) {
        byte = static_cast<uint8_t>(rd());
    }

    // Encrypt the challenge with the peer's public key using ECIES.
    std::vector<uint8_t> encrypted;
    try {
        encrypted = Ecies::encryptBasic(challenge, peerPublicKey);
    } catch (...) {
        return false; // encryption failed — invalid public key?
    }

    // Ask the peer to decrypt (via the provided callback).
    std::vector<uint8_t> decrypted;
    try {
        decrypted = peerDecryptFn(encrypted);
    } catch (...) {
        return false; // peer failed to decrypt
    }

    // Verify the peer returned the original challenge bytes.
    return decrypted == challenge;
}

// ── addPeer (testing helper) ───────────────────────────────────────────────

void PeerManager::addPeer(const PeerInfo& peer) {
    std::unique_lock lock(peerMutex_);
    PeerSession session;
    session.info = peer;
    session.lastPongReceived = std::chrono::steady_clock::now();
    sessions_[peer.nodeId] = std::move(session);
}

std::vector<std::pair<std::string, std::string>> PeerManager::getSentMessages() const {
    std::shared_lock lock(sentMessagesMutex_);
    return sentMessages_;
}

void PeerManager::clearSentMessages() {
    std::unique_lock lock(sentMessagesMutex_);
    sentMessages_.clear();
}

} // namespace brightchain::gossip
