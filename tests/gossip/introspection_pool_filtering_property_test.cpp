// Feature: cpp-gossip-protocol
// Task 18.4: Property 25 — REST pool filtering by permissions
// **Validates: Requirements 11.3**
//
// For any request to GET /api/introspection/pools by a member with Read
// permissions on a subset S of all pools, the response shall contain exactly
// the pools in S (plus all pools if the member is Admin/System).

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/introspection_api.hpp>
#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/gossip/discovery_protocol.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/member.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#if __has_include(<jwt-cpp/jwt.h>)
#include <jwt-cpp/jwt.h>
#define HAS_JWT_CPP 1
#else
#define HAS_JWT_CPP 0
#endif

using namespace brightchain::gossip;
using brightchain::MemberType;
namespace http = boost::beast::http;

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

const std::string kJwtSecret = "test-secret-key-for-jwt-signing-1234567890";

std::string createTestJwt(const std::string& memberId,
                          MemberType memberType,
                          const std::vector<std::string>& permissions = {}) {
#if HAS_JWT_CPP
    auto expiresAt = std::chrono::system_clock::now() + std::chrono::hours(1);
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
    (void)memberId; (void)memberType; (void)permissions;
    return "";
#endif
}

HttpRequest makeRequest(http::verb method, const std::string& target,
                        const std::string& token) {
    HttpRequest req{method, target, 11};
    req.set(http::field::host, "localhost");
    if (!token.empty()) {
        req.set(http::field::authorization, "Bearer " + token);
    }
    return req;
}

// ── Generators ─────────────────────────────────────────────────────────────

rc::Gen<std::string> genId() {
    return rc::gen::nonEmpty(
        rc::gen::container<std::string>(
            rc::gen::inRange(static_cast<char>('a'), static_cast<char>('z' + 1))));
}

rc::Gen<MemberType> genMemberType() {
    return rc::gen::element(
        MemberType::Admin,
        MemberType::System,
        MemberType::User,
        MemberType::Anonymous);
}

/// Generate a vector of N unique pool IDs (deterministic from count).
std::vector<std::string> makePoolIds(int count) {
    std::vector<std::string> ids;
    ids.reserve(count);
    for (int i = 0; i < count; ++i) {
        ids.push_back("pool-" + std::to_string(i));
    }
    return ids;
}

/// Create IntrospectionApi with pools in cache, dispatch a request.
/// Returns the HTTP response.
HttpResponse dispatchPoolsRequest(
    const std::vector<std::string>& poolIds,
    const std::string& token) {
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        MemberType::Admin, "test-node", "test@test.com");
    PeerManager peerManager(ioc, localMember);

    // Populate pool cache with the given pool IDs.
    for (const auto& poolId : poolIds) {
        PoolAnnouncementMetadata meta;
        meta.blockCount = 10;
        meta.totalSize = 1024;
        meta.encrypted = false;
        peerManager.updatePoolCache(poolId, meta, "host-node");
    }

    char dummyStorage[256]{};
    char dummyRegistry[256]{};

    GossipEngine engine(
        peerManager,
        *reinterpret_cast<brightchain::DiskBlockStore*>(&dummyStorage),
        *reinterpret_cast<brightchain::db::HeadRegistry*>(&dummyRegistry),
        localMember);

    DiscoveryProtocol discovery(
        peerManager,
        *reinterpret_cast<brightchain::DiskBlockStore*>(&dummyStorage));

    IntrospectionApi api(
        engine, peerManager,
        *reinterpret_cast<brightchain::DiskBlockStore*>(&dummyStorage),
        discovery, kJwtSecret);

    auto req = makeRequest(http::verb::get, "/api/introspection/pools", token);
    HttpResponse res;
    api.handleRequest(req, res);
    return res;
}

/// Extract pool IDs from a successful pools response.
std::set<std::string> extractPoolIds(const HttpResponse& res) {
    std::set<std::string> ids;
    auto body = nlohmann::json::parse(res.body());
    for (const auto& entry : body["data"]) {
        ids.insert(entry["poolId"].get<std::string>());
    }
    return ids;
}

} // namespace

// ── Property tests ─────────────────────────────────────────────────────────

#if HAS_JWT_CPP

