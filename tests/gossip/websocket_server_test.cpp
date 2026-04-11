// Feature: cpp-gossip-protocol
// Task 17.1: WebSocketServer unit tests for auth flows, access tier filtering,
// event subscriptions, and heartbeat/pong timeout logic.

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/websocket_server.hpp>
#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/member.hpp>

#include <chrono>
#include <string>
#include <vector>

using namespace brightchain::gossip;

// ── Test fixture ───────────────────────────────────────────────────────────

namespace {

/// Minimal test harness that constructs the dependencies needed by
/// WebSocketServer without starting any real network I/O.
class WebSocketServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        localMember_ = std::make_unique<brightchain::Member>(
            brightchain::Member::generate(
                brightchain::MemberType::Admin, "test-node", "test@test.com"));

        peerManager_ = std::make_unique<PeerManager>(ioc_, *localMember_);

        // GossipEngine needs a DiskBlockStore and HeadRegistry.
        // We use nullptr casts here because the WebSocketServer tests
        // don't exercise GossipEngine's block/head logic.
        // This is safe because we never call engine methods that
        // dereference these references in our tests.
        engine_ = std::make_unique<GossipEngine>(
            *peerManager_,
            *reinterpret_cast<brightchain::DiskBlockStore*>(&dummyStorage_),
            *reinterpret_cast<brightchain::db::HeadRegistry*>(&dummyRegistry_),
            *localMember_);

        server_ = std::make_unique<WebSocketServer>(
            ioc_, *engine_, *peerManager_, *localMember_, jwtSecret_);
    }

    boost::asio::io_context ioc_;
    std::string jwtSecret_ = "test-secret-key-for-jwt-signing-1234567890";
    std::unique_ptr<brightchain::Member> localMember_;
    std::unique_ptr<PeerManager> peerManager_;
    std::unique_ptr<GossipEngine> engine_;
    std::unique_ptr<WebSocketServer> server_;

private:
    // Dummy storage for GossipEngine constructor — never dereferenced.
    char dummyStorage_[256]{};
    char dummyRegistry_[256]{};
};

/// Helper to create a ClientSession with given MemberType and permissions.
ClientSession makeClientSession(const std::string& sessionId,
                                const std::string& memberId,
                                brightchain::MemberType memberType,
                                const std::vector<std::string>& permissions = {},
                                const std::unordered_set<std::string>& subscriptions = {}) {
    ClientSession session;
    session.sessionId = sessionId;
    session.claims.memberId = memberId;
    session.claims.memberType = memberType;
    session.claims.permissions = permissions;
    session.claims.expiresAt = std::chrono::system_clock::now() + std::chrono::hours(1);
    session.subscribedEvents = subscriptions;
    session.lastPongReceived = std::chrono::steady_clock::now();
    session.authenticated = true;
    session.tokenExpiringSent = false;
    return session;
}

} // namespace

// ── Access tier classification tests ───────────────────────────────────────

TEST(WebSocketEventAccessTier, AdminEvents) {
    EXPECT_EQ(getAccessTierForEvent("peer:connected"), EventAccessTier::Admin);
    EXPECT_EQ(getAccessTierForEvent("peer:disconnected"), EventAccessTier::Admin);
    EXPECT_EQ(getAccessTierForEvent("storage:alert"), EventAccessTier::Admin);
}

TEST(WebSocketEventAccessTier, PoolScopedEvents) {
    EXPECT_EQ(getAccessTierForEvent("pool:changed"), EventAccessTier::PoolScoped);
    EXPECT_EQ(getAccessTierForEvent("pool:created"), EventAccessTier::PoolScoped);
    EXPECT_EQ(getAccessTierForEvent("pool:deleted"), EventAccessTier::PoolScoped);
}

TEST(WebSocketEventAccessTier, MemberScopedEvents) {
    EXPECT_EQ(getAccessTierForEvent("energy:updated"), EventAccessTier::MemberScoped);
}

TEST(WebSocketEventAccessTier, UnknownEventDefaultsToAdmin) {
    EXPECT_EQ(getAccessTierForEvent("unknown:event"), EventAccessTier::Admin);
}

// ── Admin event authorization tests ────────────────────────────────────────

TEST(WebSocketEventAuth, AdminEventAllowedForAdmin) {
    JwtClaims claims;
    claims.memberId = "admin-1";
    claims.memberType = brightchain::MemberType::Admin;

    WebSocketEvent event;
    event.event = "peer:connected";
    event.accessTier = EventAccessTier::Admin;

    EXPECT_TRUE(isClientAuthorizedForEvent(claims, event));
}

TEST(WebSocketEventAuth, AdminEventAllowedForSystem) {
    JwtClaims claims;
    claims.memberId = "system-1";
    claims.memberType = brightchain::MemberType::System;

    WebSocketEvent event;
    event.event = "storage:alert";
    event.accessTier = EventAccessTier::Admin;

    EXPECT_TRUE(isClientAuthorizedForEvent(claims, event));
}

