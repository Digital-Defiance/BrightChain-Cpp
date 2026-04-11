// Feature: cpp-gossip-protocol
// Task 13.3: Property test — Head update signature verification
// **Property 17: Head update signature verification**
// Generate writeProofs with valid and invalid signatures; verify accept/reject behavior
// **Validates: Requirements 7.3**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/head_registry.hpp>
#include <brightchain/member.hpp>

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace brightchain::gossip;

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        auto tmp = std::filesystem::temp_directory_path() / "gossip_head_sig_test";
        tmp /= std::to_string(std::rand()) + "_" +
               std::to_string(reinterpret_cast<uintptr_t>(this));
        std::filesystem::create_directories(tmp);
        path = tmp;
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::string toHex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : bytes) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

void addConnectedPeers(PeerManager& pm, int count) {
    for (int i = 0; i < count; ++i) {
        PeerInfo info;
        info.nodeId = "peer-" + std::to_string(i);
        info.address = "127.0.0.1";
        info.httpPort = static_cast<uint16_t>(3000 + i);
        info.wsPort = static_cast<uint16_t>(4000 + i);
        info.lastSeen = "2025-01-28T12:00:00.000Z";
        info.connected = true;
        info.latencyMs = 10.0;
        pm.addPeer(info);
    }
}

/// Create a WriteProof by signing dbName + collectionName + blockId with the given member.
WriteProof makeWriteProof(const brightchain::Member& signer,
                          const std::string& dbName,
                          const std::string& collectionName,
                          const std::string& blockId) {
    std::string signedData = dbName + collectionName + blockId;
    std::vector<uint8_t> dataBytes(signedData.begin(), signedData.end());
    auto signature = signer.sign(dataBytes);

    WriteProof proof;
    proof.signerPublicKey = toHex(signer.publicKey());
    proof.signature = toHex(signature);
    proof.dbName = dbName;
    proof.collectionName = collectionName;
    proof.blockId = blockId;
    return proof;
}

/// Build a head_update announcement.
BlockAnnouncement makeHeadUpdateAnnouncement(
    const std::string& dbName,
    const std::string& collectionName,
    const std::string& blockId,
    int ttl,
    std::optional<WriteProof> proof = std::nullopt,
    const std::string& nodeId = "remote-node-001") {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::HeadUpdate;
    ann.blockId = blockId;
    ann.nodeId = nodeId;
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = ttl;
    ann.headUpdate = HeadUpdateMetadata{dbName, collectionName};
    ann.writeProof = std::move(proof);
    return ann;
}

/// Corrupt a hex signature string by flipping characters.
std::string corruptHexSignature(const std::string& hexSig) {
    if (hexSig.size() < 4) return "deadbeef";
    std::string corrupted = hexSig;
    // Flip several characters to ensure the signature is invalid
    for (size_t i = 0; i < std::min<size_t>(6, corrupted.size()); i += 2) {
        corrupted[i] = (corrupted[i] == 'a') ? 'b' : 'a';
        if (i + 1 < corrupted.size()) {
            corrupted[i + 1] = (corrupted[i + 1] == '0') ? '1' : '0';
        }
    }
    return corrupted;
}

/// Generator for non-empty alphanumeric strings (safe for JSON serialization).
rc::Gen<std::string> genAlphaNum() {
    return rc::gen::map(
        rc::gen::container<std::string>(
            rc::gen::inRange('a', static_cast<char>('z' + 1))),
        [](std::string s) {
            if (s.empty()) s = "a";
            return s;
        });
}

} // namespace

// ── Property 17a: Valid signature → head update accepted ──────────────────
// For any randomly generated (dbName, collectionName, blockId) triple,
// a head_update announcement with a correctly signed WriteProof shall be
// accepted and applied to the HeadRegistry.

