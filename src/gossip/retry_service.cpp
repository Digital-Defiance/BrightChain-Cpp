#include <brightchain/gossip/retry_service.hpp>

#include <algorithm>
#include <cmath>

namespace brightchain::gossip {

// ── Construction ───────────────────────────────────────────────────────────

RetryService::RetryService(GossipEngine& engine, RetryConfig config)
    : engine_(engine), config_(std::move(config)) {}

// ── trackDelivery ──────────────────────────────────────────────────────────

void RetryService::trackDelivery(const std::string& messageId,
                                 const std::vector<std::string>& blockIds,
                                 const MessageDeliveryMetadata& metadata) {
    std::unique_lock lock(deliveryMutex_);

    PendingDelivery pd;
    pd.messageId = messageId;
    pd.blockIds = blockIds;
    pd.metadata = metadata;
    pd.retryCount = 0;
    pd.createdAt = std::chrono::steady_clock::now();
    pd.nextRetryAt = pd.createdAt + std::chrono::milliseconds(config_.initialTimeoutMs);

    // Set every recipient to Announced.
    for (const auto& recipientId : metadata.recipientIds) {
        pd.recipientStatuses[recipientId] = DeliveryStatus::Announced;
    }

    pendingDeliveries_.emplace(messageId, std::move(pd));
}

// ── handleAck ──────────────────────────────────────────────────────────────

void RetryService::handleAck(const DeliveryAckMetadata& ack) {
    std::unique_lock lock(deliveryMutex_);

    auto it = pendingDeliveries_.find(ack.messageId);
    if (it == pendingDeliveries_.end()) {
        return; // unknown delivery — nothing to do
    }

    auto& pd = it->second;
    auto recipIt = pd.recipientStatuses.find(ack.recipientId);
    if (recipIt == pd.recipientStatuses.end()) {
        return; // unknown recipient
    }

    // Map ack status string to DeliveryStatus enum.
    DeliveryStatus newStatus;
    if (ack.status == "delivered") {
        newStatus = DeliveryStatus::Delivered;
    } else if (ack.status == "read") {
        newStatus = DeliveryStatus::Read;
    } else if (ack.status == "failed") {
        newStatus = DeliveryStatus::Failed;
    } else if (ack.status == "bounced") {
        newStatus = DeliveryStatus::Bounced;
    } else {
        return; // unrecognised status string
    }

    // Validate the transition before applying.
    if (!validateTransition(recipIt->second, newStatus)) {
        return;
    }

    recipIt->second = newStatus;

    // Remove delivery when ALL recipients have reached Delivered or Read.
    bool allDone = std::all_of(
        pd.recipientStatuses.begin(), pd.recipientStatuses.end(),
        [](const auto& pair) {
            return pair.second == DeliveryStatus::Delivered
                || pair.second == DeliveryStatus::Read;
        });

    if (allDone) {
        pendingDeliveries_.erase(it);
    }
}

// ── checkRetries ───────────────────────────────────────────────────────────

void RetryService::checkRetries() {
    std::unique_lock lock(deliveryMutex_);

    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> toRemove;

    for (auto& [msgId, pd] : pendingDeliveries_) {
        if (now < pd.nextRetryAt) {
            continue; // not yet due
        }

        pd.retryCount++;

        if (pd.retryCount > config_.maxRetries) {
            // Max retries exhausted — mark unacknowledged recipients as Failed.
            std::vector<std::string> failedRecipients;
            for (auto& [recipId, status] : pd.recipientStatuses) {
                if (status == DeliveryStatus::Announced
                    || status == DeliveryStatus::Pending) {
                    status = DeliveryStatus::Failed;
                    failedRecipients.push_back(recipId);
                }
            }

            // Notify failure handlers.
            for (const auto& handler : failureHandlers_) {
                handler(msgId, failedRecipients);
            }

            toRemove.push_back(msgId);
            continue;
        }

        // Transition Announced recipients to Pending (first retry).
        for (auto& [recipId, status] : pd.recipientStatuses) {
            if (status == DeliveryStatus::Announced) {
                status = DeliveryStatus::Pending;
            }
        }

        // Schedule next retry with exponential backoff.
        int backoffMs = calculateBackoff(pd.retryCount);
        pd.nextRetryAt = now + std::chrono::milliseconds(backoffMs);

        // NOTE: actual re-announcement via engine_ will be wired in a later
        // integration task (21.2).  At this stage the RetryService only
        // manages state and scheduling.
    }

    for (const auto& msgId : toRemove) {
        pendingDeliveries_.erase(msgId);
    }
}

// ── start / stop ───────────────────────────────────────────────────────────

void RetryService::start(boost::asio::io_context& ioc) {
    if (running_) return;
    running_ = true;
    retryTimer_ = std::make_unique<boost::asio::steady_timer>(ioc);
    scheduleRetryTimer();
}

void RetryService::stop() {
    running_ = false;
    if (retryTimer_) {
        retryTimer_->cancel();
        retryTimer_.reset();
    }
}

void RetryService::scheduleRetryTimer() {
    if (!running_ || !retryTimer_) return;

    retryTimer_->expires_after(std::chrono::seconds(1));
    retryTimer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec || !running_) return;
        checkRetries();
        scheduleRetryTimer();
    });
}

// ── Queries ────────────────────────────────────────────────────────────────

std::optional<PendingDelivery> RetryService::getPendingDelivery(
    const std::string& messageId) const {
    std::shared_lock lock(deliveryMutex_);
    auto it = pendingDeliveries_.find(messageId);
    if (it == pendingDeliveries_.end()) return std::nullopt;
    return it->second;
}

size_t RetryService::getPendingCount() const {
    std::shared_lock lock(deliveryMutex_);
    return pendingDeliveries_.size();
}

const RetryConfig& RetryService::getConfig() const {
    return config_;
}

// ── Failure handler registration ───────────────────────────────────────────

void RetryService::onDeliveryFailed(FailureHandler handler) {
    failureHandlers_.push_back(std::move(handler));
}

// ── calculateBackoff ───────────────────────────────────────────────────────

int RetryService::calculateBackoff(int retryCount) const {
    // backoff = min(initialTimeoutMs * multiplier^(retryCount - 1), maxBackoffMs)
    // retryCount is 1-based here (first retry = 1).
    if (retryCount <= 0) return config_.initialTimeoutMs;

    double delay = static_cast<double>(config_.initialTimeoutMs)
                 * std::pow(static_cast<double>(config_.backoffMultiplier),
                            static_cast<double>(retryCount - 1));

    if (delay > static_cast<double>(config_.maxBackoffMs)) {
        return config_.maxBackoffMs;
    }
    return static_cast<int>(delay);
}

// ── validateTransition ─────────────────────────────────────────────────────

bool RetryService::validateTransition(DeliveryStatus from,
                                      DeliveryStatus to) const {
    switch (from) {
    case DeliveryStatus::Announced:
        return to == DeliveryStatus::Pending;
    case DeliveryStatus::Pending:
        return to == DeliveryStatus::Delivered
            || to == DeliveryStatus::Failed
            || to == DeliveryStatus::Bounced;
    case DeliveryStatus::Delivered:
        return to == DeliveryStatus::Read;
    case DeliveryStatus::Read:
    case DeliveryStatus::Failed:
    case DeliveryStatus::Bounced:
        return false; // terminal states
    }
    return false;
}

} // namespace brightchain::gossip
