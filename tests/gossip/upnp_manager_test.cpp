// Feature: cpp-gossip-protocol, Task 19.1: UpnpManager unit tests
// Tests: config validation, default values, retry delay calculation, JSON round-trip
// Requirements: 12.1–12.7

#include <gtest/gtest.h>

#include <brightchain/gossip/upnp_manager.hpp>

#include <cmath>

using namespace brightchain::gossip;

// ── Default config values ──────────────────────────────────────────────────

TEST(UpnpConfigTest, DefaultValues) {
    UpnpConfig config;
    EXPECT_FALSE(config.enabled);
    EXPECT_EQ(config.httpPort, 3000);
    EXPECT_EQ(config.websocketPort, 3000);
    EXPECT_EQ(config.ttlSeconds, 3600);
    EXPECT_EQ(config.refreshIntervalMs, 1800000);
    EXPECT_EQ(config.retryAttempts, 3);
    EXPECT_EQ(config.retryDelayMs, 5000);
}

// ── Config validation ──────────────────────────────────────────────────────

TEST(UpnpConfigTest, ValidDefaultConfig) {
    UpnpConfig config;
    // Default: refreshIntervalMs=1800000 < ttlSeconds*1000=3600000
    EXPECT_TRUE(UpnpConfig::validate(config));
}

TEST(UpnpConfigTest, ValidCustomConfig) {
    UpnpConfig config;
    config.enabled = true;
    config.ttlSeconds = 7200;
    config.refreshIntervalMs = 3600000; // 3600000 < 7200*1000=7200000
    EXPECT_TRUE(UpnpConfig::validate(config));
}

TEST(UpnpConfigTest, InvalidRefreshExceedsTtl) {
    UpnpConfig config;
    config.ttlSeconds = 100;
    config.refreshIntervalMs = 200000; // 200000 >= 100*1000=100000
    EXPECT_FALSE(UpnpConfig::validate(config));
}

TEST(UpnpConfigTest, InvalidRefreshEqualsTtl) {
    UpnpConfig config;
    config.ttlSeconds = 100;
    config.refreshIntervalMs = 100000; // equal, not strictly less
    EXPECT_FALSE(UpnpConfig::validate(config));
}

TEST(UpnpConfigTest, InvalidZeroTtl) {
    UpnpConfig config;
    config.ttlSeconds = 0;
    config.refreshIntervalMs = 1000;
    EXPECT_FALSE(UpnpConfig::validate(config));
}

TEST(UpnpConfigTest, InvalidNegativeTtl) {
    UpnpConfig config;
    config.ttlSeconds = -1;
    config.refreshIntervalMs = 1000;
    EXPECT_FALSE(UpnpConfig::validate(config));
}

TEST(UpnpConfigTest, InvalidZeroRefreshInterval) {
    UpnpConfig config;
    config.ttlSeconds = 3600;
    config.refreshIntervalMs = 0;
    EXPECT_FALSE(UpnpConfig::validate(config));
}

TEST(UpnpConfigTest, InvalidNegativeRetryAttempts) {
    UpnpConfig config;
    config.retryAttempts = -1;
    EXPECT_FALSE(UpnpConfig::validate(config));
}

TEST(UpnpConfigTest, InvalidNegativeRetryDelay) {
    UpnpConfig config;
    config.retryDelayMs = -1;
    EXPECT_FALSE(UpnpConfig::validate(config));
}

// ── JSON round-trip ────────────────────────────────────────────────────────

TEST(UpnpConfigTest, JsonRoundTripDefault) {
    UpnpConfig original;
    auto json = original.toJson();
    auto restored = UpnpConfig::fromJson(json);
    EXPECT_EQ(original, restored);
}

TEST(UpnpConfigTest, JsonRoundTripCustom) {
    UpnpConfig original;
    original.enabled = true;
    original.httpPort = 8080;
    original.websocketPort = 8081;
    original.ttlSeconds = 7200;
    original.refreshIntervalMs = 3000000;
    original.retryAttempts = 5;
    original.retryDelayMs = 10000;

    auto json = original.toJson();
    auto restored = UpnpConfig::fromJson(json);
    EXPECT_EQ(original, restored);
}

TEST(UpnpConfigTest, JsonFieldNames) {
    UpnpConfig config;
    config.enabled = true;
    config.httpPort = 4000;
    auto json = config.toJson();

    EXPECT_TRUE(json.contains("enabled"));
    EXPECT_TRUE(json.contains("httpPort"));
    EXPECT_TRUE(json.contains("websocketPort"));
    EXPECT_TRUE(json.contains("ttlSeconds"));
    EXPECT_TRUE(json.contains("refreshIntervalMs"));
    EXPECT_TRUE(json.contains("retryAttempts"));
    EXPECT_TRUE(json.contains("retryDelayMs"));
}

// ── Retry delay calculation ────────────────────────────────────────────────

TEST(UpnpManagerTest, RetryDelayAttemptZero) {
    UpnpConfig config;
    config.retryDelayMs = 5000;
    UpnpManager manager(config);

    // attempt 0: 5000 * 2^0 = 5000
    EXPECT_EQ(manager.calculateRetryDelay(0), 5000);
}

TEST(UpnpManagerTest, RetryDelayAttemptOne) {
    UpnpConfig config;
    config.retryDelayMs = 5000;
    UpnpManager manager(config);

    // attempt 1: 5000 * 2^1 = 10000
    EXPECT_EQ(manager.calculateRetryDelay(1), 10000);
}

TEST(UpnpManagerTest, RetryDelayAttemptTwo) {
    UpnpConfig config;
    config.retryDelayMs = 5000;
    UpnpManager manager(config);

    // attempt 2: 5000 * 2^2 = 20000
    EXPECT_EQ(manager.calculateRetryDelay(2), 20000);
}

TEST(UpnpManagerTest, RetryDelayCustomBase) {
    UpnpConfig config;
    config.retryDelayMs = 1000;
    UpnpManager manager(config);

    EXPECT_EQ(manager.calculateRetryDelay(0), 1000);
    EXPECT_EQ(manager.calculateRetryDelay(1), 2000);
    EXPECT_EQ(manager.calculateRetryDelay(2), 4000);
    EXPECT_EQ(manager.calculateRetryDelay(3), 8000);
}

// ── Accessor tests ─────────────────────────────────────────────────────────

TEST(UpnpManagerTest, DisabledByDefault) {
    UpnpConfig config;
    UpnpManager manager(config);

    // Not initialized, so no external IP.
    EXPECT_FALSE(manager.getExternalIp().has_value());
    EXPECT_EQ(manager.getMappedHttpPort(), 3000);
    EXPECT_EQ(manager.getMappedWsPort(), 3000);
    EXPECT_FALSE(manager.getConfig().enabled);
}

TEST(UpnpManagerTest, ConfigAccessor) {
    UpnpConfig config;
    config.enabled = true;
    config.httpPort = 9000;
    config.websocketPort = 9001;
    UpnpManager manager(config);

    EXPECT_TRUE(manager.getConfig().enabled);
    EXPECT_EQ(manager.getConfig().httpPort, 9000);
    EXPECT_EQ(manager.getConfig().websocketPort, 9001);
}
