// Feature: cpp-gossip-protocol
// Property 29: Peer registry completeness  **Validates: Requirements 3.2**
// Property 30: ECIES challenge/response round-trip  **Validates: Requirements 3.4, 14.5**
// Property 31: Peer reconnection backoff  **Validates: Requirements 3.5**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/ec_key_pair.hpp>
#include <brightchain/ecies.hpp>
#include <brightchain/member.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace brightchain::gossip;

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

/// Generate a non-empty alphanumeric string of length [1, maxLen].
rc::Gen<std::string> genNonEmptyString(int maxLen = 32) {
    return rc::gen::nonEmpty(
        rc::gen::container<std::string>(
            rc::gen::inRange(static_cast<char>('a'), static_cast<char>('z' + 1))));
}

/// Generate a random capability string.
rc::Gen<std::string> genCapability() {
    return rc::gen::element(
        std::string("blocks"),
        std::string("pools"),
        std::string("gossip"),
        std::string("discovery"),
        std::string("quorum"));
}

/// Generate a 33-byte compressed secp256k1-style public key (random bytes).
rc::Gen<std::vector<uint8_t>> genPublicKey() {
    return rc::gen::exec([]() {
        auto bytes = *rc::gen::container<std::vector<uint8_t>>(
            33, rc::gen::inRange<uint8_t>(0, 255));
        // Compressed secp256k1 keys start with 0x02 or 0x03.
        bytes[0] = *rc::gen::element(static_cast<uint8_t>(0x02), static_cast<uint8_t>(0x03));
        return bytes;
    });
}

/// Generate a complete PeerInfo with all fields populated.
rc::Gen<PeerInfo> genPeerInfo() {
    return rc::gen::exec([]() {
        PeerInfo info;
        info.nodeId = *genNonEmptyString(48);
        info.address = *rc::gen::exec([]() {
            int a = *rc::gen::inRange(1, 256);
            int b = *rc::gen::inRange(0, 256);
            int c = *rc::gen::inRange(0, 256);
            int d = *rc::gen::inRange(1, 256);
            return std::to_string(a) + "." + std::to_string(b) + "."
                 + std::to_string(c) + "." + std::to_string(d);
        });
        info.httpPort = *rc::gen::inRange<uint16_t>(1024, 65535);
        info.wsPort = *rc::gen::inRange<uint16_t>(1024, 65535);
        info.lastSeen = "2025-01-28T12:00:00.000Z"; // fixed ISO 8601 for simplicity
        info.capabilities = *rc::gen::container<std::vector<std::string>>(
            *rc::gen::inRange(1, 6), genCapability());
        info.connected = *rc::gen::arbitrary<bool>();
        info.latencyMs = static_cast<double>(*rc::gen::inRange(1, 10000)) / 10.0;
        info.publicKey = *genPublicKey();
        return info;
    });
}

} // namespace

// ── Property 29: Peer registry completeness ────────────────────────────────
// For any PeerInfo with all fields populated, adding the peer to the registry
// and then retrieving it by nodeId shall return a PeerInfo that is equal to
// the original (all fields preserved).

RC_GTEST_PROP(PeerRegistryCompleteness,
              AddedPeerIsRetrievedWithAllFields,
              ()) {
    PeerInfo original = *genPeerInfo();

    // Ensure nodeId is unique (RapidCheck generates fresh values each run).
    RC_PRE(!original.nodeId.empty());

    // Create a minimal PeerManager with a dummy io_context and Member.
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);

    // Add the peer via the testing helper.
    pm.addPeer(original);

    // Retrieve by nodeId.
    auto retrieved = pm.getPeer(original.nodeId);
    RC_ASSERT(retrieved.has_value());
    RC_ASSERT(retrieved.value() == original);
}

// ── Property 29b: Multiple peers all retrievable ───────────────────────────
// For any set of N peers (1–20) with distinct nodeIds, adding all of them
// and then retrieving each by nodeId shall return the exact PeerInfo that
// was added.

