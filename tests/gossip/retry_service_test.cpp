// Feature: cpp-gossip-protocol, Property 11: Retry backoff calculation with cap
// **Validates: Requirements 6.2, 6.3**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/retry_service.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>

using namespace brightchain::gossip;

// Dummy GossipEngine for testing — only used as a reference placeholder.
// The property tests below only exercise calculateBackoff() and validateTransition(),
// neither of which accesses the engine_ member.
namespace {
alignas(alignof(std::max_align_t)) char dummyEngineStorage[256];
auto& dummyEngine() {
    return *reinterpret_cast<brightchain::gossip::GossipEngine*>(dummyEngineStorage);
}
} // namespace

// ── Property 11: Retry backoff calculation with cap ────────────────────────
// For any RetryConfig with positive initialTimeoutMs, backoffMultiplier >= 1,
// and maxBackoffMs >= initialTimeoutMs, and any retry count n in [1..20],
// calculateBackoff(n) == min(initialTimeoutMs * multiplier^(n-1), maxBackoffMs).

RC_GTEST_PROP(RetryBackoff,
              BackoffCalculationWithCap,
              ()) {
    // Generate constrained RetryConfig values
    const int initialTimeoutMs = *rc::gen::inRange(1, 60001);
    const int backoffMultiplier = *rc::gen::inRange(1, 11);
    const int maxBackoffMs = *rc::gen::inRange(initialTimeoutMs, initialTimeoutMs + 500001);
    const int retryCount = *rc::gen::inRange(1, 21);

    RetryConfig config;
    config.initialTimeoutMs = initialTimeoutMs;
    config.backoffMultiplier = backoffMultiplier;
    config.maxBackoffMs = maxBackoffMs;
    config.maxRetries = 20; // not relevant for backoff calculation

    RetryService service(dummyEngine(), config);

    const int actual = service.calculateBackoff(retryCount);

    // Expected: min(initialTimeoutMs * multiplier^(retryCount-1), maxBackoffMs)
    const double rawDelay = static_cast<double>(initialTimeoutMs)
                          * std::pow(static_cast<double>(backoffMultiplier),
                                     static_cast<double>(retryCount - 1));

    int expected;
    if (rawDelay > static_cast<double>(maxBackoffMs)) {
        expected = maxBackoffMs;
    } else {
        expected = static_cast<int>(rawDelay);
    }

    RC_ASSERT(actual == expected);
}

// ── Property 12: Delivery status state machine transitions ─────────────────
// **Validates: Requirements 6.8**
// For any pair of DeliveryStatus values (from, to), the transition is valid
// if and only if it matches one of: Announced → Pending, Pending → Delivered,
// Pending → Failed, Pending → Bounced, Delivered → Read.
// All other transitions shall be rejected.

namespace {

// RapidCheck generator for DeliveryStatus enum values (all 6).
rc::Gen<DeliveryStatus> genDeliveryStatus() {
    return rc::gen::element(
        DeliveryStatus::Announced,
        DeliveryStatus::Pending,
        DeliveryStatus::Delivered,
        DeliveryStatus::Read,
        DeliveryStatus::Failed,
        DeliveryStatus::Bounced
    );
}

// Reference oracle: returns true iff (from, to) is a valid state machine transition.
bool isValidTransition(DeliveryStatus from, DeliveryStatus to) {
    if (from == DeliveryStatus::Announced && to == DeliveryStatus::Pending) return true;
    if (from == DeliveryStatus::Pending   && to == DeliveryStatus::Delivered) return true;
    if (from == DeliveryStatus::Pending   && to == DeliveryStatus::Failed) return true;
    if (from == DeliveryStatus::Pending   && to == DeliveryStatus::Bounced) return true;
    if (from == DeliveryStatus::Delivered && to == DeliveryStatus::Read) return true;
    return false;
}

} // namespace

RC_GTEST_PROP(DeliveryStatusStateMachine,
              OnlyValidTransitionsAccepted,
              ()) {
    const auto from = *genDeliveryStatus();
    const auto to   = *genDeliveryStatus();

    RetryConfig config; // defaults are fine — validateTransition doesn't use config
    RetryService service(dummyEngine(), config);

    const bool actual   = service.validateTransition(from, to);
    const bool expected = isValidTransition(from, to);

    RC_ASSERT(actual == expected);
}

// ── Property 13: Retry exhaustion marks recipients as Failed ───────────────
// **Validates: Requirements 6.4, 6.6**
// For any delivery with N recipients (all starting as Announced), after
// maxRetries + 1 calls to checkRetries() (each past nextRetryAt), all
// recipients that were Announced or Pending shall be marked Failed, the
// failure handler shall be invoked with exactly those recipients, and the
// delivery shall be removed from tracking.

