#pragma once

#include <brightchain/member.hpp>
#include <brightchain/gossip/websocket_server.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

namespace brightchain {
class DiskBlockStore;
} // namespace brightchain

namespace brightchain::gossip {

class GossipEngine;
class PeerManager;
class DiscoveryProtocol;

/// HTTP request/response types used by the introspection API.
using HttpRequest = boost::beast::http::request<boost::beast::http::string_body>;
using HttpResponse = boost::beast::http::response<boost::beast::http::string_body>;

/// Route handler function type.
using RouteHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

/// Provides REST introspection endpoints for monitoring node state.
///
/// Endpoints:
///   GET  /api/introspection/status         — node status (any authenticated)
///   GET  /api/introspection/peers          — connected peers (Admin/System)
///   GET  /api/introspection/pools          — pools filtered by permissions
///   GET  /api/introspection/stats          — block store stats (Admin/System)
///   POST /api/introspection/discover-pools — discover pools from peers
///
/// All endpoints require a valid JWT bearer token.
class IntrospectionApi {
public:
    IntrospectionApi(GossipEngine& engine, PeerManager& peerManager,
                     DiskBlockStore& blockStore, DiscoveryProtocol& discovery,
                     const std::string& jwtSecret);

    /// Handle an incoming HTTP request and produce a response.
    /// Routes to the appropriate handler based on method + target path.
    void handleRequest(const HttpRequest& req, HttpResponse& res);

    /// Get the registered route table (for testing/inspection).
    [[nodiscard]] const std::unordered_map<std::string, RouteHandler>& getRoutes() const;

    /// Set the server start time (for uptime calculation). Defaults to construction time.
    void setStartTime(std::chrono::steady_clock::time_point t) { startTime_ = t; }

    /// Set the version string reported by /status.
    void setVersion(const std::string& version) { version_ = version; }

private:
    // ── Route handlers ─────────────────────────────────────────────────

    void handleStatus(const HttpRequest& req, HttpResponse& res);
    void handlePeers(const HttpRequest& req, HttpResponse& res);
    void handlePools(const HttpRequest& req, HttpResponse& res);
    void handleStats(const HttpRequest& req, HttpResponse& res);
    void handleDiscoverPools(const HttpRequest& req, HttpResponse& res);

    // ── Auth helpers ───────────────────────────────────────────────────

    /// Extract and validate the JWT bearer token from the Authorization header.
    /// @return JwtClaims on success, std::nullopt on failure.
    [[nodiscard]] std::optional<JwtClaims> extractAndValidateJwt(
        const HttpRequest& req) const;

    /// Check if the member type is Admin or System.
    [[nodiscard]] static bool isAdminOrSystem(MemberType type);

    /// Check if the member has read permission for a specific pool.
    [[nodiscard]] static bool hasPoolReadPermission(
        const JwtClaims& claims, const std::string& poolId);

    // ── Response helpers ───────────────────────────────────────────────

    static void sendSuccess(HttpResponse& res, const std::string& message,
                            const nlohmann::json& data);
    static void sendError(HttpResponse& res, boost::beast::http::status status,
                          const std::string& message,
                          const std::string& error = "");

    // ── Members ────────────────────────────────────────────────────────

    GossipEngine& engine_;
    PeerManager& peerManager_;
    DiskBlockStore& blockStore_;
    DiscoveryProtocol& discovery_;
    std::string jwtSecret_;
    std::string version_ = "1.0.0";
    std::chrono::steady_clock::time_point startTime_;

    /// Route table: "METHOD /path" → handler
    std::unordered_map<std::string, RouteHandler> routes_;
};

} // namespace brightchain::gossip
