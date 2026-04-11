// Feature: cpp-gossip-protocol, Property 1: BlockAnnouncement JSON round-trip
// **Validates: Requirements 1.10, 1.11, 1.12, 14.1, 14.6**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/block_announcement.hpp>

using namespace brightchain::gossip;

// Helper: convert rc::Maybe<T> to std::optional<T>
template <typename T>
static std::optional<T> toOptional(const rc::Maybe<T>& m) {
    if (m) return *m;
    return std::nullopt;
}

// Helper: generate std::optional<T> from a gen that produces T
template <typename T>
static rc::Gen<std::optional<T>> genOptional(rc::Gen<T> gen) {
    return rc::gen::map(rc::gen::maybe(std::move(gen)),
                        [](const rc::Maybe<T>& m) -> std::optional<T> {
                            if (m) return *m;
                            return std::nullopt;
                        });
}

// ── RapidCheck generators for metadata structs ─────────────────────────────

namespace rc {

template <>
struct Arbitrary<MessageDeliveryMetadata> {
    static Gen<MessageDeliveryMetadata> arbitrary() {
        return gen::build<MessageDeliveryMetadata>(
            gen::set(&MessageDeliveryMetadata::messageId,
                     gen::nonEmpty<std::string>()),
            gen::set(&MessageDeliveryMetadata::recipientIds,
                     gen::nonEmpty(gen::container<std::vector<std::string>>(
                         gen::nonEmpty<std::string>()))),
            gen::set(&MessageDeliveryMetadata::priority,
                     gen::element(std::string("normal"), std::string("high"))),
            gen::set(&MessageDeliveryMetadata::blockIds,
                     gen::nonEmpty(gen::container<std::vector<std::string>>(
                         gen::nonEmpty<std::string>()))),
            gen::set(&MessageDeliveryMetadata::cblBlockId,
                     gen::nonEmpty<std::string>()),
            gen::set(&MessageDeliveryMetadata::ackRequired,
                     gen::arbitrary<bool>()),
            gen::set(&MessageDeliveryMetadata::gatewayOutbound,
                     gen::arbitrary<bool>()));
    }
};

template <>
struct Arbitrary<DeliveryAckMetadata> {
    static Gen<DeliveryAckMetadata> arbitrary() {
        return gen::build<DeliveryAckMetadata>(
            gen::set(&DeliveryAckMetadata::messageId,
                     gen::nonEmpty<std::string>()),
            gen::set(&DeliveryAckMetadata::recipientId,
                     gen::nonEmpty<std::string>()),
            gen::set(&DeliveryAckMetadata::status,
                     gen::element(std::string("delivered"), std::string("read"),
                                  std::string("failed"), std::string("bounced"))),
            gen::set(&DeliveryAckMetadata::originalSenderNode,
                     gen::nonEmpty<std::string>()));
    }
};

template <>
struct Arbitrary<HeadUpdateMetadata> {
    static Gen<HeadUpdateMetadata> arbitrary() {
        return gen::build<HeadUpdateMetadata>(
            gen::set(&HeadUpdateMetadata::dbName,
                     gen::nonEmpty<std::string>()),
            gen::set(&HeadUpdateMetadata::collectionName,
                     gen::nonEmpty<std::string>()));
    }
};

template <>
struct Arbitrary<PoolAnnouncementMetadata> {
    static Gen<PoolAnnouncementMetadata> arbitrary() {
        return gen::exec([] {
            PoolAnnouncementMetadata m;
            m.blockCount = *gen::arbitrary<int64_t>();
            m.totalSize = *gen::arbitrary<int64_t>();
            m.encrypted = *gen::arbitrary<bool>();
            m.encryptedMetadata = *::genOptional(gen::nonEmpty<std::string>());
            return m;
        });
    }
};

template <>
struct Arbitrary<QuorumProposalMetadata> {
    static Gen<QuorumProposalMetadata> arbitrary() {
        return gen::exec([] {
            QuorumProposalMetadata m;
            m.proposalId = *gen::nonEmpty<std::string>();
            m.description = *gen::resize(100, gen::arbitrary<std::string>());
            m.actionType = *gen::nonEmpty<std::string>();
            m.actionPayload = *gen::nonEmpty<std::string>();
            m.proposerMemberId = *gen::nonEmpty<std::string>();
            m.expiresAt = *gen::nonEmpty<std::string>();
            m.requiredThreshold = *gen::inRange(1, 100);
            m.attachmentCblId = *::genOptional(gen::nonEmpty<std::string>());
            return m;
        });
    }
};

template <>
struct Arbitrary<QuorumVoteMetadata> {
    static Gen<QuorumVoteMetadata> arbitrary() {
        return gen::exec([] {
            QuorumVoteMetadata m;
            m.proposalId = *gen::nonEmpty<std::string>();
            m.voterMemberId = *gen::nonEmpty<std::string>();
            m.decision = *gen::element(std::string("approve"), std::string("reject"));
            m.comment = *::genOptional(gen::nonEmpty<std::string>());
            m.encryptedShare = *::genOptional(gen::nonEmpty(
                gen::container<std::vector<uint8_t>>(
                    gen::arbitrary<uint8_t>())));
            return m;
        });
    }
};

template <>
struct Arbitrary<WriteProof> {
    static Gen<WriteProof> arbitrary() {
        return gen::build<WriteProof>(
            gen::set(&WriteProof::signerPublicKey,
                     gen::nonEmpty<std::string>()),
            gen::set(&WriteProof::signature,
                     gen::nonEmpty<std::string>()),
            gen::set(&WriteProof::dbName,
                     gen::nonEmpty<std::string>()),
            gen::set(&WriteProof::collectionName,
                     gen::nonEmpty<std::string>()),
            gen::set(&WriteProof::blockId,
                     gen::nonEmpty<std::string>()));
    }
};

template <>
struct Arbitrary<CblIndexEntry> {
    static Gen<CblIndexEntry> arbitrary() {
        return gen::build<CblIndexEntry>(
            gen::set(&CblIndexEntry::magnetUrl,
                     gen::nonEmpty<std::string>()),
            gen::set(&CblIndexEntry::blockId1,
                     gen::nonEmpty<std::string>()),
            gen::set(&CblIndexEntry::blockId2,
                     gen::nonEmpty<std::string>()));
    }
};

} // namespace rc

