// Feature: cpp-gossip-protocol
// Task 18.1: IntrospectionApi unit tests for endpoint responses, JWT auth,
// admin access control, and pool filtering by permissions.

#include <gtest/gtest.h>

#include <brightchain/gossip/introspection_api.hpp>
#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/gossip/discovery_protocol.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/member.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <set>
#include <string>

// jwt-cpp for creating test tokens.
#if __has_include(<jwt-cpp/jwt.h>)
#include <jwt-cpp/jwt.h>
#define HAS_JWT_CPP 1
#else
#define HAS_JWT_CPP 0
#endif

using namespace brightchain::gossip;
namespace http = boost::beast::http;

// ── Test helpers ───────────────────────────────────────────────────────────

namespace {

const std::string kJwtSecret = "test-secret-key-for-jwt-signing-1234567890";

/// Create a signed JWT token for testing.
std::string createTestJwt(const std::string& memberId,
                          brightchain::MemberType memberType,
                          const std::vector<std::string>& permissions = {},
                          std::chrono::system_clock::time_point expiresAt = {}) {
#if HAS_JWT_CPP
    if (expiresAt == std::chrono::system_clock::time_point{}) {
        expiresAt = std::chrono::system_clock::now() + std::chrono::hours(1);
    }

    auto builder = jwt::create()
        .set_issuer("brightchain")
        .set_type("JWT")
        .set_payload_claim("memberId", jwt::claim(memberId))
        .set_payload_claim("memberType",
                           jwt::claim(std::to_string(static_cast<int>(memberType))))
        .set_expires_at(expiresAt);

    if (!permissions.empty()) {
        std::set<std::string> permSet(permissions.begin(), permissions.end());
        builder.set_payload_claim("permissions", jwt::claim(permSet));
    }

    return builder.sign(jwt::algorithm::hs256{kJwtSecret});
#else
    (void)memberId; (void)memberType; (void)permissions; (void)expiresAt;
    return "";
#endif
}

/// Build an HTTP request with the given method, target, and optional auth token.
HttpRequest makeRequest(http::verb method, const std::string& target,
                        const std::string& token = "") {
    HttpRequest req{method, target, 11};
    req.set(http::field::host, "localhost");
    if (!token.empty()) {
        req.set(http::field::authorization, "Bearer " + token);
    }
    return req;
}

} // namespace

// ── Test fixture ───────────────────────────────────────────────────────────

class IntrospectionApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        localMember_ = std::make_unique<brightchain::Member>(
            brightchain::Member::generate(
                brightchain::MemberType::Admin, "test-node", "test@test.com"));

        peerManager_ = std::make_unique<PeerManager>(ioc_, *localMember_);

        engine_ = std::make_unique<GossipEngine>(
            *peerManager_,
            *reinterpret_cast<brightchain::DiskBlockStore*>(&dummyStorage_),
            *reinterpret_cast<brightchain::db::HeadRegistry*>(&dummyRegistry_),
            *localMember_);

        discovery_ = std::make_unique<DiscoveryProtocol>(
            *peerManager_,
            *reinterpret_cast<brightchain::DiskBlockStore*>(&dummyStorage_));

        api_ = std::make_unique<IntrospectionApi>(
            *engine_, *peerManager_,
            *reinterpret_cast<brightchain::DiskBlockStore*>(&dummyStorage_),
            *discovery_, kJwtSecret);
    }

    /// Helper: dispatch a request and return the response.
    HttpResponse dispatch(const HttpRequest& req) {
        HttpResponse res;
        api_->handleRequest(req, res);
        return res;
    }

    boost::asio::io_context ioc_;
    std::unique_ptr<brightchain::Member> localMember_;
    std::unique_ptr<PeerManager> peerManager_;
    std::unique_ptr<GossipEngine> engine_;
    std::unique_ptr<DiscoveryProtocol> discovery_;
    std::unique_ptr<IntrospectionApi> api_;

private:
    char dummyStorage_[256]{};
    char dummyRegistry_[256]{};
};


// ── JWT authentication tests (Req 11.6) ────────────────────────────────────

TEST_F(IntrospectionApiTest, MissingTokenReturns401) {
    auto req = makeRequest(http::verb::get, "/api/introspection/status");
    auto res = dispatch(req);
    EXPECT_EQ(res.result(), http::status::unauthorized);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["message"], "Unauthorized");
}

TEST_F(IntrospectionApiTest, EmptyBearerTokenReturns401) {
    auto req = makeRequest(http::verb::get, "/api/introspection/status");
    req.set(http::field::authorization, "Bearer ");
    auto res = dispatch(req);
    EXPECT_EQ(res.result(), http::status::unauthorized);
}

TEST_F(IntrospectionApiTest, MalformedTokenReturns401) {
    auto req = makeRequest(http::verb::get, "/api/introspection/status",
                           "not-a-valid-jwt-token");
    auto res = dispatch(req);
    EXPECT_EQ(res.result(), http::status::unauthorized);
}

