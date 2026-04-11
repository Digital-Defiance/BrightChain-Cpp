#include <brightchain/gossip/introspection_api.hpp>
#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/gossip/discovery_protocol.hpp>
#include <brightchain/disk_block_store.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <set>

// jwt-cpp is header-only; conditionally include if available.
#if __has_include(<jwt-cpp/jwt.h>)
#include <jwt-cpp/jwt.h>
#define HAS_JWT_CPP 1
#else
#define HAS_JWT_CPP 0
#endif

namespace brightchain::gossip {

// ── Construction ───────────────────────────────────────────────────────────

IntrospectionApi::IntrospectionApi(GossipEngine& engine,
                                   PeerManager& peerManager,
                                   DiskBlockStore& blockStore,
                                   DiscoveryProtocol& discovery,
                                   const std::string& jwtSecret)
    : engine_(engine)
    , peerManager_(peerManager)
    , blockStore_(blockStore)
    , discovery_(discovery)
    , jwtSecret_(jwtSecret)
    , startTime_(std::chrono::steady_clock::now())
{
    // Register routes.
    routes_["GET /api/introspection/status"] =
        [this](const HttpRequest& req, HttpResponse& res) { handleStatus(req, res); };
    routes_["GET /api/introspection/peers"] =
        [this](const HttpRequest& req, HttpResponse& res) { handlePeers(req, res); };
    routes_["GET /api/introspection/pools"] =
        [this](const HttpRequest& req, HttpResponse& res) { handlePools(req, res); };
    routes_["GET /api/introspection/stats"] =
        [this](const HttpRequest& req, HttpResponse& res) { handleStats(req, res); };
    routes_["POST /api/introspection/discover-pools"] =
        [this](const HttpRequest& req, HttpResponse& res) { handleDiscoverPools(req, res); };
}

// ── Request dispatch ───────────────────────────────────────────────────────

void IntrospectionApi::handleRequest(const HttpRequest& req, HttpResponse& res) {
    std::string method(req.method_string());
    std::string target(req.target());

    // Strip query string if present.
    auto qpos = target.find('?');
    if (qpos != std::string::npos) {
        target = target.substr(0, qpos);
    }

    std::string key = method + " " + target;
    auto it = routes_.find(key);
    if (it != routes_.end()) {
        try {
            it->second(req, res);
        } catch (...) {
            sendError(res, boost::beast::http::status::internal_server_error,
                      "Internal Server Error");
        }
    } else {
        sendError(res, boost::beast::http::status::not_found, "Not Found");
    }
}

const std::unordered_map<std::string, RouteHandler>&
IntrospectionApi::getRoutes() const {
    return routes_;
}


// ── GET /api/introspection/status ──────────────────────────────────────────

void IntrospectionApi::handleStatus(const HttpRequest& req, HttpResponse& res) {
    auto claims = extractAndValidateJwt(req);
    if (!claims) {
        sendError(res, boost::beast::http::status::unauthorized, "Unauthorized");
        return;
    }

    const auto& config = engine_.getConfig();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - startTime_).count();

    nlohmann::json data;
    data["nodeId"] = config.fanout > 0 ? "local-node" : "local-node"; // placeholder
    data["healthy"] = true;
    data["uptime"] = uptime;
    data["version"] = version_;
    data["capabilities"] = nlohmann::json::array({"blocks", "pools", "gossip"});
    data["partitionMode"] = false;

    sendSuccess(res, "Success", data);
}

// ── GET /api/introspection/peers ───────────────────────────────────────────

void IntrospectionApi::handlePeers(const HttpRequest& req, HttpResponse& res) {
    auto claims = extractAndValidateJwt(req);
    if (!claims) {
        sendError(res, boost::beast::http::status::unauthorized, "Unauthorized");
        return;
    }

    if (!isAdminOrSystem(claims->memberType)) {
        sendError(res, boost::beast::http::status::forbidden, "Forbidden");
        return;
    }

    auto peers = peerManager_.getConnectedPeers();
    nlohmann::json peersJson = nlohmann::json::array();
    for (const auto& peer : peers) {
        nlohmann::json pj;
        pj["nodeId"] = peer.nodeId;
        pj["address"] = peer.address;
        pj["httpPort"] = peer.httpPort;
        pj["wsPort"] = peer.wsPort;
        pj["lastSeen"] = peer.lastSeen;
        pj["capabilities"] = peer.capabilities;
        pj["connected"] = peer.connected;
        pj["latencyMs"] = peer.latencyMs;
        peersJson.push_back(pj);
    }

    sendSuccess(res, "Success", peersJson);
}

// ── GET /api/introspection/pools ───────────────────────────────────────────

void IntrospectionApi::handlePools(const HttpRequest& req, HttpResponse& res) {
    auto claims = extractAndValidateJwt(req);
    if (!claims) {
        sendError(res, boost::beast::http::status::unauthorized, "Unauthorized");
        return;
    }

    auto poolCache = peerManager_.getPoolCache();
    nlohmann::json poolsJson = nlohmann::json::array();

    for (const auto& [poolId, entry] : poolCache) {
        // Admin/System see all pools; others need read permission.
        if (!isAdminOrSystem(claims->memberType) &&
            !hasPoolReadPermission(*claims, poolId)) {
            continue;
        }

        nlohmann::json pj;
        pj["poolId"] = poolId;
        pj["blockCount"] = entry.metadata.blockCount;
        pj["totalSize"] = entry.metadata.totalSize;
        pj["encrypted"] = entry.metadata.encrypted;
        pj["hostNodeId"] = entry.hostNodeId;
        poolsJson.push_back(pj);
    }

    sendSuccess(res, "Success", poolsJson);
}

