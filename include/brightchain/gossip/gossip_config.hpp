#pragma once

#include <nlohmann/json.hpp>

namespace brightchain::gossip {

struct PriorityGossipConfig {
    int fanout = 0;
    int ttl = 0;

    bool operator==(const PriorityGossipConfig& other) const = default;

    nlohmann::json toJson() const;
    static PriorityGossipConfig fromJson(const nlohmann::json& j);
};

struct GossipConfig {
    int fanout = 3;
    int defaultTtl = 3;
    int batchIntervalMs = 1000;
    int maxBatchSize = 100;

    struct MessagePriority {
        PriorityGossipConfig normal{5, 5};
        PriorityGossipConfig high{7, 7};

        bool operator==(const MessagePriority& other) const = default;
    } messagePriority;

    bool operator==(const GossipConfig& other) const = default;

    nlohmann::json toJson() const;
    static GossipConfig fromJson(const nlohmann::json& j);

    /// Returns true iff all fanout and TTL values are >= 1.
    static bool validate(const GossipConfig& config);
};

} // namespace brightchain::gossip