// ── Generator for BlockAnnouncement covering all 12 types ──────────────────
// Defined outside namespace rc so it's accessible from RC_GTEST_PROP macros

static rc::Gen<BlockAnnouncement> genBlockAnnouncementForType(AnnouncementType type) {
    return rc::gen::exec([type] {
        BlockAnnouncement ann;
        ann.type = type;
        ann.blockId = *rc::gen::nonEmpty<std::string>();
        ann.nodeId = *rc::gen::nonEmpty<std::string>();
        ann.timestamp = *rc::gen::nonEmpty<std::string>();
        ann.ttl = *rc::gen::inRange(0, 20);

        switch (type) {
        case AnnouncementType::Add:
            ann.messageDelivery =
                *genOptional(rc::gen::arbitrary<MessageDeliveryMetadata>());
            ann.poolId = *genOptional(rc::gen::nonEmpty<std::string>());
            ann.writeProof = *genOptional(rc::gen::arbitrary<WriteProof>());
            break;

        case AnnouncementType::Remove:
            ann.poolId = *genOptional(rc::gen::nonEmpty<std::string>());
            break;

        case AnnouncementType::Ack:
            ann.deliveryAck = *rc::gen::arbitrary<DeliveryAckMetadata>();
            break;

        case AnnouncementType::PoolDeleted:
            ann.poolId = *rc::gen::nonEmpty<std::string>();
            break;

        case AnnouncementType::CblIndexUpdate:
        case AnnouncementType::CblIndexDelete:
            ann.poolId = *rc::gen::nonEmpty<std::string>();
            ann.cblIndexEntry = *rc::gen::arbitrary<CblIndexEntry>();
            break;

        case AnnouncementType::HeadUpdate:
            ann.headUpdate = *rc::gen::arbitrary<HeadUpdateMetadata>();
            ann.writeProof = *genOptional(rc::gen::arbitrary<WriteProof>());
            break;

        case AnnouncementType::AclUpdate:
            ann.poolId = *rc::gen::nonEmpty<std::string>();
            ann.aclBlockId = *rc::gen::nonEmpty<std::string>();
            break;

        case AnnouncementType::PoolAnnounce:
            ann.poolId = *rc::gen::nonEmpty<std::string>();
            ann.poolAnnouncement = *rc::gen::arbitrary<PoolAnnouncementMetadata>();
            break;

        case AnnouncementType::PoolRemove:
            ann.poolId = *rc::gen::nonEmpty<std::string>();
            break;

        case AnnouncementType::QuorumProposal:
            ann.quorumProposal = *rc::gen::arbitrary<QuorumProposalMetadata>();
            break;

        case AnnouncementType::QuorumVote:
            ann.quorumVote = *rc::gen::arbitrary<QuorumVoteMetadata>();
            break;
        }

        return ann;
    });
}

