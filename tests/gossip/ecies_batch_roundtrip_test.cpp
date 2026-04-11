// Feature: cpp-gossip-protocol, Property 10: Sensitive batch ECIES encryption round-trip
// **Validates: Requirements 5.7**
//
// For any batch of BlockAnnouncement objects containing messageDelivery or
// deliveryAck metadata, and any peer with a known public key, encrypting the
// batch with ECIES using the peer's public key then decrypting with the
// peer's private key shall produce the original batch.

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/block_announcement.hpp>
#include <brightchain/ecies.hpp>
#include <brightchain/ec_key_pair.hpp>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace brightchain::gossip;

// ── RapidCheck generators ──────────────────────────────────────────────────

namespace {

/// Generate a non-empty alphanumeric string.
rc::Gen<std::string> genNonEmptyAlpha() {
    return rc::gen::nonEmpty(
        rc::gen::container<std::string>(
            rc::gen::inRange(static_cast<char>('a'), static_cast<char>('z' + 1))));
}

/// Generate std::optional<T> from a gen that produces T.
template <typename T>
rc::Gen<std::optional<T>> genOptional(rc::Gen<T> gen) {
    return rc::gen::map(rc::gen::maybe(std::move(gen)),
                        [](const rc::Maybe<T>& m) -> std::optional<T> {
                            if (m) return *m;
                            return std::nullopt;
                        });
}

/// Generate a MessageDeliveryMetadata with valid fields.
rc::Gen<MessageDeliveryMetadata> genMessageDelivery() {
    return rc::gen::exec([] {
        MessageDeliveryMetadata md;
        md.messageId = *genNonEmptyAlpha();
        md.recipientIds = *rc::gen::nonEmpty(
            rc::gen::container<std::vector<std::string>>(genNonEmptyAlpha()));
        md.priority = *rc::gen::element(std::string("normal"), std::string("high"));
        md.blockIds = *rc::gen::nonEmpty(
            rc::gen::container<std::vector<std::string>>(genNonEmptyAlpha()));
        md.cblBlockId = *genNonEmptyAlpha();
        md.ackRequired = *rc::gen::arbitrary<bool>();
        md.gatewayOutbound = *rc::gen::arbitrary<bool>();
        return md;
    });
}

/// Generate a DeliveryAckMetadata with valid fields.
rc::Gen<DeliveryAckMetadata> genDeliveryAck() {
    return rc::gen::exec([] {
        DeliveryAckMetadata ack;
        ack.messageId = *genNonEmptyAlpha();
        ack.recipientId = *genNonEmptyAlpha();
        ack.status = *rc::gen::element(
            std::string("delivered"), std::string("read"),
            std::string("failed"), std::string("bounced"));
        ack.originalSenderNode = *genNonEmptyAlpha();
        return ack;
    });
}

/// Generate a BlockAnnouncement with messageDelivery metadata (Add type).
rc::Gen<BlockAnnouncement> genMessageDeliveryAnn() {
    return rc::gen::exec([] {
        BlockAnnouncement ann;
        ann.type = AnnouncementType::Add;
        ann.blockId = *genNonEmptyAlpha();
        ann.nodeId = *genNonEmptyAlpha();
        ann.timestamp = "2025-01-28T12:00:00.000Z";
        ann.ttl = *rc::gen::inRange(1, 10);
        ann.messageDelivery = *genMessageDelivery();
        return ann;
    });
}

/// Generate a BlockAnnouncement with deliveryAck metadata (Ack type).
rc::Gen<BlockAnnouncement> genDeliveryAckAnn() {
    return rc::gen::exec([] {
        BlockAnnouncement ann;
        ann.type = AnnouncementType::Ack;
        ann.blockId = *genNonEmptyAlpha();
        ann.nodeId = *genNonEmptyAlpha();
        ann.timestamp = "2025-01-28T12:00:00.000Z";
        ann.ttl = *rc::gen::inRange(1, 10);
        ann.deliveryAck = *genDeliveryAck();
        return ann;
    });
}

/// Generate a sensitive BlockAnnouncement (either messageDelivery or deliveryAck).
rc::Gen<BlockAnnouncement> genSensitiveAnn() {
    return rc::gen::oneOf(genMessageDeliveryAnn(), genDeliveryAckAnn());
}

/// Generate a batch of 1–10 sensitive BlockAnnouncements.
rc::Gen<std::vector<BlockAnnouncement>> genSensitiveBatch() {
    return rc::gen::exec([] {
        int count = *rc::gen::inRange(1, 11);
        std::vector<BlockAnnouncement> batch;
        batch.reserve(count);
        for (int i = 0; i < count; ++i) {
            batch.push_back(*genSensitiveAnn());
        }
        return batch;
    });
}

} // namespace