RC_GTEST_PROP(PeerRegistryCompleteness,
              MultiplePeersAllRetrievable,
              ()) {
    const int peerCount = *rc::gen::inRange(1, 21);

    // Generate peers with guaranteed-unique nodeIds.
    std::vector<PeerInfo> peers;
    peers.reserve(peerCount);
    for (int i = 0; i < peerCount; ++i) {
        PeerInfo p = *genPeerInfo();
        p.nodeId = "peer-" + std::to_string(i) + "-" + p.nodeId;
        peers.push_back(std::move(p));
    }

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);

    // Add all peers.
    for (const auto& peer : peers) {
        pm.addPeer(peer);
    }

    // Verify each peer is retrievable with complete data.
    for (const auto& peer : peers) {
        auto retrieved = pm.getPeer(peer.nodeId);
        RC_ASSERT(retrieved.has_value());
        RC_ASSERT(retrieved.value() == peer);
    }

    // Verify connected peer count matches.
    auto connectedPeers = pm.getConnectedPeers();
    int expectedConnected = 0;
    for (const auto& p : peers) {
        if (p.connected) ++expectedConnected;
    }
    RC_ASSERT(static_cast<int>(connectedPeers.size()) == expectedConnected);

    // Verify connected peer IDs match.
    auto connectedIds = pm.getConnectedPeerIds();
    RC_ASSERT(static_cast<int>(connectedIds.size()) == expectedConnected);
    for (const auto& id : connectedIds) {
        auto it = std::find_if(peers.begin(), peers.end(),
            [&id](const PeerInfo& p) { return p.nodeId == id && p.connected; });
        RC_ASSERT(it != peers.end());
    }
}

// ── Property 29c: Non-existent peer returns nullopt ────────────────────────
// For any nodeId that was never added, getPeer shall return std::nullopt.

RC_GTEST_PROP(PeerRegistryCompleteness,
              NonExistentPeerReturnsNullopt,
              ()) {
    std::string randomId = *genNonEmptyString(48);

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);

    auto result = pm.getPeer(randomId);
    RC_ASSERT(!result.has_value());
}


// ── Property 30: ECIES challenge/response round-trip ───────────────────────
// For any randomly generated EcKeyPair and any random challenge bytes,
// encrypting the challenge with the public key and decrypting with the
// private key shall produce the original challenge bytes.
// **Validates: Requirements 3.4, 14.5**

// ── 30a: Direct ECIES encrypt→decrypt round-trip ───────────────────────────
// Generate random EcKeyPairs and random challenge byte vectors; verify that
// Ecies::encryptBasic followed by Ecies::decrypt recovers the original bytes.

RC_GTEST_PROP(EciesChallengeResponseRoundTrip,
              EncryptDecryptProducesOriginalBytes,
              ()) {
    // Generate a fresh secp256k1 key pair.
    auto keyPair = brightchain::EcKeyPair::generate();
    auto pubKey = keyPair.publicKey();

    // Generate a random challenge of 1–256 bytes.
    int len = *rc::gen::inRange(1, 257);
    auto challenge = *rc::gen::container<std::vector<uint8_t>>(
        static_cast<size_t>(len), rc::gen::inRange<uint8_t>(0, 255));

    // Encrypt with the public key.
    auto encrypted = brightchain::Ecies::encryptBasic(challenge, pubKey);

    // Encrypted output must be larger than the plaintext.
    RC_ASSERT(encrypted.size() > challenge.size());

    // Decrypt with the private key.
    auto decrypted = brightchain::Ecies::decrypt(encrypted, keyPair);

    // Round-trip must recover the original challenge.
    RC_ASSERT(decrypted == challenge);
}

// ── 30b: PeerManager::authenticateConnection round-trip ────────────────────
// Simulate the full ECIES challenge/response authentication flow used by
// PeerManager: the verifier encrypts a random challenge with the peer's
// public key, the peer decrypts with their private key, and the verifier
// checks that the decrypted bytes match the original challenge.

RC_GTEST_PROP(EciesChallengeResponseRoundTrip,
              AuthenticateConnectionSucceedsWithCorrectKey,
              ()) {
    // Create a PeerManager with a local member.
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "verifier-node", "[email]");

    PeerManager pm(ioc, localMember);

    // Generate a peer key pair.
    auto peerKeyPair = brightchain::EcKeyPair::generate();
    auto peerPubKey = peerKeyPair.publicKey();

    // The peer's decrypt function: decrypt using the peer's private key.
    // This is a move-capture because EcKeyPair is move-only.
    auto peerDecryptFn = [&peerKeyPair](const std::vector<uint8_t>& encryptedChallenge)
        -> std::vector<uint8_t> {
        return brightchain::Ecies::decrypt(encryptedChallenge, peerKeyPair);
    };

    // Authentication must succeed when the peer holds the correct private key.
    bool result = pm.authenticateConnection(peerPubKey, peerDecryptFn);
    RC_ASSERT(result);
}

// ── 30c: Authentication fails with wrong key ───────────────────────────────
// If the peer attempts to decrypt with a different private key, the
// challenge/response must fail.

