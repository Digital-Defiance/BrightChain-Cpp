// Feature: cpp-gossip-protocol
// Task 17.2: Property 22 — WebSocket event access tier filtering
// **Validates: Requirements 10.5**
//
// For any WebSocket event with an access tier and any client with a MemberType,
// isClientAuthorizedForEvent() returns true if and only if:
//   - Admin events: client is Admin or System
//   - PoolScoped events: client is Admin/System, or has pool:<poolId>:read permission
//   - MemberScoped events: client.memberId == event.targetMemberId

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/websocket_server.hpp>
#include <brightchain/member.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace brightchain::gossip;
using brightchain::MemberType;

// ── Generators ─────────────────────────────────────────────────────────────

namespace {

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

rc::Gen<EventAccessTier> genAccessTier() {
    return rc::gen::element(
        EventAccessTier::Admin,
        EventAccessTier::PoolScoped,
        EventAccessTier::MemberScoped);
}

/// Build JwtClaims with the given member type, id, and permissions.
JwtClaims makeClaims(const std::string& memberId,
                     MemberType memberType,
                     const std::vector<std::string>& permissions = {}) {
    JwtClaims c;
    c.memberId = memberId;
    c.memberType = memberType;
    c.permissions = permissions;
    c.expiresAt = std::chrono::system_clock::now() + std::chrono::hours(1);
    return c;
}

/// Compute the expected authorization result from first principles.
bool expectedAuthorization(const JwtClaims& claims,
                           const WebSocketEvent& event) {
    switch (event.accessTier) {
    case EventAccessTier::Admin:
        return claims.memberType == MemberType::Admin ||
               claims.memberType == MemberType::System;

    case EventAccessTier::PoolScoped:
        if (claims.memberType == MemberType::Admin ||
            claims.memberType == MemberType::System) {
            return true;
        }
        if (!event.targetPoolId.has_value()) {
            return false;
        }
        {
            const std::string perm =
                "pool:" + event.targetPoolId.value() + ":read";
            return std::find(claims.permissions.begin(),
                             claims.permissions.end(),
                             perm) != claims.permissions.end();
        }

    case EventAccessTier::MemberScoped:
        if (!event.targetMemberId.has_value()) {
            return false;
        }
        return claims.memberId == event.targetMemberId.value();
    }
    return false;
}

} // namespace

// ── Property 22a: Admin events delivered only to Admin/System ──────────────

RC_GTEST_PROP(WebSocketAccessTierProperty,
              AdminEventsOnlyForAdminOrSystem,
              ()) {
    auto memberType = *genMemberType();
    auto memberId = *genId();

    JwtClaims claims = makeClaims(memberId, memberType);

    // Pick a concrete admin event.
    auto eventName = *rc::gen::element(
        std::string("peer:connected"),
        std::string("peer:disconnected"),
        std::string("storage:alert"));

    WebSocketEvent event;
    event.event = eventName;
    event.accessTier = EventAccessTier::Admin;
    event.data = "{}";

    bool actual = isClientAuthorizedForEvent(claims, event);
    bool expected = (memberType == MemberType::Admin ||
                     memberType == MemberType::System);

    RC_ASSERT(actual == expected);
}

// ── Property 22b: PoolScoped events respect permission rules ───────────────

RC_GTEST_PROP(WebSocketAccessTierProperty,
              PoolScopedEventsRespectPermissions,
              ()) {
    auto memberType = *genMemberType();
    auto memberId = *genId();
    auto poolId = *genId();

    // Randomly decide whether the client has the read permission for this pool.
    bool hasReadPerm = *rc::gen::arbitrary<bool>();

    std::vector<std::string> permissions;
    if (hasReadPerm) {
        permissions.push_back("pool:" + poolId + ":read");
    }
    // Optionally add some unrelated permissions as noise.
    int extraPerms = *rc::gen::inRange(0, 4);
    for (int i = 0; i < extraPerms; ++i) {
        auto otherPool = *genId();
        permissions.push_back("pool:" + otherPool + ":write");
    }

    JwtClaims claims = makeClaims(memberId, memberType, permissions);

    auto eventName = *rc::gen::element(
        std::string("pool:changed"),
        std::string("pool:created"),
        std::string("pool:deleted"));

    WebSocketEvent event;
    event.event = eventName;
    event.accessTier = EventAccessTier::PoolScoped;
    event.targetPoolId = poolId;
    event.data = "{}";

    bool actual = isClientAuthorizedForEvent(claims, event);
    bool expected = expectedAuthorization(claims, event);

    RC_ASSERT(actual == expected);
}