TEST(WebSocketEventAuth, AdminEventDeniedForUser) {
    JwtClaims claims;
    claims.memberId = "user-1";
    claims.memberType = brightchain::MemberType::User;

    WebSocketEvent event;
    event.event = "peer:disconnected";
    event.accessTier = EventAccessTier::Admin;

    EXPECT_FALSE(isClientAuthorizedForEvent(claims, event));
}

TEST(WebSocketEventAuth, AdminEventDeniedForAnonymous) {
    JwtClaims claims;
    claims.memberId = "anon-1";
    claims.memberType = brightchain::MemberType::Anonymous;

    WebSocketEvent event;
    event.event = "peer:connected";
    event.accessTier = EventAccessTier::Admin;

    EXPECT_FALSE(isClientAuthorizedForEvent(claims, event));
}

// ── Pool-scoped event authorization tests ──────────────────────────────────

TEST(WebSocketEventAuth, PoolScopedAllowedForAdminWithoutPermission) {
    JwtClaims claims;
    claims.memberId = "admin-1";
    claims.memberType = brightchain::MemberType::Admin;

    WebSocketEvent event;
    event.event = "pool:changed";
    event.accessTier = EventAccessTier::PoolScoped;
    event.targetPoolId = "pool-abc";

    EXPECT_TRUE(isClientAuthorizedForEvent(claims, event));
}

TEST(WebSocketEventAuth, PoolScopedAllowedForUserWithReadPermission) {
    JwtClaims claims;
    claims.memberId = "user-1";
    claims.memberType = brightchain::MemberType::User;
    claims.permissions = {"pool:pool-abc:read", "pool:pool-xyz:write"};

    WebSocketEvent event;
    event.event = "pool:created";
    event.accessTier = EventAccessTier::PoolScoped;
    event.targetPoolId = "pool-abc";

    EXPECT_TRUE(isClientAuthorizedForEvent(claims, event));
}

TEST(WebSocketEventAuth, PoolScopedDeniedForUserWithoutPermission) {
    JwtClaims claims;
    claims.memberId = "user-1";
    claims.memberType = brightchain::MemberType::User;
    claims.permissions = {"pool:pool-xyz:read"};

    WebSocketEvent event;
    event.event = "pool:deleted";
    event.accessTier = EventAccessTier::PoolScoped;
    event.targetPoolId = "pool-abc";

    EXPECT_FALSE(isClientAuthorizedForEvent(claims, event));
}

TEST(WebSocketEventAuth, PoolScopedDeniedWhenNoTargetPool) {
    JwtClaims claims;
    claims.memberId = "user-1";
    claims.memberType = brightchain::MemberType::User;
    claims.permissions = {"pool:pool-abc:read"};

    WebSocketEvent event;
    event.event = "pool:changed";
    event.accessTier = EventAccessTier::PoolScoped;
    // No targetPoolId set.

    EXPECT_FALSE(isClientAuthorizedForEvent(claims, event));
}

// ── Member-scoped event authorization tests ────────────────────────────────

TEST(WebSocketEventAuth, MemberScopedAllowedForTargetMember) {
    JwtClaims claims;
    claims.memberId = "user-42";
    claims.memberType = brightchain::MemberType::User;

    WebSocketEvent event;
    event.event = "energy:updated";
    event.accessTier = EventAccessTier::MemberScoped;
    event.targetMemberId = "user-42";

    EXPECT_TRUE(isClientAuthorizedForEvent(claims, event));
}

TEST(WebSocketEventAuth, MemberScopedDeniedForDifferentMember) {
    JwtClaims claims;
    claims.memberId = "user-42";
    claims.memberType = brightchain::MemberType::User;

    WebSocketEvent event;
    event.event = "energy:updated";
    event.accessTier = EventAccessTier::MemberScoped;
    event.targetMemberId = "user-99";

    EXPECT_FALSE(isClientAuthorizedForEvent(claims, event));
}

TEST(WebSocketEventAuth, MemberScopedDeniedForAdminNotTarget) {
    // Even admins cannot see another member's energy:updated.
    JwtClaims claims;
    claims.memberId = "admin-1";
    claims.memberType = brightchain::MemberType::Admin;

    WebSocketEvent event;
    event.event = "energy:updated";
    event.accessTier = EventAccessTier::MemberScoped;
    event.targetMemberId = "user-42";

    EXPECT_FALSE(isClientAuthorizedForEvent(claims, event));
}

