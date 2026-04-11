// Feature: cpp-gossip-protocol, Property 2: GossipConfig validation
// **Validates: Requirements 1.8, 1.9**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/gossip_config.hpp>

using namespace brightchain::gossip;

// ── RapidCheck generator for GossipConfig ──────────────────────────────────

namespace rc {

template <>
struct Arbitrary<GossipConfig> {
    static Gen<GossipConfig> arbitrary() {
        return gen::exec([] {
            GossipConfig cfg;
            cfg.fanout = *gen::inRange(-5, 20);
            cfg.defaultTtl = *gen::inRange(-5, 20);
            cfg.batchIntervalMs = *gen::inRange(100, 5000);
            cfg.maxBatchSize = *gen::inRange(1, 500);
            cfg.messagePriority.normal.fanout = *gen::inRange(-5, 20);
            cfg.messagePriority.normal.ttl = *gen::inRange(-5, 20);
            cfg.messagePriority.high.fanout = *gen::inRange(-5, 20);
            cfg.messagePriority.high.ttl = *gen::inRange(-5, 20);
            return cfg;
        });
    }
};

} // namespace rc

// ── Property test ──────────────────────────────────────────────────────────

RC_GTEST_PROP(GossipConfigValidation,
              ValidateReturnsTrueIffAllFanoutAndTtlPositive,
              (GossipConfig cfg)) {
    // Property 2: validate() returns true iff all fanout >= 1 and all TTL >= 1
    const bool expected =
        cfg.fanout >= 1
        && cfg.defaultTtl >= 1
        && cfg.messagePriority.normal.fanout >= 1
        && cfg.messagePriority.normal.ttl >= 1
        && cfg.messagePriority.high.fanout >= 1
        && cfg.messagePriority.high.ttl >= 1;

    RC_ASSERT(GossipConfig::validate(cfg) == expected);
}

// ── Property 3: GossipConfig JSON round-trip ───────────────────────────────
// **Validates: Requirements 14.2**

static rc::Gen<GossipConfig> genValidGossipConfig() {
    return rc::gen::exec([] {
        GossipConfig cfg;
        cfg.fanout = *rc::gen::inRange(1, 20);
        cfg.defaultTtl = *rc::gen::inRange(1, 20);
        cfg.batchIntervalMs = *rc::gen::inRange(100, 5000);
        cfg.maxBatchSize = *rc::gen::inRange(1, 500);
        cfg.messagePriority.normal.fanout = *rc::gen::inRange(1, 20);
        cfg.messagePriority.normal.ttl = *rc::gen::inRange(1, 20);
        cfg.messagePriority.high.fanout = *rc::gen::inRange(1, 20);
        cfg.messagePriority.high.ttl = *rc::gen::inRange(1, 20);
        return cfg;
    });
}

RC_GTEST_PROP(GossipConfigRoundTrip,
              JsonRoundTrip,
              ()) {
    // Property 3: fromJson(toJson(config)) == config for all valid configs
    auto config = *genValidGossipConfig();
    auto json = config.toJson();
    auto restored = GossipConfig::fromJson(json);
    RC_ASSERT(restored == config);
}