// Property 25a: Admin/System members see ALL pools regardless of permissions.
RC_GTEST_PROP(RestPoolFilteringProperty,
              AdminOrSystemSeesAllPools,
              ()) {
    auto memberType = *rc::gen::element(MemberType::Admin, MemberType::System);
    auto memberId = *genId();

    // Generate 1–5 unique pool IDs.
    auto poolCount = *rc::gen::inRange(1, 6);
    auto poolIds = makePoolIds(poolCount);

    // Admin/System with no explicit pool permissions should still see all.
    auto token = createTestJwt(memberId, memberType);
    auto res = dispatchPoolsRequest(poolIds, token);

    RC_ASSERT(res.result() == http::status::ok);

    auto returnedIds = extractPoolIds(res);
    std::set<std::string> expectedIds(poolIds.begin(), poolIds.end());
    RC_ASSERT(returnedIds == expectedIds);
}

// Property 25b: User/Anonymous members see ONLY pools they have
// pool:<poolId>:read permission for.
RC_GTEST_PROP(RestPoolFilteringProperty,
              UserSeesOnlyPermittedPools,
              ()) {
    auto memberType = *rc::gen::element(MemberType::User, MemberType::Anonymous);
    auto memberId = *genId();

    // Generate 2–5 unique pool IDs.
    auto poolCount = *rc::gen::inRange(2, 6);
    auto poolIds = makePoolIds(poolCount);

    // Grant read permission on a random non-empty subset.
    auto permittedCount = *rc::gen::inRange(1, static_cast<int>(poolIds.size()));
    std::vector<std::string> permissions;
    std::set<std::string> expectedIds;
    for (int i = 0; i < permittedCount; ++i) {
        permissions.push_back("pool:" + poolIds[i] + ":read");
        expectedIds.insert(poolIds[i]);
    }

    auto token = createTestJwt(memberId, memberType, permissions);
    auto res = dispatchPoolsRequest(poolIds, token);

    RC_ASSERT(res.result() == http::status::ok);

    auto returnedIds = extractPoolIds(res);
    RC_ASSERT(returnedIds == expectedIds);
}

// Property 25c: User with NO read permissions sees zero pools.
RC_GTEST_PROP(RestPoolFilteringProperty,
              UserWithNoPermissionsSeesNoPools,
              ()) {
    auto memberType = *rc::gen::element(MemberType::User, MemberType::Anonymous);
    auto memberId = *genId();

    // Generate 1–5 unique pool IDs.
    auto poolCount2 = *rc::gen::inRange(1, 6);
    auto poolIds = makePoolIds(poolCount2);

    // No permissions at all.
    auto token = createTestJwt(memberId, memberType);
    auto res = dispatchPoolsRequest(poolIds, token);

    RC_ASSERT(res.result() == http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    RC_ASSERT(body["data"].size() == 0u);
}

// Property 25d: Universal — response pool set equals exactly the set of
// permitted pools for any member type and permission combination.
RC_GTEST_PROP(RestPoolFilteringProperty,
              ResponseMatchesExactlyPermittedPools,
              ()) {
    auto memberType = *genMemberType();
    auto memberId = *genId();

    // Generate 1–5 unique pool IDs.
    auto poolCount3 = *rc::gen::inRange(1, 6);
    auto poolIds = makePoolIds(poolCount3);

    // For each pool, randomly decide if the member has read permission.
    std::vector<std::string> permissions;
    std::set<std::string> permittedIds;
    for (const auto& poolId : poolIds) {
        if (*rc::gen::inRange(0, 2) == 1) {
            permissions.push_back("pool:" + poolId + ":read");
            permittedIds.insert(poolId);
        }
    }

    auto token = createTestJwt(memberId, memberType, permissions);
    auto res = dispatchPoolsRequest(poolIds, token);

    RC_ASSERT(res.result() == http::status::ok);

    bool isAdmin = (memberType == MemberType::Admin ||
                    memberType == MemberType::System);

    std::set<std::string> expectedIds;
    if (isAdmin) {
        // Admin/System see all pools regardless of permissions.
        expectedIds = std::set<std::string>(poolIds.begin(), poolIds.end());
    } else {
        expectedIds = permittedIds;
    }

    auto returnedIds = extractPoolIds(res);
    RC_ASSERT(returnedIds == expectedIds);
}

#endif // HAS_JWT_CPP