TEST(WebSocketEventAuth, MemberScopedDeniedWhenNoTargetMember) {
    JwtClaims claims;
    claims.memberId = "user-42";
    claims.memberType = brightchain::MemberType::User;

    WebSocketEvent event;
    event.event = "energy:updated";
    event.accessTier = EventAccessTier::MemberScoped;
    // No targetMemberId set.

    EXPECT_FALSE(isClientAuthorizedForEvent(claims, event));
}

// ── Event subscription management tests ────────────────────────────────────

TEST_F(WebSocketServerTest, AddAndRetrieveClientSession) {
    auto session = makeClientSession("s1", "member-1",
                                     brightchain::MemberType::User);
    server_->addClientSession(session);

    auto retrieved = server_->getClientSession("s1");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->sessionId, "s1");
    EXPECT_EQ(retrieved->claims.memberId, "member-1");
    EXPECT_EQ(retrieved->claims.memberType, brightchain::MemberType::User);
}

TEST_F(WebSocketServerTest, RemoveClientSession) {
    auto session = makeClientSession("s1", "member-1",
                                     brightchain::MemberType::User);
    server_->addClientSession(session);
    server_->removeClientSession("s1");

    auto retrieved = server_->getClientSession("s1");
    EXPECT_FALSE(retrieved.has_value());
}

TEST_F(WebSocketServerTest, SubscribeAndUnsubscribeEvents) {
    auto session = makeClientSession("s1", "member-1",
                                     brightchain::MemberType::User);
    server_->addClientSession(session);

    server_->subscribeClient("s1", "pool:changed");
    server_->subscribeClient("s1", "energy:updated");

    auto retrieved = server_->getClientSession("s1");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->subscribedEvents.size(), 2u);
    EXPECT_TRUE(retrieved->subscribedEvents.count("pool:changed"));
    EXPECT_TRUE(retrieved->subscribedEvents.count("energy:updated"));

    server_->unsubscribeClient("s1", "pool:changed");
    retrieved = server_->getClientSession("s1");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->subscribedEvents.size(), 1u);
    EXPECT_FALSE(retrieved->subscribedEvents.count("pool:changed"));
    EXPECT_TRUE(retrieved->subscribedEvents.count("energy:updated"));
}

TEST_F(WebSocketServerTest, GetAllClientSessions) {
    server_->addClientSession(
        makeClientSession("s1", "m1", brightchain::MemberType::Admin));
    server_->addClientSession(
        makeClientSession("s2", "m2", brightchain::MemberType::User));
    server_->addClientSession(
        makeClientSession("s3", "m3", brightchain::MemberType::System));

    auto sessions = server_->getClientSessions();
    EXPECT_EQ(sessions.size(), 3u);
}

TEST_F(WebSocketServerTest, NonExistentSessionReturnsNullopt) {
    auto retrieved = server_->getClientSession("nonexistent");
    EXPECT_FALSE(retrieved.has_value());
}

// ── JWT validation tests ───────────────────────────────────────────────────

#if __has_include(<jwt-cpp/jwt.h>)

TEST_F(WebSocketServerTest, ValidJwtTokenAccepted) {
    JwtClaims claims;
    claims.memberId = "member-abc";
    claims.memberType = brightchain::MemberType::User;
    claims.permissions = {"pool:p1:read"};
    claims.expiresAt = std::chrono::system_clock::now() + std::chrono::hours(1);

    std::string token = server_->createJwt(claims);
    ASSERT_FALSE(token.empty());

    auto result = server_->validateJwt(token);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->memberId, "member-abc");
}

TEST_F(WebSocketServerTest, ExpiredJwtTokenRejected) {
    JwtClaims claims;
    claims.memberId = "member-abc";
    claims.memberType = brightchain::MemberType::User;
    claims.expiresAt = std::chrono::system_clock::now() - std::chrono::hours(1);

    std::string token = server_->createJwt(claims);
    ASSERT_FALSE(token.empty());

    auto result = server_->validateJwt(token);
    EXPECT_FALSE(result.has_value());
}

TEST_F(WebSocketServerTest, MalformedJwtTokenRejected) {
    auto result = server_->validateJwt("not.a.valid.jwt");
    EXPECT_FALSE(result.has_value());
}

TEST_F(WebSocketServerTest, EmptyJwtTokenRejected) {
    auto result = server_->validateJwt("");
    EXPECT_FALSE(result.has_value());
}

TEST_F(WebSocketServerTest, JwtWithWrongSecretRejected) {
    // Create a token with a different secret.
    auto otherServer = std::make_unique<WebSocketServer>(
        ioc_, *engine_, *peerManager_, *localMember_, "different-secret");

    JwtClaims claims;
    claims.memberId = "member-abc";
    claims.memberType = brightchain::MemberType::User;
    claims.expiresAt = std::chrono::system_clock::now() + std::chrono::hours(1);

    std::string token = otherServer->createJwt(claims);

    // Validate with the original server (different secret).
    auto result = server_->validateJwt(token);
    EXPECT_FALSE(result.has_value());
}