namespace rc {

template <>
struct Arbitrary<BlockAnnouncement> {
    static Gen<BlockAnnouncement> arbitrary() {
        return gen::oneOf(
            ::genBlockAnnouncementForType(AnnouncementType::Add),
            ::genBlockAnnouncementForType(AnnouncementType::Remove),
            ::genBlockAnnouncementForType(AnnouncementType::Ack),
            ::genBlockAnnouncementForType(AnnouncementType::PoolDeleted),
            ::genBlockAnnouncementForType(AnnouncementType::CblIndexUpdate),
            ::genBlockAnnouncementForType(AnnouncementType::CblIndexDelete),
            ::genBlockAnnouncementForType(AnnouncementType::HeadUpdate),
            ::genBlockAnnouncementForType(AnnouncementType::AclUpdate),
            ::genBlockAnnouncementForType(AnnouncementType::PoolAnnounce),
            ::genBlockAnnouncementForType(AnnouncementType::PoolRemove),
            ::genBlockAnnouncementForType(AnnouncementType::QuorumProposal),
            ::genBlockAnnouncementForType(AnnouncementType::QuorumVote));
    }
};

} // namespace rc

// ── Property test ──────────────────────────────────────────────────────────

RC_GTEST_PROP(BlockAnnouncementRoundTrip,
              JsonRoundTrip,
              (BlockAnnouncement ann)) {
    // Property 1: fromJson(toJson(ann)) == ann
    auto json = ann.toJson();
    auto restored = BlockAnnouncement::fromJson(json);
    RC_ASSERT(restored == ann);
}

// ── Property 4: BlockAnnouncement type-specific validation ─────────────────
// **Validates: Requirements 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.10**

// Helper: generate a valid announcement for a given type (reuses genBlockAnnouncementForType)
static rc::Gen<AnnouncementType> genAnnouncementType() {
    return rc::gen::element(
        AnnouncementType::Add,
        AnnouncementType::Remove,
        AnnouncementType::Ack,
        AnnouncementType::PoolDeleted,
        AnnouncementType::CblIndexUpdate,
        AnnouncementType::CblIndexDelete,
        AnnouncementType::HeadUpdate,
        AnnouncementType::AclUpdate,
        AnnouncementType::PoolAnnounce,
        AnnouncementType::PoolRemove,
        AnnouncementType::QuorumProposal,
        AnnouncementType::QuorumVote);
}

// Property 4a: Valid announcements pass validation
RC_GTEST_PROP(BlockAnnouncementValidation,
              ValidAnnouncementsValidate,
              ()) {
    auto type = *genAnnouncementType();
    auto ann = *genBlockAnnouncementForType(type);
    RC_ASSERT(ann.validate());
}

// Property 4b: messageDelivery on non-Add types → invalid (Req 2.1)
RC_GTEST_PROP(BlockAnnouncementValidation,
              MessageDeliveryOnNonAddIsInvalid,
              ()) {
    // Pick any type that is NOT Add
    auto type = *rc::gen::suchThat(genAnnouncementType(), [](AnnouncementType t) {
        return t != AnnouncementType::Add;
    });
    auto ann = *genBlockAnnouncementForType(type);
    // Force messageDelivery to be present
    ann.messageDelivery = *rc::gen::arbitrary<MessageDeliveryMetadata>();
    RC_ASSERT(!ann.validate());
}

// Property 4c: deliveryAck on non-Ack types → invalid (Req 2.2)
RC_GTEST_PROP(BlockAnnouncementValidation,
              DeliveryAckOnNonAckIsInvalid,
              ()) {
    auto type = *rc::gen::suchThat(genAnnouncementType(), [](AnnouncementType t) {
        return t != AnnouncementType::Ack;
    });
    auto ann = *genBlockAnnouncementForType(type);
    ann.deliveryAck = *rc::gen::arbitrary<DeliveryAckMetadata>();
    RC_ASSERT(!ann.validate());
}

