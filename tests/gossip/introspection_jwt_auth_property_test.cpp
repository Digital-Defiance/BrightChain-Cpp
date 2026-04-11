// Feature: cpp-gossip-protocol
// Task 18.3: Property 24 — REST JWT authentication
// **Validates: Requirements 11.6**
//
// For any introspection endpoint, requests with missing, expired, or
// malformed JWT tokens SHALL receive HTTP 401 Unauthorized responses.

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

/// Build an HTTP request with the given method, target, and optional raw
/// Authorization header value (not prefixed with "Bearer " automatically).
HttpRequest makeRawRequest(http::verb method, const std::string& target,
                           const std::string& authHeader = "") {
    HttpRequest req{method, target, 11};
    req.set(http::field::host, "localhost");
    if (!authHeader.empty()) {
        req.set(http::field::authorization, authHeader);
    }
    return req;
}

/// All introspection endpoints as (method, path) pairs.
struct Endpoint {
    http::verb method;
    std::string path;
};

Endpoint endpointFromIndex(int idx) {
    switch (idx) {
    case 0:  return {http::verb::get,  "/api/introspection/status"};
    case 1:  return {http::verb::get,  "/api/introspection/peers"};
    case 2:  return {http::verb::get,  "/api/introspection/pools"};
    case 3:  return {http::verb::get,  "/api/introspection/stats"};
    default: return {http::verb::post, "/api/introspection/discover-pools"};
    }
}

/// Create the IntrospectionApi stack and dispatch a request.
HttpResponse dispatchWithApi(const HttpRequest& req) {
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

    HttpResponse res;
    api.handleRequest(req, res);
    return res;
}

// ── Generators ─────────────────────────────────────────────────────────────

/// Generate a random printable string (no whitespace) for malformed tokens.
rc::Gen<std::string> genGarbage() {
    return rc::gen::nonEmpty(
        rc::gen::container<std::string>(
            rc::gen::inRange(static_cast<char>('!'), static_cast<char>('~' + 1))));
}

/// Generate an endpoint index covering all 5 introspection endpoints.
rc::Gen<int> genEndpointIndex() {
    return rc::gen::inRange(0, 5);
}

} // namespace

// ── Property tests ─────────────────────────────────────────────────────────

// Property 24a: Missing Authorization header → 401 on every endpoint.
RC_GTEST_PROP(RestJwtAuthProperty,
              MissingTokenReturns401OnAllEndpoints,
              ()) {
    auto ep = endpointFromIndex(*genEndpointIndex());
    auto req = makeRawRequest(ep.method, ep.path); // no auth header

    auto res = dispatchWithApi(req);

    RC_ASSERT(res.result() == http::status::unauthorized);
    auto body = nlohmann::json::parse(res.body());
    RC_ASSERT(body["message"] == "Unauthorized");
}

// Property 24b: Empty Bearer token → 401 on every endpoint.
RC_GTEST_PROP(RestJwtAuthProperty,
              EmptyBearerTokenReturns401OnAllEndpoints,
              ()) {
    auto ep = endpointFromIndex(*genEndpointIndex());
    auto req = makeRawRequest(ep.method, ep.path, "Bearer ");

    auto res = dispatchWithApi(req);

    RC_ASSERT(res.result() == http::status::unauthorized);
}

// Property 24c: Random garbage token → 401 on every endpoint.
RC_GTEST_PROP(RestJwtAuthProperty,
              MalformedTokenReturns401OnAllEndpoints,
              ()) {
    auto ep = endpointFromIndex(*genEndpointIndex());
    auto garbage = *genGarbage();
    auto req = makeRawRequest(ep.method, ep.path, "Bearer " + garbage);

    auto res = dispatchWithApi(req);

    RC_ASSERT(res.result() == http::status::unauthorized);
}

#if HAS_JWT_CPP

// Property 24d: Expired token → 401 on every endpoint.
RC_GTEST_PROP(RestJwtAuthProperty,
              ExpiredTokenReturns401OnAllEndpoints,
              ()) {
    auto ep = endpointFromIndex(*genEndpointIndex());

    // Generate a random member type — even Admin tokens must be rejected when expired.
    auto memberType = *rc::gen::element(
        MemberType::Admin, MemberType::System,
        MemberType::User, MemberType::Anonymous);

    // Expire between 1 second and 24 hours ago.
    int secondsAgo = *rc::gen::inRange(1, 86400);
    auto expiresAt = std::chrono::system_clock::now()
                     - std::chrono::seconds(secondsAgo);

    auto token = jwt::create()
        .set_issuer("brightchain")
        .set_type("JWT")
        .set_payload_claim("memberId", jwt::claim(std::string("member-1")))
        .set_payload_claim("memberType",
                           jwt::claim(std::to_string(static_cast<int>(memberType))))
        .set_expires_at(expiresAt)
        .sign(jwt::algorithm::hs256{kJwtSecret});

    auto req = makeRawRequest(ep.method, ep.path, "Bearer " + token);
    auto res = dispatchWithApi(req);

    RC_ASSERT(res.result() == http::status::unauthorized);
}

// Property 24e: Token signed with wrong secret → 401 on every endpoint.
RC_GTEST_PROP(RestJwtAuthProperty,
              WrongSecretTokenReturns401OnAllEndpoints,
              ()) {
    auto ep = endpointFromIndex(*genEndpointIndex());

    auto token = jwt::create()
        .set_issuer("brightchain")
        .set_type("JWT")
        .set_payload_claim("memberId", jwt::claim(std::string("admin-1")))
        .set_payload_claim("memberType",
                           jwt::claim(std::to_string(static_cast<int>(MemberType::Admin))))
        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(1))
        .sign(jwt::algorithm::hs256{"completely-wrong-secret-key-9999"});

    auto req = makeRawRequest(ep.method, ep.path, "Bearer " + token);
    auto res = dispatchWithApi(req);

    RC_ASSERT(res.result() == http::status::unauthorized);
}

// Property 24f: Authorization header without "Bearer " prefix → 401.
RC_GTEST_PROP(RestJwtAuthProperty,
              NonBearerSchemeReturns401OnAllEndpoints,
              ()) {
    auto ep = endpointFromIndex(*genEndpointIndex());

    // Create a valid token but use a wrong auth scheme.
    auto token = jwt::create()
        .set_issuer("brightchain")
        .set_type("JWT")
        .set_payload_claim("memberId", jwt::claim(std::string("admin-1")))
        .set_payload_claim("memberType",
                           jwt::claim(std::to_string(static_cast<int>(MemberType::Admin))))
        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(1))
        .sign(jwt::algorithm::hs256{kJwtSecret});

    // Use "Basic" scheme instead of "Bearer".
    auto req = makeRawRequest(ep.method, ep.path, "Basic " + token);
    auto res = dispatchWithApi(req);

    RC_ASSERT(res.result() == http::status::unauthorized);
}

#endif // HAS_JWT_CPP
