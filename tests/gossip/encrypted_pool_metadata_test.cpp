// Feature: cpp-gossip-protocol
// Property 19: Encrypted pool metadata in announcements
// **Validates: Requirements 8.5**
//
// For any pool with encrypted == true, announcePoolCreation shall produce
// an announcement whose encryptedMetadata is non-empty and decryptable by
// the authorized member (the local node that created the announcement).
// The decrypted JSON must contain the original blockCount, totalSize, and
// encrypted fields.

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/head_registry.hpp>
#include <brightchain/member.hpp>
#include <brightchain/ecies.hpp>
#include <brightchain/ec_key_pair.hpp>

#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace brightchain::gossip;

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        auto tmp = std::filesystem::temp_directory_path() / "gossip_enc_pool_prop_test";
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

/// Simple base64 decoder for test verification.
std::vector<uint8_t> base64Decode(const std::string& encoded) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> result;
    int val = 0, bits = -8;
    for (char c : encoded) {
        if (c == '=') break;
        auto pos = chars.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + static_cast<int>(pos);
        bits += 6;
        if (bits >= 0) {
            result.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return result;
}

} // namespace

// ── Property 19a: Encrypted pool metadata is non-empty and decryptable ────
// For any pool with encrypted == true and random blockCount/totalSize,
// the announcement's encryptedMetadata shall be non-empty and decryptable
// by the local node, yielding the original metadata values.

RC_GTEST_PROP(EncryptedPoolMetadata,
              EncryptedMetadataIsDecryptableByAuthorizedMember,
              ()) {
    auto blockCount = *rc::gen::inRange(static_cast<int64_t>(0),
                                         static_cast<int64_t>(1000000));
    auto totalSize = *rc::gen::inRange(static_cast<int64_t>(0),
                                        static_cast<int64_t>(100000000));

    TempDir storeTmp;
    TempDir headTmp;
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "enc-pool-node", "[email]");
    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipConfig config;
    config.fanout = 1;
    config.defaultTtl = 3;
    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    PoolAnnouncementMetadata meta;
    meta.blockCount = blockCount;
    meta.totalSize = totalSize;
    meta.encrypted = true;

    engine.announcePoolCreation("pool-enc-prop", meta);

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(pending.size() == 1u);

    const auto& ann = pending[0];
    RC_ASSERT(ann.type == AnnouncementType::PoolAnnounce);
    RC_ASSERT(ann.poolAnnouncement.has_value());
    RC_ASSERT(ann.poolAnnouncement->encrypted);

    // encryptedMetadata must be present and non-empty
    RC_ASSERT(ann.poolAnnouncement->encryptedMetadata.has_value());
    RC_ASSERT(!ann.poolAnnouncement->encryptedMetadata->empty());

    // Decode base64 → ciphertext bytes
    auto encryptedBytes = base64Decode(*ann.poolAnnouncement->encryptedMetadata);
    RC_ASSERT(!encryptedBytes.empty());

    // Decrypt using the local node's key pair
    auto privKeyBytes = localMember.privateKey();
    auto keyPair = brightchain::EcKeyPair::fromPrivateKey(privKeyBytes);
    auto decrypted = brightchain::Ecies::decrypt(encryptedBytes, keyPair);

    // Parse decrypted JSON and verify it matches original metadata
    std::string decryptedStr(decrypted.begin(), decrypted.end());
    auto j = nlohmann::json::parse(decryptedStr);

    RC_ASSERT(j["blockCount"].get<int64_t>() == blockCount);
    RC_ASSERT(j["totalSize"].get<int64_t>() == totalSize);
    RC_ASSERT(j["encrypted"].get<bool>() == true);
}

// ── Property 19b: Non-encrypted pools have no encryptedMetadata ───────────
// For any pool with encrypted == false, the announcement shall NOT contain
// encryptedMetadata.

RC_GTEST_PROP(EncryptedPoolMetadata,
              NonEncryptedPoolHasNoEncryptedMetadata,
              ()) {
    auto blockCount = *rc::gen::inRange(static_cast<int64_t>(0),
                                         static_cast<int64_t>(1000000));
    auto totalSize = *rc::gen::inRange(static_cast<int64_t>(0),
                                        static_cast<int64_t>(100000000));

    TempDir storeTmp;
    TempDir headTmp;
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "plain-pool-node", "[email]");
    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipConfig config;
    config.fanout = 1;
    config.defaultTtl = 3;
    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    PoolAnnouncementMetadata meta;
    meta.blockCount = blockCount;
    meta.totalSize = totalSize;
    meta.encrypted = false;

    engine.announcePoolCreation("pool-plain-prop", meta);

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(pending.size() == 1u);

    const auto& ann = pending[0];
    RC_ASSERT(ann.poolAnnouncement.has_value());
    RC_ASSERT(!ann.poolAnnouncement->encrypted);
    RC_ASSERT(!ann.poolAnnouncement->encryptedMetadata.has_value());
}