// Property 4d: Ack without deliveryAck → invalid (Req 2.2)
RC_GTEST_PROP(BlockAnnouncementValidation,
              AckWithoutDeliveryAckIsInvalid,
              ()) {
    auto ann = *genBlockAnnouncementForType(AnnouncementType::Ack);
    ann.deliveryAck = std::nullopt;
    RC_ASSERT(!ann.validate());
}

// Property 4e: pool_deleted with empty poolId → invalid (Req 2.3)
RC_GTEST_PROP(BlockAnnouncementValidation,
              PoolDeletedEmptyPoolIdIsInvalid,
              ()) {
    auto ann = *genBlockAnnouncementForType(AnnouncementType::PoolDeleted);
    ann.poolId = *rc::gen::element(std::optional<std::string>(std::nullopt),
                                    std::optional<std::string>(std::string("")));
    RC_ASSERT(!ann.validate());
}

// Property 4f: pool_deleted with messageDelivery → invalid (Req 2.3)
RC_GTEST_PROP(BlockAnnouncementValidation,
              PoolDeletedWithMessageDeliveryIsInvalid,
              ()) {
    auto ann = *genBlockAnnouncementForType(AnnouncementType::PoolDeleted);
    ann.messageDelivery = *rc::gen::arbitrary<MessageDeliveryMetadata>();
    RC_ASSERT(!ann.validate());
}

// Property 4g: pool_deleted with deliveryAck → invalid (Req 2.3)
RC_GTEST_PROP(BlockAnnouncementValidation,
              PoolDeletedWithDeliveryAckIsInvalid,
              ()) {
    auto ann = *genBlockAnnouncementForType(AnnouncementType::PoolDeleted);
    ann.deliveryAck = *rc::gen::arbitrary<DeliveryAckMetadata>();
    RC_ASSERT(!ann.validate());
}

// Property 4h: cbl_index_update/delete with missing poolId → invalid (Req 2.4)
RC_GTEST_PROP(BlockAnnouncementValidation,
              CblIndexMissingPoolIdIsInvalid,
              ()) {
    auto type = *rc::gen::element(AnnouncementType::CblIndexUpdate,
                                   AnnouncementType::CblIndexDelete);
    auto ann = *genBlockAnnouncementForType(type);
    ann.poolId = *rc::gen::element(std::optional<std::string>(std::nullopt),
                                    std::optional<std::string>(std::string("")));
    RC_ASSERT(!ann.validate());
}

// Property 4i: cbl_index_update/delete with missing/empty cblIndexEntry fields → invalid (Req 2.4)
RC_GTEST_PROP(BlockAnnouncementValidation,
              CblIndexEmptyEntryFieldsIsInvalid,
              ()) {
    auto type = *rc::gen::element(AnnouncementType::CblIndexUpdate,
                                   AnnouncementType::CblIndexDelete);
    auto ann = *genBlockAnnouncementForType(type);
    // Corrupt one of the cblIndexEntry fields
    auto choice = *rc::gen::inRange(0, 4);
    switch (choice) {
    case 0:
        ann.cblIndexEntry = std::nullopt;
        break;
    case 1:
        ann.cblIndexEntry->magnetUrl = "";
        break;
    case 2:
        ann.cblIndexEntry->blockId1 = "";
        break;
    case 3:
        ann.cblIndexEntry->blockId2 = "";
        break;
    }
    RC_ASSERT(!ann.validate());
}

// Property 4j: head_update with empty blockId → invalid (Req 2.5)
RC_GTEST_PROP(BlockAnnouncementValidation,
              HeadUpdateEmptyBlockIdIsInvalid,
              ()) {
    auto ann = *genBlockAnnouncementForType(AnnouncementType::HeadUpdate);
    ann.blockId = "";
    RC_ASSERT(!ann.validate());
}

