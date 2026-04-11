#pragma once

#include <brightchain/gossip/block_announcement.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

namespace brightchain::gossip {

// Forward declaration — full definition lives in gossip_engine.hpp.
class GossipEngine;

/// Delivery status state machine:
///   Announced → Pending
///   Pending   → Delivered | Failed | Bounced
///   Delivered → Read
enum class DeliveryStatus : uint8_t {
    Announced,
    Pending,
    Delivered,
    Read,
    Failed,
    Bounced
};

/// Configuration for the retry service.
struct RetryConfig {
    int initialTimeoutMs = 30000;   // 30 s
    int backoffMultiplier = 2;
    int maxRetries = 5;
    int maxBackoffMs = 240000;      // 240 s
};

/// A delivery that is being tracked for retry.
struct PendingDelivery {
    std::string messageId;
    std::vector<std::string> blockIds;
    MessageDeliveryMetadata metadata;
    std::unordered_map<std::string, DeliveryStatus> recipientStatuses;
    int retryCount = 0;
    std::chrono::steady_clock::time_point nextRetryAt;
    std::chrono::steady_clock::time_point createdAt;
};

/// Tracks pending message deliveries and retries unacknowledged deliveries
/// with exponential backoff.  Uses boost::asio::steady_timer for periodic
/// retry checks (every 1 second).
class RetryService {
public:
    explicit RetryService(GossipEngine& engine, RetryConfig config = {});

    /// Begin tracking a new delivery.  Sets all recipients to Announced and
    /// schedules the first retry after initialTimeoutMs.
    void trackDelivery(const std::string& messageId,
                       const std::vector<std::string>& blockIds,
                       const MessageDeliveryMetadata& metadata);

    /// Process an incoming delivery acknowledgment.  Updates recipient status
    /// and removes the delivery when all recipients reach Delivered or Read.
    void handleAck(const DeliveryAckMetadata& ack);

    /// Check all pending deliveries for retries.  Public for testing.
    void checkRetries();

    /// Start the periodic retry timer (checks every 1 second).
    void start(boost::asio::io_context& ioc);

    /// Stop the retry timer.
    void stop();

    /// Query a specific pending delivery by messageId.
    [[nodiscard]] std::optional<PendingDelivery> getPendingDelivery(
        const std::string& messageId) const;

    /// Number of deliveries currently being tracked.
    [[nodiscard]] size_t getPendingCount() const;

    /// Access the current retry configuration.
    [[nodiscard]] const RetryConfig& getConfig() const;

    /// Callback invoked when a delivery exhausts all retries.
    using FailureHandler = std::function<void(const std::string& messageId,
                                              const std::vector<std::string>& failedRecipients)>;
    void onDeliveryFailed(FailureHandler handler);

    // ── Helpers exposed for property-based testing ──────────────────────

    /// Compute the backoff delay in milliseconds for a given retry count.
    [[nodiscard]] int calculateBackoff(int retryCount) const;

    /// Return true if the transition from → to is valid per the state machine.
    [[nodiscard]] bool validateTransition(DeliveryStatus from, DeliveryStatus to) const;

private:
    void scheduleRetryTimer();

    GossipEngine& engine_;
    RetryConfig config_;
    mutable std::shared_mutex deliveryMutex_;
    std::unordered_map<std::string, PendingDelivery> pendingDeliveries_;
    std::unique_ptr<boost::asio::steady_timer> retryTimer_;
    std::vector<FailureHandler> failureHandlers_;
    bool running_ = false;
};

} // namespace brightchain::gossip