// ── Property 10: Sensitive batch ECIES encryption round-trip ───────────────
// For any batch of BlockAnnouncement objects containing messageDelivery or
// deliveryAck metadata, and any random EcKeyPair, encrypting the serialized
// batch JSON with ECIES using the public key then decrypting with the
// private key shall produce the original batch.

RC_GTEST_PROP(EciesBatchRoundTrip,
              EncryptDecryptRecoversBatch,
              ()) {
    // Generate a batch of sensitive announcements
    auto batch = *genSensitiveBatch();

    // Generate a fresh EcKeyPair for the peer
    auto keyPair = brightchain::EcKeyPair::generate();
    auto pubKey = keyPair.publicKey();

    // Serialize the batch to JSON (same as encryptAndSendBatch does)
    nlohmann::json batchJson = nlohmann::json::array();
    for (const auto& ann : batch) {
        batchJson.push_back(ann.toJson());
    }
    std::string payload = batchJson.dump();
    std::vector<uint8_t> plaintext(payload.begin(), payload.end());

    // Encrypt with the peer's public key
    auto encrypted = brightchain::Ecies::encryptBasic(plaintext, pubKey);

    // Encrypted output must differ from plaintext
    RC_ASSERT(encrypted.size() > plaintext.size());

    // Decrypt with the peer's private key
    auto decrypted = brightchain::Ecies::decrypt(encrypted, keyPair);

    // Decrypted bytes must match original plaintext
    RC_ASSERT(decrypted == plaintext);

    // Parse the decrypted JSON back into BlockAnnouncements
    std::string decryptedStr(decrypted.begin(), decrypted.end());
    auto decryptedJson = nlohmann::json::parse(decryptedStr);
    RC_ASSERT(decryptedJson.is_array());
    RC_ASSERT(decryptedJson.size() == batch.size());

    // Each deserialized announcement must equal the original
    for (size_t i = 0; i < batch.size(); ++i) {
        auto restored = BlockAnnouncement::fromJson(decryptedJson[i]);
        RC_ASSERT(restored == batch[i]);
    }
}

// ── Property 10b: Round-trip preserves sensitive metadata fields ────────────
// Specifically verify that messageDelivery and deliveryAck metadata fields
// survive the encrypt→decrypt→deserialize cycle.

RC_GTEST_PROP(EciesBatchRoundTrip,
              PreservesSensitiveMetadataFields,
              ()) {
    auto ann = *genSensitiveAnn();
    auto keyPair = brightchain::EcKeyPair::generate();
    auto pubKey = keyPair.publicKey();

    // Serialize single-element batch
    nlohmann::json batchJson = nlohmann::json::array();
    batchJson.push_back(ann.toJson());
    std::string payload = batchJson.dump();
    std::vector<uint8_t> plaintext(payload.begin(), payload.end());

    // Encrypt → decrypt
    auto encrypted = brightchain::Ecies::encryptBasic(plaintext, pubKey);
    auto decrypted = brightchain::Ecies::decrypt(encrypted, keyPair);

    std::string decryptedStr(decrypted.begin(), decrypted.end());
    auto decryptedJson = nlohmann::json::parse(decryptedStr);
    auto restored = BlockAnnouncement::fromJson(decryptedJson[0]);

    // Verify sensitive metadata is preserved
    RC_ASSERT(restored.messageDelivery == ann.messageDelivery);
    RC_ASSERT(restored.deliveryAck == ann.deliveryAck);
    RC_ASSERT(restored == ann);
}

// ── Property 10c: Different key pairs produce different ciphertexts ────────
// Encrypting the same batch with two different public keys should produce
// different ciphertexts (probabilistic encryption).

RC_GTEST_PROP(EciesBatchRoundTrip,
              DifferentKeysProduceDifferentCiphertexts,
              ()) {
    auto batch = *genSensitiveBatch();

    auto keyPair1 = brightchain::EcKeyPair::generate();
    auto keyPair2 = brightchain::EcKeyPair::generate();

    nlohmann::json batchJson = nlohmann::json::array();
    for (const auto& ann : batch) {
        batchJson.push_back(ann.toJson());
    }
    std::string payload = batchJson.dump();
    std::vector<uint8_t> plaintext(payload.begin(), payload.end());

    auto encrypted1 = brightchain::Ecies::encryptBasic(plaintext, keyPair1.publicKey());
    auto encrypted2 = brightchain::Ecies::encryptBasic(plaintext, keyPair2.publicKey());

    // Ciphertexts should differ (different ephemeral keys + different recipients)
    RC_ASSERT(encrypted1 != encrypted2);

    // But both should decrypt to the same plaintext with their respective keys
    auto decrypted1 = brightchain::Ecies::decrypt(encrypted1, keyPair1);
    auto decrypted2 = brightchain::Ecies::decrypt(encrypted2, keyPair2);
    RC_ASSERT(decrypted1 == plaintext);
    RC_ASSERT(decrypted2 == plaintext);
}
