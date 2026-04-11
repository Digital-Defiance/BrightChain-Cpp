#pragma once

#include <brightchain/member.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

namespace brightchain::gossip {

class GossipEngine;
class PeerManager;

/// Access tier for WebSocket events.
enum class EventAccessTier : uint8_t {
    Admin,       // peer:connected, peer:disconnected, storage:alert
    PoolScoped,  // pool:changed, pool:created, pool:deleted
    MemberScoped // energy:updated
};

/// A WebSocket event to be delivered to subscribed clients.
struct WebSocketEvent {
    std::string event;
    std::string data; // JSON string
    EventAccessTier accessTier = EventAccessTier::Admin;
    std::optional<std::string> targetPoolId;   // for PoolScoped events
    std::optional<std::string> targetMemberId; // for MemberScoped events
};

/// JWT claims extracted from a client token.
struct JwtClaims {
    std::string memberId;
    MemberType memberType = MemberType::Anonymous;
    std::vector<std::string> permissions; // e.g. "pool:<id>:read"
    std::chrono::system_clock::time_point expiresAt;
};

/// Represents a connected client WebSocket session.
struct ClientSession {
    std::string sessionId;
    JwtClaims claims;
    std::unordered_set<std::string> subscribedEvents;
    std::chrono::steady_clock::time_point lastPongReceived;
    bool authenticated = false;
    bool tokenExpiringSent = false; // true once auth:token_expiring event sent
};

/// Determines the access tier for a given event type string.
EventAccessTier getAccessTierForEvent(const std::string& eventType);

/// Checks whether a client with the given claims may receive an event.
/// @param claims  The client's JWT claims.
/// @param event   The event to check.
/// @return true if the client is authorized to receive the event.
bool isClientAuthorizedForEvent(const JwtClaims& claims,
                                const WebSocketEvent& event);

/// Manages WebSocket connections for both node-to-node (ECIES auth) and
/// client (JWT auth) endpoints, with event subscription and access filtering.
///
/// Endpoints:
///   /ws/node/:nodeId  — ECIES challenge/response authentication
///   /ws/client?token=<jwt> — JWT authentication
///
/// Thread safety: client sessions are protected by std::shared_mutex.
class WebSocketServer {
public:
    /// Construct a WebSocketServer.
    WebSocketServer(boost::asio::io_context& ioc,
                    GossipEngine& engine,
                    PeerManager& peerManager,
                    const Member& localNode,
                    const std::string& jwtSecret);

    ~WebSocketServer();

    /// Start listening on the given address and port.
    void start(const std::string& address, uint16_t port);

    /// Stop the server and close all connections.
    void stop();

    /// Broadcast an event to all authorized, subscribed clients.
    void broadcastEvent(const WebSocketEvent& event);

    // ── JWT helpers (public for testing) ───────────────────────────────

    /// Validate a JWT token string and extract claims.
    /// @return JwtClaims on success, std::nullopt on failure.
    [[nodiscard]] std::optional<JwtClaims> validateJwt(const std::string& token) const;

    /// Create a signed JWT token (for testing).
    [[nodiscard]] std::string createJwt(const JwtClaims& claims) const;

    // ── Session management (public for testing) ────────────────────────

    /// Add a client session directly (for testing).
    void addClientSession(const ClientSession& session);

    /// Remove a client session by sessionId.
    void removeClientSession(const std::string& sessionId);

    /// Get a client session by sessionId.
    [[nodiscard]] std::optional<ClientSession> getClientSession(
        const std::string& sessionId) const;

    /// Subscribe a client to an event type.
    void subscribeClient(const std::string& sessionId,
                         const std::string& eventType);

    /// Unsubscribe a client from an event type.
    void unsubscribeClient(const std::string& sessionId,
                           const std::string& eventType);

    /// Get all client sessions (for testing/inspection).
    [[nodiscard]] std::vector<ClientSession> getClientSessions() const;

    // ── Configuration ──────────────────────────────────────────────────

    void setHeartbeatIntervalMs(int ms) { heartbeatIntervalMs_ = ms; }
    void setHeartbeatTimeoutMs(int ms) { heartbeatTimeoutMs_ = ms; }
    void setJwtGracePeriodMs(int ms) { jwtGracePeriodMs_ = ms; }

    [[nodiscard]] int heartbeatIntervalMs() const { return heartbeatIntervalMs_; }
    [[nodiscard]] int heartbeatTimeoutMs() const { return heartbeatTimeoutMs_; }
    [[nodiscard]] int jwtGracePeriodMs() const { return jwtGracePeriodMs_; }

    /// WebSocket close code for JWT expiry.
    static constexpr uint16_t kCloseCodeJwtExpired = 4002;

private:
    // WebSocket session type alias
    using WsStream = boost::beast::websocket::stream<boost::beast::tcp_stream>;

    void doAccept();
    void handleNodeConnection(std::shared_ptr<WsStream> ws,
                              const std::string& nodeId);
    void handleClientConnection(std::shared_ptr<WsStream> ws,
                                const std::string& token);
    bool authenticateEcies(std::shared_ptr<WsStream> ws,
                           const std::vector<uint8_t>& peerPublicKey);
    void scheduleHeartbeat();
    void scheduleJwtExpiryCheck();
    void startNodeReadLoop(std::shared_ptr<WsStream> ws,
                           const std::string& nodeId);
    void startClientReadLoop(std::shared_ptr<WsStream> ws,
                             const std::string& sessionId);
    void sendToClient(const std::string& sessionId, const std::string& message);

    boost::asio::io_context& ioc_;
    GossipEngine& engine_;
    PeerManager& peerManager_;
    const Member& localNode_;
    std::string jwtSecret_;

    std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor_;

    mutable std::shared_mutex clientMutex_;
    std::unordered_map<std::string, ClientSession> clientSessions_;

    // Track live WS streams per client session for sending events.
    mutable std::shared_mutex wsStreamMutex_;
    std::unordered_map<std::string, std::shared_ptr<WsStream>> clientWsStreams_;

    std::unique_ptr<boost::asio::steady_timer> heartbeatTimer_;
    std::unique_ptr<boost::asio::steady_timer> jwtExpiryTimer_;

    int heartbeatIntervalMs_ = 10000;  // 10 seconds
    int heartbeatTimeoutMs_ = 30000;   // 30 seconds
    int jwtGracePeriodMs_ = 30000;     // 30 seconds grace before close

    bool running_ = false;
};

} // namespace brightchain::gossip