RC_GTEST_PROP(EciesChallengeResponseRoundTrip,
              AuthenticateConnectionFailsWithWrongKey,
              ()) {
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "verifier-node", "[email]");

    PeerManager pm(ioc, localMember);

    // Generate the real peer key pair and a different (wrong) key pair.
    auto realKeyPair = brightchain::EcKeyPair::generate();
    auto wrongKeyPair = brightchain::EcKeyPair::generate();
    auto realPubKey = realKeyPair.publicKey();

    // The "peer" tries to decrypt with the wrong private key.
    auto wrongDecryptFn = [&wrongKeyPair](const std::vector<uint8_t>& encryptedChallenge)
        -> std::vector<uint8_t> {
        // This will either throw or return wrong bytes.
        return brightchain::Ecies::decrypt(encryptedChallenge, wrongKeyPair);
    };

    // Authentication must fail.
    bool result = pm.authenticateConnection(realPubKey, wrongDecryptFn);
    RC_ASSERT(!result);
}

// ── 30d: Empty challenge edge case ─────────────────────────────────────────
// ECIES must handle encrypting and decrypting an empty payload correctly.

TEST(EciesChallengeResponseRoundTrip, EmptyPayloadRoundTrip) {
    auto keyPair = brightchain::EcKeyPair::generate();
    auto pubKey = keyPair.publicKey();

    std::vector<uint8_t> emptyChallenge;
    auto encrypted = brightchain::Ecies::encryptBasic(emptyChallenge, pubKey);
    auto decrypted = brightchain::Ecies::decrypt(encrypted, keyPair);

    EXPECT_EQ(decrypted, emptyChallenge);
}


// ── Property 31: Peer reconnection backoff ─────────────────────────────────
// For any reconnection attempt number n >= 0, the computed delay shall equal
// min(1 * 2^n, 60) seconds.  Negative attempt numbers shall return 1.
// **Validates: Requirements 3.5**

// ── 31a: Delay matches min(2^n, 60) for non-negative attempts ─────────────
// We limit to [0, 30] to stay within well-defined int shift behavior.

RC_GTEST_PROP(PeerReconnectionBackoff,
              DelayMatchesFormula,
              ()) {
    int attempt = *rc::gen::inRange(0, 31);

    int actual = PeerManager::calculateReconnectDelay(attempt);

    int expected = std::min(1 << attempt, 60);

    RC_ASSERT(actual == expected);
}

// ── 31b: Delay is always in [1, 60] ───────────────────────────────────────

RC_GTEST_PROP(PeerReconnectionBackoff,
              DelayAlwaysInRange,
              ()) {
    int attempt = *rc::gen::inRange(-10, 31);

    int delay = PeerManager::calculateReconnectDelay(attempt);

    RC_ASSERT(delay >= 1);
    RC_ASSERT(delay <= 60);
}

// ── 31c: Delay is monotonically non-decreasing ────────────────────────────

RC_GTEST_PROP(PeerReconnectionBackoff,
              DelayIsMonotonicallyNonDecreasing,
              ()) {
    int attempt = *rc::gen::inRange(0, 30);

    int delayN = PeerManager::calculateReconnectDelay(attempt);
    int delayN1 = PeerManager::calculateReconnectDelay(attempt + 1);

    RC_ASSERT(delayN1 >= delayN);
}

// ── 31d: Delay caps at exactly 60 seconds ──────────────────────────────────

RC_GTEST_PROP(PeerReconnectionBackoff,
              DelayCapsAtSixtySeconds,
              ()) {
    // For attempt >= 6, 2^6 = 64 > 60, so delay must be capped at 60.
    int attempt = *rc::gen::inRange(6, 31);

    int delay = PeerManager::calculateReconnectDelay(attempt);

    RC_ASSERT(delay == 60);
}

// ── 31e: Known values spot-check ───────────────────────────────────────────

TEST(PeerReconnectionBackoff, KnownValues) {
    // attempt 0 → min(1, 60) = 1
    EXPECT_EQ(PeerManager::calculateReconnectDelay(0), 1);
    // attempt 1 → min(2, 60) = 2
    EXPECT_EQ(PeerManager::calculateReconnectDelay(1), 2);
    // attempt 2 → min(4, 60) = 4
    EXPECT_EQ(PeerManager::calculateReconnectDelay(2), 4);
    // attempt 3 → min(8, 60) = 8
    EXPECT_EQ(PeerManager::calculateReconnectDelay(3), 8);
    // attempt 4 → min(16, 60) = 16
    EXPECT_EQ(PeerManager::calculateReconnectDelay(4), 16);
    // attempt 5 → min(32, 60) = 32
    EXPECT_EQ(PeerManager::calculateReconnectDelay(5), 32);
    // attempt 6 → min(64, 60) = 60
    EXPECT_EQ(PeerManager::calculateReconnectDelay(6), 60);
    // attempt 10 → min(1024, 60) = 60
    EXPECT_EQ(PeerManager::calculateReconnectDelay(10), 60);
    // negative → 1
    EXPECT_EQ(PeerManager::calculateReconnectDelay(-1), 1);
    EXPECT_EQ(PeerManager::calculateReconnectDelay(-100), 1);
}