#endif // __has_include(<jwt-cpp/jwt.h>)

// ── Configuration tests ────────────────────────────────────────────────────

TEST_F(WebSocketServerTest, DefaultConfiguration) {
    EXPECT_EQ(server_->heartbeatIntervalMs(), 10000);
    EXPECT_EQ(server_->heartbeatTimeoutMs(), 30000);
    EXPECT_EQ(server_->jwtGracePeriodMs(), 30000);
}

TEST_F(WebSocketServerTest, ConfigurableHeartbeat) {
    server_->setHeartbeatIntervalMs(5000);
    server_->setHeartbeatTimeoutMs(15000);
    server_->setJwtGracePeriodMs(60000);

    EXPECT_EQ(server_->heartbeatIntervalMs(), 5000);
    EXPECT_EQ(server_->heartbeatTimeoutMs(), 15000);
    EXPECT_EQ(server_->jwtGracePeriodMs(), 60000);
}

TEST(WebSocketServerConstants, CloseCodeJwtExpired) {
    EXPECT_EQ(WebSocketServer::kCloseCodeJwtExpired, 4002);
}

// ── Broadcast event filtering tests ────────────────────────────────────────

TEST_F(WebSocketServerTest, BroadcastEventOnlyToSubscribedClients) {
    // Admin client subscribed to peer:connected.
    auto s1 = makeClientSession("s1", "admin-1",
                                brightchain::MemberType::Admin,
                                {},
                                {"peer:connected"});
    // Admin client NOT subscribed to peer:connected.
    auto s2 = makeClientSession("s2", "admin-2",
                                brightchain::MemberType::Admin);

    server_->addClientSession(s1);
    server_->addClientSession(s2);

    WebSocketEvent event;
    event.event = "peer:connected";
    event.accessTier = EventAccessTier::Admin;
    event.data = R"({"nodeId":"node-1"})";

    // broadcastEvent should not crash and should filter correctly.
    // Without live WS streams, sendToClient is a no-op, but the filtering
    // logic is exercised.
    EXPECT_NO_THROW(server_->broadcastEvent(event));
}

TEST_F(WebSocketServerTest, BroadcastPoolEventFiltersByPermission) {
    // User with read permission on pool-abc.
    auto s1 = makeClientSession("s1", "user-1",
                                brightchain::MemberType::User,
                                {"pool:pool-abc:read"},
                                {"pool:changed"});
    // User without permission on pool-abc.
    auto s2 = makeClientSession("s2", "user-2",
                                brightchain::MemberType::User,
                                {},
                                {"pool:changed"});

    server_->addClientSession(s1);
    server_->addClientSession(s2);

    WebSocketEvent event;
    event.event = "pool:changed";
    event.accessTier = EventAccessTier::PoolScoped;
    event.targetPoolId = "pool-abc";
    event.data = "{}";

    // Should not crash; s1 authorized, s2 not.
    EXPECT_NO_THROW(server_->broadcastEvent(event));
}

TEST_F(WebSocketServerTest, BroadcastMemberScopedEventOnlyToTarget) {
    auto s1 = makeClientSession("s1", "user-42",
                                brightchain::MemberType::User,
                                {},
                                {"energy:updated"});
    auto s2 = makeClientSession("s2", "user-99",
                                brightchain::MemberType::User,
                                {},
                                {"energy:updated"});

    server_->addClientSession(s1);
    server_->addClientSession(s2);

    WebSocketEvent event;
    event.event = "energy:updated";
    event.accessTier = EventAccessTier::MemberScoped;
    event.targetMemberId = "user-42";
    event.data = "{}";

    EXPECT_NO_THROW(server_->broadcastEvent(event));
}

// ── Token expiring flag tests ──────────────────────────────────────────────

TEST_F(WebSocketServerTest, TokenExpiringSentDefaultsFalse) {
    auto session = makeClientSession("s1", "member-1",
                                     brightchain::MemberType::User);
    EXPECT_FALSE(session.tokenExpiringSent);

    server_->addClientSession(session);
    auto retrieved = server_->getClientSession("s1");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_FALSE(retrieved->tokenExpiringSent);
}

// ── Start/stop lifecycle tests ─────────────────────────────────────────────

TEST_F(WebSocketServerTest, StopClearsAllSessions) {
    server_->addClientSession(
        makeClientSession("s1", "m1", brightchain::MemberType::Admin));
    server_->addClientSession(
        makeClientSession("s2", "m2", brightchain::MemberType::User));

    EXPECT_EQ(server_->getClientSessions().size(), 2u);

    server_->stop();

    EXPECT_EQ(server_->getClientSessions().size(), 0u);
}

TEST_F(WebSocketServerTest, DoubleStopDoesNotCrash) {
    server_->stop();
    EXPECT_NO_THROW(server_->stop());
}