#if HAS_JWT_CPP
TEST_F(IntrospectionApiTest, ExpiredTokenReturns401) {
    auto expired = std::chrono::system_clock::now() - std::chrono::hours(1);
    auto token = createTestJwt("user-1", brightchain::MemberType::Admin, {}, expired);
    auto req = makeRequest(http::verb::get, "/api/introspection/status", token);
    auto res = dispatch(req);
    EXPECT_EQ(res.result(), http::status::unauthorized);
}
#endif

// ── Route not found ────────────────────────────────────────────────────────

TEST_F(IntrospectionApiTest, UnknownRouteReturns404) {
    auto token = createTestJwt("admin-1", brightchain::MemberType::Admin);
    auto req = makeRequest(http::verb::get, "/api/introspection/unknown", token);
    auto res = dispatch(req);
    EXPECT_EQ(res.result(), http::status::not_found);
}

// ── GET /api/introspection/status (Req 11.1) ──────────────────────────────

#if HAS_JWT_CPP
TEST_F(IntrospectionApiTest, StatusEndpointReturnsCorrectFields) {
    auto token = createTestJwt("admin-1", brightchain::MemberType::Admin);
    auto req = makeRequest(http::verb::get, "/api/introspection/status", token);
    auto res = dispatch(req);

    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["message"], "Success");
    ASSERT_TRUE(body.contains("data"));

    auto& data = body["data"];
    EXPECT_TRUE(data.contains("nodeId"));
    EXPECT_TRUE(data.contains("healthy"));
    EXPECT_TRUE(data.contains("uptime"));
    EXPECT_TRUE(data.contains("version"));
    EXPECT_TRUE(data.contains("capabilities"));
    EXPECT_TRUE(data.contains("partitionMode"));

    EXPECT_TRUE(data["healthy"].get<bool>());
    EXPECT_GE(data["uptime"].get<int64_t>(), 0);
    EXPECT_TRUE(data["capabilities"].is_array());
}

TEST_F(IntrospectionApiTest, StatusAccessibleByUser) {
    auto token = createTestJwt("user-1", brightchain::MemberType::User);
    auto req = makeRequest(http::verb::get, "/api/introspection/status", token);
    auto res = dispatch(req);
    EXPECT_EQ(res.result(), http::status::ok);
}

// ── GET /api/introspection/peers (Req 11.2, 11.7) ─────────────────────────

TEST_F(IntrospectionApiTest, PeersAccessibleByAdmin) {
    auto token = createTestJwt("admin-1", brightchain::MemberType::Admin);
    auto req = makeRequest(http::verb::get, "/api/introspection/peers", token);
    auto res = dispatch(req);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["message"], "Success");
    EXPECT_TRUE(body["data"].is_array());
}

TEST_F(IntrospectionApiTest, PeersAccessibleBySystem) {
    auto token = createTestJwt("system-1", brightchain::MemberType::System);
    auto req = makeRequest(http::verb::get, "/api/introspection/peers", token);
    auto res = dispatch(req);
    EXPECT_EQ(res.result(), http::status::ok);
}

TEST_F(IntrospectionApiTest, PeersForbiddenForUser) {
    auto token = createTestJwt("user-1", brightchain::MemberType::User);
    auto req = makeRequest(http::verb::get, "/api/introspection/peers", token);
    auto res = dispatch(req);
    EXPECT_EQ(res.result(), http::status::forbidden);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["message"], "Forbidden");
}

TEST_F(IntrospectionApiTest, PeersReturnsConnectedPeerInfo) {
    // Add a test peer.
    PeerInfo peer;
    peer.nodeId = "peer-1";
    peer.address = "192.168.1.100";
    peer.httpPort = 3000;
    peer.wsPort = 3001;
    peer.lastSeen = "2025-01-28T12:00:00.000Z";
    peer.capabilities = {"blocks", "gossip"};
    peer.connected = true;
    peer.latencyMs = 42.5;
    peerManager_->addPeer(peer);

    auto token = createTestJwt("admin-1", brightchain::MemberType::Admin);
    auto req = makeRequest(http::verb::get, "/api/introspection/peers", token);
    auto res = dispatch(req);

    EXPECT_EQ(res.result(), http::status::ok);
    auto body = nlohmann::json::parse(res.body());
    auto& data = body["data"];
    ASSERT_EQ(data.size(), 1u);
    EXPECT_EQ(data[0]["nodeId"], "peer-1");
    EXPECT_EQ(data[0]["address"], "192.168.1.100");
    EXPECT_EQ(data[0]["connected"], true);
    EXPECT_DOUBLE_EQ(data[0]["latencyMs"].get<double>(), 42.5);
}

// ── GET /api/introspection/pools (Req 11.3) ────────────────────────────────