// ── GET /api/introspection/stats ───────────────────────────────────────────

void IntrospectionApi::handleStats(const HttpRequest& req, HttpResponse& res) {
    auto claims = extractAndValidateJwt(req);
    if (!claims) {
        sendError(res, boost::beast::http::status::unauthorized, "Unauthorized");
        return;
    }

    if (!isAdminOrSystem(claims->memberType)) {
        sendError(res, boost::beast::http::status::forbidden, "Forbidden");
        return;
    }

    nlohmann::json data;
    data["storePath"] = blockStore_.storePath();
    data["blockSize"] = static_cast<int>(blockStore_.blockSize());

    // Compute storage stats from the filesystem.
    try {
        auto spaceInfo = std::filesystem::space(blockStore_.storePath());
        data["totalCapacity"] = spaceInfo.capacity;
        data["availableSpace"] = spaceInfo.available;
        data["currentUsage"] = spaceInfo.capacity - spaceInfo.available;
    } catch (...) {
        data["totalCapacity"] = 0;
        data["availableSpace"] = 0;
        data["currentUsage"] = 0;
    }

    sendSuccess(res, "Success", data);
}

// ── POST /api/introspection/discover-pools ─────────────────────────────────

void IntrospectionApi::handleDiscoverPools(const HttpRequest& req,
                                           HttpResponse& res) {
    auto claims = extractAndValidateJwt(req);
    if (!claims) {
        sendError(res, boost::beast::http::status::unauthorized, "Unauthorized");
        return;
    }

    // Gather pools from all connected peers, deduplicate by poolId.
    auto poolCache = peerManager_.getPoolCache();
    std::set<std::string> seenPoolIds;
    nlohmann::json poolsJson = nlohmann::json::array();

    for (const auto& [poolId, entry] : poolCache) {
        if (seenPoolIds.count(poolId) > 0) continue;
        seenPoolIds.insert(poolId);

        // Filter by ACL: Admin/System see all, others need read permission.
        if (!isAdminOrSystem(claims->memberType) &&
            !hasPoolReadPermission(*claims, poolId)) {
            continue;
        }

        nlohmann::json pj;
        pj["poolId"] = poolId;
        pj["blockCount"] = entry.metadata.blockCount;
        pj["totalSize"] = entry.metadata.totalSize;
        pj["encrypted"] = entry.metadata.encrypted;
        pj["hostNodeId"] = entry.hostNodeId;
        poolsJson.push_back(pj);
    }

    sendSuccess(res, "Success", poolsJson);
}


// ── JWT validation ─────────────────────────────────────────────────────────

std::optional<JwtClaims> IntrospectionApi::extractAndValidateJwt(
    const HttpRequest& req) const {
    // Look for "Authorization: Bearer <token>" header.
    auto it = req.find(boost::beast::http::field::authorization);
    if (it == req.end()) return std::nullopt;

    std::string authHeader(it->value());
    const std::string bearerPrefix = "Bearer ";
    if (authHeader.size() <= bearerPrefix.size() ||
        authHeader.substr(0, bearerPrefix.size()) != bearerPrefix) {
        return std::nullopt;
    }

    std::string token = authHeader.substr(bearerPrefix.size());
    if (token.empty()) return std::nullopt;

#if HAS_JWT_CPP
    try {
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{jwtSecret_})
            .with_issuer("brightchain");

        auto decoded = jwt::decode(token);
        verifier.verify(decoded);

        JwtClaims claims;
        if (decoded.has_payload_claim("memberId")) {
            claims.memberId = decoded.get_payload_claim("memberId").as_string();
        }
        if (decoded.has_payload_claim("memberType")) {
            int mt = decoded.get_payload_claim("memberType").as_integer();
            claims.memberType = static_cast<MemberType>(std::clamp(mt, 0, 3));
        }
        if (decoded.has_payload_claim("permissions")) {
            auto permClaim = decoded.get_payload_claim("permissions");
            auto permSet = permClaim.as_set();
            for (const auto& p : permSet) {
                claims.permissions.push_back(p);
            }
        }
        if (decoded.has_payload_claim("exp")) {
            auto exp = decoded.get_payload_claim("exp").as_date();
            claims.expiresAt = exp;
        }

        return claims;
    } catch (...) {
        return std::nullopt;
    }
#else
    (void)token;
    return std::nullopt;
#endif
}

// ── Auth helpers ───────────────────────────────────────────────────────────

bool IntrospectionApi::isAdminOrSystem(MemberType type) {
    return type == MemberType::Admin || type == MemberType::System;
}

bool IntrospectionApi::hasPoolReadPermission(const JwtClaims& claims,
                                             const std::string& poolId) {
    const std::string requiredPerm = "pool:" + poolId + ":read";
    return std::find(claims.permissions.begin(), claims.permissions.end(),
                     requiredPerm) != claims.permissions.end();
}

// ── Response helpers ───────────────────────────────────────────────────────

void IntrospectionApi::sendSuccess(HttpResponse& res,
                                   const std::string& message,
                                   const nlohmann::json& data) {
    nlohmann::json body;
    body["message"] = message;
    body["data"] = data;

    res.result(boost::beast::http::status::ok);
    res.set(boost::beast::http::field::content_type, "application/json");
    res.body() = body.dump();
    res.prepare_payload();
}

void IntrospectionApi::sendError(HttpResponse& res,
                                 boost::beast::http::status status,
                                 const std::string& message,
                                 const std::string& error) {
    nlohmann::json body;
    body["message"] = message;
    if (!error.empty()) {
        body["error"] = error;
    }

    res.result(status);
    res.set(boost::beast::http::field::content_type, "application/json");
    res.body() = body.dump();
    res.prepare_payload();
}

} // namespace brightchain::gossip
