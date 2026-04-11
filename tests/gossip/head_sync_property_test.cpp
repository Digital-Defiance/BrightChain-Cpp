// Feature: cpp-gossip-protocol
// Task 13.2: Property test — Head update announcement creation
// **Property 16: Head update announcement creation**
// Generate dbName/collectionName/blockId triples; verify announcement fields match
// **Validates: Requirements 7.1**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/head_registry.hpp>
#include <brightchain/member.hpp>

#include <algorithm>
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
        auto tmp = std::filesystem::temp_directory_path() / "gossip_head_prop_test";
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

} // namespace

// ── Property 16a: Basic head update announcement fields match inputs ──────
// For any randomly generated (dbName, collectionName, blockId) triple and
// defaultTtl, calling announceHeadUpdate produces a pending announcement
// with correct type, blockId, headUpdate metadata, ttl, nodeId, and no
// writeProof.

RC_GTEST_PROP(HeadUpdateAnnouncementCreation,
              FieldsMatchInputs,
              ()) {
    auto dbName = *rc::gen::nonEmpty<std::string>();
    auto collectionName = *rc::gen::nonEmpty<std::string>();
    auto blockId = *rc::gen::nonEmpty<std::string>();
    int defaultTtl = *rc::gen::inRange(1, 21);

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
    config.defaultTtl = defaultTtl;

    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);
    addConnectedPeers(pm, 5);

    engine.announceHeadUpdate(dbName, collectionName, blockId);

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(pending.size() == 1);

    const auto& ann = pending[0];
    RC_ASSERT(ann.type == AnnouncementType::HeadUpdate);
    RC_ASSERT(ann.blockId == blockId);
    RC_ASSERT(ann.ttl == defaultTtl);
    RC_ASSERT(ann.nodeId == localMember.idHex());
    RC_ASSERT(ann.headUpdate.has_value());
    RC_ASSERT(ann.headUpdate->dbName == dbName);
    RC_ASSERT(ann.headUpdate->collectionName == collectionName);
    RC_ASSERT(!ann.writeProof.has_value());
}


// ── Property 16b: Head update with WriteProof preserves proof fields ──────
// For any randomly generated (dbName, collectionName, blockId) triple,
// creating a WriteProof and calling announceHeadUpdate with it produces
// a pending announcement that includes the writeProof with matching fields.

RC_GTEST_PROP(HeadUpdateAnnouncementCreation,
              WithWriteProofPreservesFields,
              ()) {
    auto dbName = *rc::gen::nonEmpty<std::string>();
    auto collectionName = *rc::gen::nonEmpty<std::string>();
    auto blockId = *rc::gen::nonEmpty<std::string>();

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

    // Create a WriteProof by signing dbName + collectionName + blockId
    auto signer = brightchain::Member::generate(
        brightchain::MemberType::User, "signer", "[email]");
    std::string signedData = dbName + collectionName + blockId;
    std::vector<uint8_t> dataBytes(signedData.begin(), signedData.end());
    auto signature = signer.sign(dataBytes);

    WriteProof proof;
    proof.signerPublicKey = toHex(signer.publicKey());
    proof.signature = toHex(signature);
    proof.dbName = dbName;
    proof.collectionName = collectionName;
    proof.blockId = blockId;

    engine.announceHeadUpdate(dbName, collectionName, blockId, proof);

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(pending.size() == 1);

    const auto& ann = pending[0];
    RC_ASSERT(ann.type == AnnouncementType::HeadUpdate);
    RC_ASSERT(ann.blockId == blockId);
    RC_ASSERT(ann.headUpdate.has_value());
    RC_ASSERT(ann.headUpdate->dbName == dbName);
    RC_ASSERT(ann.headUpdate->collectionName == collectionName);
    RC_ASSERT(ann.writeProof.has_value());
    RC_ASSERT(ann.writeProof->dbName == dbName);
    RC_ASSERT(ann.writeProof->collectionName == collectionName);
    RC_ASSERT(ann.writeProof->blockId == blockId);
    RC_ASSERT(ann.writeProof->signerPublicKey == toHex(signer.publicKey()));
    RC_ASSERT(ann.writeProof->signature == toHex(signature));
}

// ── Property 16c: Exactly one announcement is queued per call ─────────────
// For any random count (1–5) of head update calls with different inputs,
// exactly that many pending announcements exist, all of type HeadUpdate.

RC_GTEST_PROP(HeadUpdateAnnouncementCreation,
              ExactlyOneAnnouncementPerCall,
              ()) {
    int count = *rc::gen::inRange(1, 6);

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

    for (int i = 0; i < count; ++i) {
        std::string dbName = "db-" + std::to_string(i);
        std::string collectionName = "col-" + std::to_string(i);
        std::string blockId = "block-" + std::to_string(i);
        engine.announceHeadUpdate(dbName, collectionName, blockId);
    }

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(static_cast<int>(pending.size()) == count);

    for (const auto& ann : pending) {
        RC_ASSERT(ann.type == AnnouncementType::HeadUpdate);
    }
}