// Property 4k: head_update with missing/empty headUpdate fields → invalid (Req 2.5)
RC_GTEST_PROP(BlockAnnouncementValidation,
              HeadUpdateMissingOrEmptyFieldsIsInvalid,
              ()) {
    auto ann = *genBlockAnnouncementForType(AnnouncementType::HeadUpdate);
    auto choice = *rc::gen::inRange(0, 3);
    switch (choice) {
    case 0:
        ann.headUpdate = std::nullopt;
        break;
    case 1:
        ann.headUpdate->dbName = "";
        break;
    case 2:
        ann.headUpdate->collectionName = "";
        break;
    }
    RC_ASSERT(!ann.validate());
}

// Property 4l: acl_update with missing poolId or aclBlockId → invalid (Req 2.6)
RC_GTEST_PROP(BlockAnnouncementValidation,
              AclUpdateMissingFieldsIsInvalid,
              ()) {
    auto ann = *genBlockAnnouncementForType(AnnouncementType::AclUpdate);
    auto choice = *rc::gen::inRange(0, 2);
    switch (choice) {
    case 0:
        ann.poolId = *rc::gen::element(std::optional<std::string>(std::nullopt),
                                        std::optional<std::string>(std::string("")));
        break;
    case 1:
        ann.aclBlockId = *rc::gen::element(std::optional<std::string>(std::nullopt),
                                            std::optional<std::string>(std::string("")));
        break;
    }
    RC_ASSERT(!ann.validate());
}

// Property 4m: pool_announce with missing poolId or poolAnnouncement → invalid (Req 2.7)
RC_GTEST_PROP(BlockAnnouncementValidation,
              PoolAnnounceMissingFieldsIsInvalid,
              ()) {
    auto ann = *genBlockAnnouncementForType(AnnouncementType::PoolAnnounce);
    auto choice = *rc::gen::inRange(0, 2);
    switch (choice) {
    case 0:
        ann.poolId = *rc::gen::element(std::optional<std::string>(std::nullopt),
                                        std::optional<std::string>(std::string("")));
        break;
    case 1:
        ann.poolAnnouncement = std::nullopt;
        break;
    }
    RC_ASSERT(!ann.validate());
}

// Property 4n: quorum_proposal with invalid fields → invalid (Req 2.8)
RC_GTEST_PROP(BlockAnnouncementValidation,
              QuorumProposalInvalidFieldsIsInvalid,
              ()) {
    auto ann = *genBlockAnnouncementForType(AnnouncementType::QuorumProposal);
    auto choice = *rc::gen::inRange(0, 5);
    switch (choice) {
    case 0:
        ann.quorumProposal = std::nullopt;
        break;
    case 1:
        ann.quorumProposal->proposalId = "";
        break;
    case 2:
        // description > 4096 chars
        ann.quorumProposal->description = std::string(4097, 'x');
        break;
    case 3:
        ann.quorumProposal->proposerMemberId = "";
        break;
    case 4:
        ann.quorumProposal->requiredThreshold = *rc::gen::inRange(-100, 1);
        break;
    }
    RC_ASSERT(!ann.validate());
}

// Property 4o: quorum_vote with invalid fields → invalid (Req 2.9)
RC_GTEST_PROP(BlockAnnouncementValidation,
              QuorumVoteInvalidFieldsIsInvalid,
              ()) {
    auto ann = *genBlockAnnouncementForType(AnnouncementType::QuorumVote);
    auto choice = *rc::gen::inRange(0, 5);
    switch (choice) {
    case 0:
        ann.quorumVote = std::nullopt;
        break;
    case 1:
        ann.quorumVote->proposalId = "";
        break;
    case 2:
        ann.quorumVote->voterMemberId = "";
        break;
    case 3:
        // invalid decision
        ann.quorumVote->decision = *rc::gen::suchThat<std::string>([](const std::string& s) {
            return s != "approve" && s != "reject";
        });
        break;
    case 4:
        // comment > 1024 chars
        ann.quorumVote->comment = std::string(1025, 'y');
        break;
    }
    RC_ASSERT(!ann.validate());
}

// Property 4p: pool_remove with missing poolId → invalid
RC_GTEST_PROP(BlockAnnouncementValidation,
              PoolRemoveMissingPoolIdIsInvalid,
              ()) {
    auto ann = *genBlockAnnouncementForType(AnnouncementType::PoolRemove);
    ann.poolId = *rc::gen::element(std::optional<std::string>(std::nullopt),
                                    std::optional<std::string>(std::string("")));
    RC_ASSERT(!ann.validate());
}
