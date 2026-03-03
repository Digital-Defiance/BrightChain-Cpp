#include <gtest/gtest.h>
#include "brightchain/head_registry.hpp"
#include <filesystem>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace brightchain::db;

namespace {

// Random hex string of given length
std::string randomHex(std::mt19937& rng, size_t len) {
    static const char hexChars[] = "0123456789abcdef";
    std::uniform_int_distribution<int> dist(0, 15);
    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result += hexChars[dist(rng)];
    }
    return result;
}

// Random alphanumeric string (for keys)
std::string randomAlphaNum(std::mt19937& rng, size_t minLen, size_t maxLen) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789_-";
    std::uniform_int_distribution<size_t> lenDist(minLen, maxLen);
    std::uniform_int_distribution<int> charDist(0, sizeof(chars) - 2);
    size_t len = lenDist(rng);
    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result += chars[charDist(rng)];
    }
    return result;
}

// Generate a random registry key in "dbName:collectionName" format
std::string randomKey(std::mt19937& rng) {
    return randomAlphaNum(rng, 2, 10) + ":" + randomAlphaNum(rng, 2, 12);
}

// Generate a random blockId (128-char hex, like SHA3-512)
std::string randomBlockId(std::mt19937& rng) {
    return randomHex(rng, 128);
}

} // anonymous namespace

class HeadRegistryPropertyTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto tmpl = (std::filesystem::temp_directory_path() / "brightchain_hr_prop_XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        ASSERT_NE(mkdtemp(buf.data()), nullptr) << "mkdtemp failed";
        testDir_ = std::string(buf.data());
    }

    void TearDown() override {
        std::filesystem::remove_all(testDir_);
    }

    std::filesystem::path testDir_;
};

/**
 * Property 1: HeadRegistry persist/load round-trip
 *
 * For all valid registry states, persisting to disk and loading
 * produces an equivalent state.
 *
 * Validates: Requirements 7.3
 *
 * Strategy: Generate random registry states with varying numbers of
 * entries (0 to 50), random keys in "db:collection" format, and random
 * 128-char hex blockIds. Populate a HeadRegistry, let it persist, then
 * load into a fresh instance and verify equivalence.
 */
TEST_F(HeadRegistryPropertyTest, PersistLoadRoundTrip) {
    constexpr int numTrials = 100;
    std::mt19937 rng(42); // fixed seed for reproducibility
    std::uniform_int_distribution<size_t> sizeDist(0, 50);

    for (int trial = 0; trial < numTrials; ++trial) {
        // Clean directory for each trial
        std::filesystem::remove_all(testDir_);
        std::filesystem::create_directories(testDir_);

        // Generate random registry state
        size_t numEntries = sizeDist(rng);
        std::unordered_map<std::string, std::string> expected; // key → blockId

        HeadRegistry reg(testDir_);
        reg.load();

        for (size_t i = 0; i < numEntries; ++i) {
            std::string key = randomKey(rng);
            std::string blockId = randomBlockId(rng);
            reg.setHead(key, blockId);
            expected[key] = blockId; // last-write-wins for duplicate keys
        }

        // Verify the persisted state by loading into a fresh instance
        HeadRegistry reg2(testDir_);
        reg2.load();

        auto loadedKeys = reg2.keys();
        ASSERT_EQ(loadedKeys.size(), expected.size())
            << "Trial " << trial << ": key count mismatch";

        for (const auto& [key, blockId] : expected) {
            auto entry = reg2.getHead(key);
            ASSERT_TRUE(entry.has_value())
                << "Trial " << trial << ": missing key '" << key << "'";
            EXPECT_EQ(entry->blockId, blockId)
                << "Trial " << trial << ": blockId mismatch for key '" << key << "'";
            // Timestamp should be non-empty ISO 8601 (set by setHead)
            EXPECT_FALSE(entry->timestamp.empty())
                << "Trial " << trial << ": empty timestamp for key '" << key << "'";
        }
    }
}

/**
 * Property 1 variant: Multiple mutations then round-trip.
 *
 * Applies a random sequence of setHead, removeHead, and clear operations,
 * then verifies the final state survives persist/load.
 */
TEST_F(HeadRegistryPropertyTest, MutationSequenceRoundTrip) {
    constexpr int numTrials = 50;
    std::mt19937 rng(123);
    std::uniform_int_distribution<int> opDist(0, 2); // 0=set, 1=remove, 2=clear
    std::uniform_int_distribution<int> opCountDist(1, 30);

    for (int trial = 0; trial < numTrials; ++trial) {
        std::filesystem::remove_all(testDir_);
        std::filesystem::create_directories(testDir_);

        HeadRegistry reg(testDir_);
        reg.load();

        // Track expected state
        std::unordered_map<std::string, std::string> expected;
        // Keep a pool of keys we've used so remove can target them
        std::vector<std::string> keyPool;

        int numOps = opCountDist(rng);
        for (int op = 0; op < numOps; ++op) {
            int opType = opDist(rng);

            if (opType == 0) {
                // setHead
                std::string key = randomKey(rng);
                std::string blockId = randomBlockId(rng);
                reg.setHead(key, blockId);
                expected[key] = blockId;
                keyPool.push_back(key);
            } else if (opType == 1 && !keyPool.empty()) {
                // removeHead — pick a random existing key
                std::uniform_int_distribution<size_t> idx(0, keyPool.size() - 1);
                std::string key = keyPool[idx(rng)];
                reg.removeHead(key);
                expected.erase(key);
            } else {
                // clear
                reg.clear();
                expected.clear();
            }
        }

        // Round-trip: load into fresh instance
        HeadRegistry reg2(testDir_);
        reg2.load();

        auto loadedKeys = reg2.keys();
        ASSERT_EQ(loadedKeys.size(), expected.size())
            << "Trial " << trial << ": key count mismatch after mutation sequence";

        for (const auto& [key, blockId] : expected) {
            auto entry = reg2.getHead(key);
            ASSERT_TRUE(entry.has_value())
                << "Trial " << trial << ": missing key '" << key << "'";
            EXPECT_EQ(entry->blockId, blockId)
                << "Trial " << trial << ": blockId mismatch for key '" << key << "'";
        }
    }
}

/**
 * Property 1 variant: Empty registry round-trip.
 *
 * An empty registry persisted and loaded should remain empty.
 */
TEST_F(HeadRegistryPropertyTest, EmptyRegistryRoundTrip) {
    HeadRegistry reg(testDir_);
    reg.load();

    // Don't add anything — persist via clear (which persists)
    reg.clear();

    HeadRegistry reg2(testDir_);
    reg2.load();

    EXPECT_TRUE(reg2.keys().empty());
}