RC_GTEST_PROP(RetryExhaustion,
              AllAnnouncedOrPendingBecomeFailedAfterMaxRetries,
              ()) {
    const int maxRetries = *rc::gen::inRange(1, 11);
    const int recipientCount = *rc::gen::inRange(1, 11);

    // Build unique recipient IDs.
    std::vector<std::string> recipientIds;
    recipientIds.reserve(recipientCount);
    for (int i = 0; i < recipientCount; ++i) {
        recipientIds.push_back("recipient-" + std::to_string(i));
    }

    RetryConfig config;
    config.initialTimeoutMs = 0; // retries fire immediately
    config.backoffMultiplier = 1;
    config.maxRetries = maxRetries;
    config.maxBackoffMs = 0;

    RetryService service(dummyEngine(), config);

    // Track failure handler invocations.
    std::vector<std::string> reportedFailedRecipients;
    std::string reportedMessageId;
    service.onDeliveryFailed(
        [&](const std::string& msgId, const std::vector<std::string>& failed) {
            reportedMessageId = msgId;
            reportedFailedRecipients = failed;
        });

    // Set up delivery metadata.
    MessageDeliveryMetadata meta;
    meta.messageId = "test-msg";
    meta.recipientIds = recipientIds;
    meta.priority = "normal";
    meta.blockIds = {"block-1"};
    meta.cblBlockId = "cbl-1";
    meta.ackRequired = false;

    service.trackDelivery("test-msg", {"block-1"}, meta);

    // Verify initial state: all recipients Announced, delivery tracked.
    RC_ASSERT(service.getPendingCount() == 1);
    auto pd = service.getPendingDelivery("test-msg");
    RC_ASSERT(pd.has_value());
    for (const auto& rid : recipientIds) {
        RC_ASSERT(pd->recipientStatuses.at(rid) == DeliveryStatus::Announced);
    }

    // Call checkRetries maxRetries times — should NOT exhaust yet.
    // Each call increments retryCount; exhaustion triggers when
    // retryCount > maxRetries (i.e., on the maxRetries+1 th call).
    for (int i = 0; i < maxRetries; ++i) {
        service.checkRetries();
    }

    // Delivery should still be tracked (not yet exhausted).
    RC_ASSERT(service.getPendingCount() == 1);
    RC_ASSERT(reportedFailedRecipients.empty());

    // One more call to exhaust retries.
    service.checkRetries();

    // Now the delivery should be removed and failure handler invoked.
    RC_ASSERT(service.getPendingCount() == 0);
    RC_ASSERT(service.getPendingDelivery("test-msg") == std::nullopt);
    RC_ASSERT(reportedMessageId == "test-msg");

    // All recipients should have been reported as failed.
    std::set<std::string> expectedFailed(recipientIds.begin(), recipientIds.end());
    std::set<std::string> actualFailed(reportedFailedRecipients.begin(),
                                       reportedFailedRecipients.end());
    RC_ASSERT(actualFailed == expectedFailed);
}

// ── Property 14: Ack removes fully-delivered from tracking ─────────────────
// **Validates: Requirements 6.5**
// For any PendingDelivery where every recipient's status reaches Delivered or
// Read, the delivery shall be removed from the pending tracking map.
// We generate 1–10 recipients, transition them through Announced → Pending via
// checkRetries(), then send "delivered" or "read" acks for each.  After all
// but the last recipient reaches a terminal state the delivery must still be
// tracked; after the final recipient's ack it must be removed.