RC_GTEST_PROP(HeadUpdateSignatureVerification,
              ValidSignatureAccepted,
              ()) {
    // Generate non-empty alphanumeric strings (safe for JSON round-trip)
    auto dbName = *genAlphaNum();
    auto collectionName = *genAlphaNum();
    auto blockId = *genAlphaNum();

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipConfig config;
    config.fanout = 3;
    config.defaultTtl = 5;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);
    addConnectedPeers(pm, 5);

    // Generate a fresh signer and create a valid WriteProof
    auto signer = brightchain::Member::generate(
        brightchain::MemberType::User, "signer", "[email]");
    auto proof = makeWriteProof(signer, dbName, collectionName, blockId);

    auto ann = makeHeadUpdateAnnouncement(dbName, collectionName, blockId, 3, proof);
    engine.handleAnnouncement(ann);

    // The head should have been updated
    std::string key = dbName + ":" + collectionName;
    auto entry = headRegistry.getHead(key);
    RC_ASSERT(entry.has_value());
    RC_ASSERT(entry->blockId == blockId);
}

// ── Property 17b: Corrupted signature → head update rejected ──────────────
// For any randomly generated (dbName, collectionName, blockId) triple,
// a head_update announcement with a corrupted signature in the WriteProof
// shall be rejected and NOT applied to the HeadRegistry.

RC_GTEST_PROP(HeadUpdateSignatureVerification,
              CorruptedSignatureRejected,
              ()) {
    auto dbName = *genAlphaNum();
    auto collectionName = *genAlphaNum();
    auto blockId = *genAlphaNum();

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipConfig config;
    config.fanout = 3;
    config.defaultTtl = 5;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);
    addConnectedPeers(pm, 5);

    // Create a valid proof, then corrupt the signature
    auto signer = brightchain::Member::generate(
        brightchain::MemberType::User, "signer", "[email]");
    auto proof = makeWriteProof(signer, dbName, collectionName, blockId);
    proof.signature = corruptHexSignature(proof.signature);

    auto ann = makeHeadUpdateAnnouncement(dbName, collectionName, blockId, 3, proof);
    engine.handleAnnouncement(ann);

    // The head should NOT have been updated
    std::string key = dbName + ":" + collectionName;
    auto entry = headRegistry.getHead(key);
    RC_ASSERT(!entry.has_value());
}

// ── Property 17c: Wrong signer key → head update rejected ─────────────────
// For any randomly generated (dbName, collectionName, blockId) triple,
// signing with one key but putting a different key in signerPublicKey
// shall cause the head update to be rejected.

RC_GTEST_PROP(HeadUpdateSignatureVerification,
              WrongSignerKeyRejected,
              ()) {
    auto dbName = *genAlphaNum();
    auto collectionName = *genAlphaNum();
    auto blockId = *genAlphaNum();

    TempDir storeTmp;
    TempDir headTmp;

    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "test-node", "[email]");

    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipConfig config;
    config.fanout = 3;
    config.defaultTtl = 5;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);
    addConnectedPeers(pm, 5);

    // Sign with one member but attribute to a different member's public key
    auto actualSigner = brightchain::Member::generate(
        brightchain::MemberType::User, "actual-signer", "[email]");
    auto wrongSigner = brightchain::Member::generate(
        brightchain::MemberType::User, "wrong-signer", "[email]");

    std::string signedData = dbName + collectionName + blockId;
    std::vector<uint8_t> dataBytes(signedData.begin(), signedData.end());
    auto signature = actualSigner.sign(dataBytes);

    WriteProof proof;
    proof.signerPublicKey = toHex(wrongSigner.publicKey()); // wrong key
    proof.signature = toHex(signature);
    proof.dbName = dbName;
    proof.collectionName = collectionName;
    proof.blockId = blockId;

    auto ann = makeHeadUpdateAnnouncement(dbName, collectionName, blockId, 3, proof);
    engine.handleAnnouncement(ann);

    // The head should NOT have been updated
    std::string key = dbName + ":" + collectionName;
    auto entry = headRegistry.getHead(key);
    RC_ASSERT(!entry.has_value());
}
