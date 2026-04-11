// Feature: cpp-gossip-protocol
// Task 15.3: Property 21 — Encrypted share preservation in approve votes
// **Validates: Requirements 9.5**
//
// For any quorum_vote BlockAnnouncement with an encryptedShare byte vector,
// serializing to JSON via toJson() then deserializing via fromJson() shall
// produce a QuorumVoteMetadata whose encryptedShare is byte-for-byte identical
// to the original.

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/block_announcement.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace brightchain::gossip;

// ── Generators ─────────────────────────────────────────────────────────────

namespace {

rc::Gen<std::string> genNonEmptyAlphaNum() {
    return rc::gen::nonEmpty(
        rc::gen::container<std::string>(
            rc::gen::inRange(static_cast<char>('a'), static_cast<char>('z' + 1))));
}

rc::Gen<std::vector<uint8_t>> genNonEmptyBytes() {
    return rc::gen::nonEmpty(
        rc::gen::container<std::vector<uint8_t>>(
            rc::gen::inRange(static_cast<uint8_t>(0), static_cast<uint8_t>(255))));
}

rc::Gen<QuorumVoteMetadata> genApproveVoteWithShare() {
    return rc::gen::exec([] {
        QuorumVoteMetadata qv;
        qv.proposalId = *genNonEmptyAlphaNum();
        qv.voterMemberId = *genNonEmptyAlphaNum();
        qv.decision = "approve";
        qv.encryptedShare = *genNonEmptyBytes();
        // Optionally include a comment
        if (*rc::gen::inRange(0, 2) == 0) {
            qv.comment = *genNonEmptyAlphaNum();
        }
        return qv;
    });
}

} // namespace

// ── Property 21a: encryptedShare bytes preserved through QuorumVoteMetadata
//    toJson → fromJson round-trip ───────────────────────────────────────────

RC_GTEST_PROP(EncryptedSharePreservation,
              VoteMetadataRoundTrip,
              ()) {
    auto vote = *genApproveVoteWithShare();

    auto json = vote.toJson();
    auto restored = QuorumVoteMetadata::fromJson(json);

    RC_ASSERT(restored.encryptedShare.has_value());
    RC_ASSERT(restored.encryptedShare.value() == vote.encryptedShare.value());
    RC_ASSERT(restored == vote);
}

// ── Property 21b: encryptedShare bytes preserved through full
//    BlockAnnouncement toJson → fromJson round-trip ─────────────────────────

RC_GTEST_PROP(EncryptedSharePreservation,
              FullAnnouncementRoundTrip,
              ()) {
    auto vote = *genApproveVoteWithShare();

    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumVote;
    ann.blockId = *genNonEmptyAlphaNum();
    ann.nodeId = *genNonEmptyAlphaNum();
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = *rc::gen::inRange(1, 15);
    ann.quorumVote = vote;

    auto json = ann.toJson();
    auto restored = BlockAnnouncement::fromJson(json);

    RC_ASSERT(restored.quorumVote.has_value());
    RC_ASSERT(restored.quorumVote->encryptedShare.has_value());
    RC_ASSERT(restored.quorumVote->encryptedShare.value() ==
              vote.encryptedShare.value());
    RC_ASSERT(restored == ann);
}

// ── Property 21c: absent encryptedShare stays absent through round-trip ────

RC_GTEST_PROP(EncryptedSharePreservation,
              AbsentShareRemainsAbsent,
              ()) {
    QuorumVoteMetadata vote;
    vote.proposalId = *genNonEmptyAlphaNum();
    vote.voterMemberId = *genNonEmptyAlphaNum();
    vote.decision = *rc::gen::element(std::string("approve"), std::string("reject"));
    // No encryptedShare set

    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumVote;
    ann.blockId = *genNonEmptyAlphaNum();
    ann.nodeId = *genNonEmptyAlphaNum();
    ann.timestamp = "2025-01-28T12:00:00.000Z";
    ann.ttl = *rc::gen::inRange(1, 15);
    ann.quorumVote = vote;

    auto json = ann.toJson();
    auto restored = BlockAnnouncement::fromJson(json);

    RC_ASSERT(restored.quorumVote.has_value());
    RC_ASSERT(!restored.quorumVote->encryptedShare.has_value());
    RC_ASSERT(restored == ann);
}