TEST_F(IntrospectionApiTest, PoolsFilteredByPermissions) {
    // Add two pools to the cache.
    PoolAnnouncementMetadata meta1;
    meta1.blockCount = 100;
    meta1.totalSize = 1024;
    meta1.encrypted = false;
    peerManager_->updatePoolCache("pool-A", meta1, "host-1");

    PoolAnnouncementMetadata meta2;
    meta2.blockCount = 200;
    meta2.totalSize = 2048;
    meta2.encrypted = true;
    peerManager_->updatePoolCache("pool-B", meta2, "host-2");

    // User with read permission only on pool-A.
    auto token = createTestJwt("user-1", brightchain::MemberType::User,
                               {"pool:pool-A:read"});
    auto req = makeRequest(http::verb::get, "/api/introspection/pools", token);
    auto res = dispatch(req);

    EXPECT_EQ(res.result(), http::status::ok);
    auto body = nlohmann::json::parse(res.body());
    auto& data = body["data"];
    ASSERT_EQ(data.size(), 1u);
    EXPECT_EQ(data[0]["poolId"], "pool-A");
}

TEST_F(IntrospectionApiTest, AdminSeesAllPools) {
    PoolAnnouncementMetadata meta1;
    meta1.blockCount = 100;
    meta1.totalSize = 1024;
    meta1.encrypted = false;
    peerManager_->updatePoolCache("pool-A", meta1, "host-1");

    PoolAnnouncementMetadata meta2;
    meta2.blockCount = 200;
    meta2.totalSize = 2048;
    meta2.encrypted = true;
    peerManager_->updatePoolCache("pool-B", meta2, "host-2");

    auto token = createTestJwt("admin-1", brightchain::MemberType::Admin);
    auto req = makeRequest(http::verb::get, "/api/introspection/pools", token);
    auto res = dispatch(req);

    EXPECT_EQ(res.result(), http::status::ok);
    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["data"].size(), 2u);
}

// ── GET /api/introspection/stats (Req 11.4, 11.7) ─────────────────────────

TEST_F(IntrospectionApiTest, StatsAccessibleByAdmin) {
    auto token = createTestJwt("admin-1", brightchain::MemberType::Admin);
    auto req = makeRequest(http::verb::get, "/api/introspection/stats", token);
    auto res = dispatch(req);
    EXPECT_EQ(res.result(), http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_EQ(body["message"], "Success");
    EXPECT_TRUE(body["data"].contains("blockSize"));
}

TEST_F(IntrospectionApiTest, StatsForbiddenForUser) {
    auto token = createTestJwt("user-1", brightchain::MemberType::User);
    auto req = makeRequest(http::verb::get, "/api/introspection/stats", token);
    auto res = dispatch(req);
    EXPECT_EQ(res.result(), http::status::forbidden);
}

// ── POST /api/introspection/discover-pools (Req 11.5) ──────────────────────

TEST_F(IntrospectionApiTest, DiscoverPoolsDeduplicatesAndFilters) {
    PoolAnnouncementMetadata meta1;
    meta1.blockCount = 50;
    meta1.totalSize = 512;
    meta1.encrypted = false;
    peerManager_->updatePoolCache("pool-X", meta1, "host-1");

    PoolAnnouncementMetadata meta2;
    meta2.blockCount = 75;
    meta2.totalSize = 768;
    meta2.encrypted = false;
    peerManager_->updatePoolCache("pool-Y", meta2, "host-2");

    // User with read on pool-X only.
    auto token = createTestJwt("user-1", brightchain::MemberType::User,
                               {"pool:pool-X:read"});
    auto req = makeRequest(http::verb::post,
                           "/api/introspection/discover-pools", token);
    auto res = dispatch(req);

    EXPECT_EQ(res.result(), http::status::ok);
    auto body = nlohmann::json::parse(res.body());
    auto& data = body["data"];
    ASSERT_EQ(data.size(), 1u);
    EXPECT_EQ(data[0]["poolId"], "pool-X");
}

// ── Response envelope format (Req 14.4) ────────────────────────────────────

TEST_F(IntrospectionApiTest, SuccessResponseMatchesEnvelopeFormat) {
    auto token = createTestJwt("admin-1", brightchain::MemberType::Admin);
    auto req = makeRequest(http::verb::get, "/api/introspection/status", token);
    auto res = dispatch(req);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_TRUE(body.contains("message"));
    EXPECT_TRUE(body.contains("data"));
    EXPECT_EQ(body["message"], "Success");
}

TEST_F(IntrospectionApiTest, ErrorResponseMatchesEnvelopeFormat) {
    auto req = makeRequest(http::verb::get, "/api/introspection/status");
    auto res = dispatch(req);

    auto body = nlohmann::json::parse(res.body());
    EXPECT_TRUE(body.contains("message"));
    EXPECT_EQ(body["message"], "Unauthorized");
}
#endif // HAS_JWT_CPP
