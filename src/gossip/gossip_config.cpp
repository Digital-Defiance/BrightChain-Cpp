#include <brightchain/gossip/gossip_config.hpp>

namespace brightchain::gossip {

// ── PriorityGossipConfig ───────────────────────────────────────────────────

nlohmann::json PriorityGossipConfig::toJson() const {
    return {
        {"fanout", fanout},
        {"ttl", ttl},
    };
}

PriorityGossipConfig PriorityGossipConfig::fromJson(const nlohmann::json& j) {
    return {
        j.at("fanout").get<int>(),
        j.at("ttl").get<int>(),
    };
}

// ── GossipConfig ───────────────────────────────────────────────────────────

nlohmann::json GossipConfig::toJson() const {
    return {
        {"fanout", fanout},
        {"defaultTtl", defaultTtl},
        {"batchIntervalMs", batchIntervalMs},
        {"maxBatchSize", maxBatchSize},
        {"messagePriority", {
            {"normal", messagePriority.normal.toJson()},
            {"high", messagePriority.high.toJson()},
        }},
    };
}

GossipConfig GossipConfig::fromJson(const nlohmann::json& j) {
    GossipConfig c;
    c.fanout = j.at("fanout").get<int>();
    c.defaultTtl = j.at("defaultTtl").get<int>();
    c.batchIntervalMs = j.at("batchIntervalMs").get<int>();
    c.maxBatchSize = j.at("maxBatchSize").get<int>();

    const auto& mp = j.at("messagePriority");
    c.messagePriority.normal = PriorityGossipConfig::fromJson(mp.at("normal"));
    c.messagePriority.high = PriorityGossipConfig::fromJson(mp.at("high"));

    return c;
}

bool GossipConfig::validate(const GossipConfig& config) {
    return config.fanout >= 1
        && config.defaultTtl >= 1
        && config.messagePriority.normal.fanout >= 1
        && config.messagePriority.normal.ttl >= 1
        && config.messagePriority.high.fanout >= 1
        && config.messagePriority.high.ttl >= 1;
}

} // namespace brightchain::gossip