// ── Property 22c: PoolScoped without targetPoolId always denied for non-admin

RC_GTEST_PROP(WebSocketAccessTierProperty,
              PoolScopedWithoutTargetPoolDeniedForNonAdmin,
              ()) {
    auto memberType = *rc::gen::element(MemberType::User, MemberType::Anonymous);
    auto memberId = *genId();

    JwtClaims claims = makeClaims(memberId, memberType);

    WebSocketEvent event;
    event.event = "pool:changed";
    event.accessTier = EventAccessTier::PoolScoped;
    // No targetPoolId set.
    event.data = "{}";

    RC_ASSERT(!isClientAuthorizedForEvent(claims, event));
}

// ── Property 22d: MemberScoped events delivered only to target member ──────

RC_GTEST_PROP(WebSocketAccessTierProperty,
              MemberScopedOnlyForTargetMember,
              ()) {
    auto memberType = *genMemberType();
    auto memberId = *genId();
    auto targetMemberId = *genId();

    JwtClaims claims = makeClaims(memberId, memberType);

    WebSocketEvent event;
    event.event = "energy:updated";
    event.accessTier = EventAccessTier::MemberScoped;
    event.targetMemberId = targetMemberId;
    event.data = "{}";

    bool actual = isClientAuthorizedForEvent(claims, event);
    // MemberScoped: only the exact target member receives the event,
    // regardless of Admin/System status.
    bool expected = (memberId == targetMemberId);

    RC_ASSERT(actual == expected);
}

// ── Property 22e: MemberScoped without targetMemberId always denied ────────

RC_GTEST_PROP(WebSocketAccessTierProperty,
              MemberScopedWithoutTargetAlwaysDenied,
              ()) {
    auto memberType = *genMemberType();
    auto memberId = *genId();

    JwtClaims claims = makeClaims(memberId, memberType);

    WebSocketEvent event;
    event.event = "energy:updated";
    event.accessTier = EventAccessTier::MemberScoped;
    // No targetMemberId set.
    event.data = "{}";

    RC_ASSERT(!isClientAuthorizedForEvent(claims, event));
}

// ── Property 22f: Universal — isClientAuthorizedForEvent matches spec ──────

RC_GTEST_PROP(WebSocketAccessTierProperty,
              UniversalAuthorizationMatchesSpec,
              ()) {
    auto memberType = *genMemberType();
    auto memberId = *genId();
    auto accessTier = *genAccessTier();

    // Generate context-appropriate optional fields.
    std::optional<std::string> targetPoolId;
    std::optional<std::string> targetMemberId;
    std::vector<std::string> permissions;

    if (accessTier == EventAccessTier::PoolScoped) {
        targetPoolId = *genId();
        if (*rc::gen::arbitrary<bool>()) {
            permissions.push_back("pool:" + *targetPoolId + ":read");
        }
    } else if (accessTier == EventAccessTier::MemberScoped) {
        targetMemberId = *genId();
    }

    JwtClaims claims = makeClaims(memberId, memberType, permissions);

    WebSocketEvent event;
    event.event = "test:event";
    event.accessTier = accessTier;
    event.targetPoolId = targetPoolId;
    event.targetMemberId = targetMemberId;
    event.data = "{}";

    bool actual = isClientAuthorizedForEvent(claims, event);
    bool expected = expectedAuthorization(claims, event);

    RC_ASSERT(actual == expected);
}
