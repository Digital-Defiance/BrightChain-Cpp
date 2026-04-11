// Feature: cpp-gossip-protocol
// Task 18.2: Property 23 — REST admin access control
// **Validates: Requirements 11.2, 11.4, 11.7**
//
// For any MemberType and any admin-restricted endpoint (/peers, /stats),
// the response status is 200 if and only if the member is Admin or System,
// and 403 otherwise.

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

#include <set>
#include <string>

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

/// Admin-restricted endpoint index: 0 = /peers, 1 = /stats.
struct AdminEndpoint {
    http::verb method;
    std::string path;
};

AdminEndpoint adminEndpointFromIndex(int idx) {
    switch (idx) {
    case 0:  return {http::verb::get, "/api/introspection/peers"};
    default: return {http::verb::get, "/api/introspection/stats"};
    }
}

/// Create the IntrospectionApi and its dependencies, dispatch a request.
HttpResponse dispatchWithApi(http::verb method, const std::string& target,
                             const std::string& token) {
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        MemberType::Admin, "test-node", "test@test.com");
    PeerManager peerManager(ioc, localMember);

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

    auto req = makeRequest(method, target, token);
    HttpResponse res;
    api.handleRequest(req, res);
    return res;
}

} // namespace

// ── Property tests ─────────────────────────────────────────────────────────

#if HAS_JWT_CPP

// Property 23a: Admin/System get 200 on admin endpoints.
RC_GTEST_PROP(RestAdminAccessProperty,
              AdminOrSystemGets200OnAdminEndpoints,
              ()) {
    auto memberType = *rc::gen::element(MemberType::Admin, MemberType::System);
    auto memberId = *genId();
    auto endpoint = adminEndpointFromIndex(*rc::gen::inRange(0, 2));

    auto token = createTestJwt(memberId, memberType);
    auto res = dispatchWithApi(endpoint.method, endpoint.path, token);

    RC_ASSERT(res.result() == http::status::ok);

    auto body = nlohmann::json::parse(res.body());
    RC_ASSERT(body.contains("message"));
    RC_ASSERT(body["message"] == "Success");
    RC_ASSERT(body.contains("data"));
}

// Property 23b: User/Anonymous get 403 on admin endpoints.
RC_GTEST_PROP(RestAdminAccessProperty,
              UserOrAnonymousGets403OnAdminEndpoints,
              ()) {
    auto memberType = *rc::gen::element(MemberType::User, MemberType::Anonymous);
    auto memberId = *genId();
    auto endpoint = adminEndpointFromIndex(*rc::gen::inRange(0, 2));

    // Even with random extra permissions, non-admin types are forbidden.
    std::vector<std::string> permissions;
    int extraPerms = *rc::gen::inRange(0, 5);
    for (int i = 0; i < extraPerms; ++i) {
        permissions.push_back("pool:" + *genId() + ":read");
    }

    auto token = createTestJwt(memberId, memberType, permissions);
    auto res = dispatchWithApi(endpoint.method, endpoint.path, token);

    RC_ASSERT(res.result() == http::status::forbidden);

    auto body = nlohmann::json::parse(res.body());
    RC_ASSERT(body["message"] == "Forbidden");
}

// Property 23c: Universal — admin access matches isAdminOrSystem for all types.
RC_GTEST_PROP(RestAdminAccessProperty,
              AdminAccessMatchesMemberTypeCheck,
              ()) {
    auto memberType = *genMemberType();
    auto memberId = *genId();
    auto endpoint = adminEndpointFromIndex(*rc::gen::inRange(0, 2));

    auto token = createTestJwt(memberId, memberType);
    auto res = dispatchWithApi(endpoint.method, endpoint.path, token);

    bool isAdmin = (memberType == MemberType::Admin ||
                    memberType == MemberType::System);

    if (isAdmin) {
        RC_ASSERT(res.result() == http::status::ok);
    } else {
        RC_ASSERT(res.result() == http::status::forbidden);
    }
}

// Property 23d: Non-admin endpoint (/status) accessible by all member types.
RC_GTEST_PROP(RestAdminAccessProperty,
              StatusEndpointAccessibleByAllMemberTypes,
              ()) {
    auto memberType = *genMemberType();
    auto memberId = *genId();

    auto token = createTestJwt(memberId, memberType);
    auto res = dispatchWithApi(http::verb::get, "/api/introspection/status", token);

    RC_ASSERT(res.result() == http::status::ok);
}

#endif // HAS_JWT_CPP