// ── Property 19c: Pre-supplied encryptedMetadata is preserved ─────────────
// When the caller already provides encryptedMetadata, the engine shall NOT
// overwrite it with its own encryption.

RC_GTEST_PROP(EncryptedPoolMetadata,
              PreSuppliedEncryptedMetadataIsPreserved,
              ()) {
    // Generate a random "pre-encrypted" string
    auto preEncrypted = *rc::gen::nonEmpty(
        rc::gen::container<std::string>(
            rc::gen::inRange(static_cast<char>('A'), static_cast<char>('Z' + 1))));

    TempDir storeTmp;
    TempDir headTmp;
    boost::asio::io_context ioc;
    auto localMember = brightchain::Member::generate(
        brightchain::MemberType::User, "presupplied-node", "[email]");
    PeerManager pm(ioc, localMember);
    brightchain::DiskBlockStore blockStore(storeTmp.path.string(),
                                           brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry headRegistry(headTmp.path.string());

    GossipConfig config;
    config.fanout = 1;
    config.defaultTtl = 3;
    GossipEngine engine(pm, blockStore, headRegistry, localMember, config);

    PoolAnnouncementMetadata meta;
    meta.blockCount = 100;
    meta.totalSize = 5000;
    meta.encrypted = true;
    meta.encryptedMetadata = preEncrypted;

    engine.announcePoolCreation("pool-presupplied", meta);

    auto pending = engine.getPendingAnnouncements();
    RC_ASSERT(pending.size() == 1u);

    const auto& ann = pending[0];
    RC_ASSERT(ann.poolAnnouncement.has_value());
    RC_ASSERT(ann.poolAnnouncement->encryptedMetadata.has_value());
    // Must be the exact same string we provided, not re-encrypted
    RC_ASSERT(*ann.poolAnnouncement->encryptedMetadata == preEncrypted);
}

// ── Property 19d: Different members produce different ciphertexts ─────────
// Two different members encrypting the same pool metadata should produce
// different encryptedMetadata (since they use different public keys).

RC_GTEST_PROP(EncryptedPoolMetadata,
              DifferentMembersProduceDifferentCiphertexts,
              ()) {
    auto blockCount = *rc::gen::inRange(static_cast<int64_t>(0),
                                         static_cast<int64_t>(100000));
    auto totalSize = *rc::gen::inRange(static_cast<int64_t>(0),
                                        static_cast<int64_t>(10000000));

    TempDir storeTmp1, storeTmp2;
    TempDir headTmp1, headTmp2;
    boost::asio::io_context ioc;

    auto member1 = brightchain::Member::generate(
        brightchain::MemberType::User, "node-1", "[email]");
    auto member2 = brightchain::Member::generate(
        brightchain::MemberType::User, "node-2", "[email]");

    PeerManager pm1(ioc, member1);
    PeerManager pm2(ioc, member2);
    brightchain::DiskBlockStore bs1(storeTmp1.path.string(), brightchain::BlockSize::Small);
    brightchain::DiskBlockStore bs2(storeTmp2.path.string(), brightchain::BlockSize::Small);
    brightchain::db::HeadRegistry hr1(headTmp1.path.string());
    brightchain::db::HeadRegistry hr2(headTmp2.path.string());

    GossipConfig config;
    config.fanout = 1;
    config.defaultTtl = 3;
    GossipEngine engine1(pm1, bs1, hr1, member1, config);
    GossipEngine engine2(pm2, bs2, hr2, member2, config);

    PoolAnnouncementMetadata meta;
    meta.blockCount = blockCount;
    meta.totalSize = totalSize;
    meta.encrypted = true;

    engine1.announcePoolCreation("pool-diff", meta);
    engine2.announcePoolCreation("pool-diff", meta);

    auto pending1 = engine1.getPendingAnnouncements();
    auto pending2 = engine2.getPendingAnnouncements();
    RC_ASSERT(pending1.size() == 1u);
    RC_ASSERT(pending2.size() == 1u);

    auto enc1 = pending1[0].poolAnnouncement->encryptedMetadata;
    auto enc2 = pending2[0].poolAnnouncement->encryptedMetadata;
    RC_ASSERT(enc1.has_value());
    RC_ASSERT(enc2.has_value());

    // Different keys → different ciphertexts (ECIES is probabilistic)
    RC_ASSERT(*enc1 != *enc2);

    // But each member can decrypt their own
    auto decrypted1 = brightchain::Ecies::decrypt(
        base64Decode(*enc1),
        brightchain::EcKeyPair::fromPrivateKey(member1.privateKey()));
    auto decrypted2 = brightchain::Ecies::decrypt(
        base64Decode(*enc2),
        brightchain::EcKeyPair::fromPrivateKey(member2.privateKey()));

    // Both decrypt to the same plaintext content
    RC_ASSERT(decrypted1 == decrypted2);
}