RC_GTEST_PROP(AckRemovesFullyDelivered,
              AllRecipientsDeliveredOrReadRemovesFromTracking,
              ()) {
    const int recipientCount = *rc::gen::inRange(1, 11);

    // Build unique recipient IDs.
    std::vector<std::string> recipientIds;
    recipientIds.reserve(recipientCount);
    for (int i = 0; i < recipientCount; ++i) {
        recipientIds.push_back("recip-" + std::to_string(i));
    }

    // For each recipient, randomly choose whether their final status is
    // Delivered (false) or Read (true).
    std::vector<bool> wantsRead;
    wantsRead.reserve(recipientCount);
    for (int i = 0; i < recipientCount; ++i) {
        wantsRead.push_back(*rc::gen::arbitrary<bool>());
    }

    // Use initialTimeoutMs = 0 so checkRetries fires immediately.
    RetryConfig config;
    config.initialTimeoutMs = 0;
    config.backoffMultiplier = 1;
    config.maxRetries = 100; // high enough to never exhaust
    config.maxBackoffMs = 0;

    RetryService service(dummyEngine(), config);

    // Set up delivery metadata.
    MessageDeliveryMetadata meta;
    meta.messageId = "prop14-msg";
    meta.recipientIds = recipientIds;
    meta.priority = "normal";
    meta.blockIds = {"block-1"};
    meta.cblBlockId = "cbl-1";
    meta.ackRequired = true;

    service.trackDelivery("prop14-msg", {"block-1"}, meta);
    RC_ASSERT(service.getPendingCount() == 1);

    // Transition all recipients from Announced → Pending.
    service.checkRetries();

    // Verify all recipients are now Pending.
    auto pd = service.getPendingDelivery("prop14-msg");
    RC_ASSERT(pd.has_value());
    for (const auto& rid : recipientIds) {
        RC_ASSERT(pd->recipientStatuses.at(rid) == DeliveryStatus::Pending);
    }

    // Phase 1: Send "delivered" acks for all recipients except the last.
    // For non-last recipients that want Read, also send "read" afterwards.
    // The delivery must remain tracked throughout because the last recipient
    // is still Pending.
    for (int i = 0; i < recipientCount - 1; ++i) {
        DeliveryAckMetadata ack;
        ack.messageId = "prop14-msg";
        ack.recipientId = recipientIds[static_cast<size_t>(i)];
        ack.originalSenderNode = "sender-node";

        // Pending → Delivered
        ack.status = "delivered";
        service.handleAck(ack);

        if (wantsRead[static_cast<size_t>(i)]) {
            // Delivered → Read
            ack.status = "read";
            service.handleAck(ack);
        }

        // Delivery must still be tracked (last recipient still Pending).
        RC_ASSERT(service.getPendingCount() == 1);
    }

    // Phase 2: Send the final recipient's "delivered" ack.
    // This makes all recipients Delivered or Read → delivery is removed.
    DeliveryAckMetadata finalAck;
    finalAck.messageId = "prop14-msg";
    finalAck.recipientId = recipientIds[static_cast<size_t>(recipientCount - 1)];
    finalAck.originalSenderNode = "sender-node";
    finalAck.status = "delivered";
    service.handleAck(finalAck);

    // The delivery must now be removed from tracking.
    RC_ASSERT(service.getPendingCount() == 0);
    RC_ASSERT(service.getPendingDelivery("prop14-msg") == std::nullopt);
}

// ── Property 15: Initial tracking sets Announced status ────────────────────
// **Validates: Requirements 6.1**
// For any delivery with N recipients (1–20) and any positive initialTimeoutMs,
// after trackDelivery() all N recipients shall have Announced status and
// nextRetryAt shall equal createdAt + initialTimeoutMs.

RC_GTEST_PROP(InitialTracking,
              AllRecipientsAnnouncedWithCorrectNextRetryAt,
              ()) {
    const int recipientCount = *rc::gen::inRange(1, 21);
    const int initialTimeoutMs = *rc::gen::inRange(1, 120001);

    // Build unique recipient IDs.
    std::vector<std::string> recipientIds;
    recipientIds.reserve(recipientCount);
    for (int i = 0; i < recipientCount; ++i) {
        recipientIds.push_back("user-" + std::to_string(i));
    }

    RetryConfig config;
    config.initialTimeoutMs = initialTimeoutMs;

    RetryService service(dummyEngine(), config);

    MessageDeliveryMetadata meta;
    meta.messageId = "prop15-msg";
    meta.recipientIds = recipientIds;
    meta.priority = "normal";
    meta.blockIds = {"block-1"};
    meta.cblBlockId = "cbl-1";
    meta.ackRequired = true;

    service.trackDelivery("prop15-msg", {"block-1"}, meta);

    // Delivery must be tracked.
    RC_ASSERT(service.getPendingCount() == 1);

    auto pd = service.getPendingDelivery("prop15-msg");
    RC_ASSERT(pd.has_value());

    // All N recipients must have Announced status.
    RC_ASSERT(pd->recipientStatuses.size() == static_cast<size_t>(recipientCount));
    for (const auto& rid : recipientIds) {
        auto it = pd->recipientStatuses.find(rid);
        RC_ASSERT(it != pd->recipientStatuses.end());
        RC_ASSERT(it->second == DeliveryStatus::Announced);
    }

    // nextRetryAt must equal createdAt + initialTimeoutMs.
    auto expectedNextRetry = pd->createdAt + std::chrono::milliseconds(initialTimeoutMs);
    RC_ASSERT(pd->nextRetryAt == expectedNextRetry);

    // retryCount must be 0 (no retries yet).
    RC_ASSERT(pd->retryCount == 0);
}
