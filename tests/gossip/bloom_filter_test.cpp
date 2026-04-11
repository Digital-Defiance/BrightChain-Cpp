// Feature: cpp-gossip-protocol, Property 26: Bloom filter membership
// **Validates: Requirements 4.1**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <brightchain/gossip/bloom_filter.hpp>

#include <set>
#include <string>

using namespace brightchain::gossip;

// ── RapidCheck generator helpers ───────────────────────────────────────────

/// Generate a random hex-like block ID string (8–64 hex chars).
static rc::Gen<std::string> genBlockId() {
    return rc::gen::exec([] {
        static const char hexChars[] = "0123456789abcdef";
        int len = *rc::gen::inRange(8, 65);
        std::string id;
        id.reserve(static_cast<size_t>(len));
        for (int i = 0; i < len; ++i) {
            id += hexChars[*rc::gen::inRange(0, 16)];
        }
        return id;
    });
}

/// Generate a non-empty set of unique block IDs (1–50 items).
static rc::Gen<std::vector<std::string>> genBlockIdSet() {
    return rc::gen::exec([] {
        int count = *rc::gen::inRange(1, 51);
        std::set<std::string> seen;
        std::vector<std::string> ids;
        while (static_cast<int>(ids.size()) < count) {
            auto id = *genBlockId();
            if (seen.insert(id).second) {
                ids.push_back(std::move(id));
            }
        }
        return ids;
    });
}

// ── Property 26: Bloom filter membership (no false negatives) ──────────────

RC_GTEST_PROP(BloomFilterMembership,
              NoFalseNegatives,
              ()) {
    // Property 26: For any set of block IDs added to a Bloom filter,
    // mightContain(blockId) returns true for every added ID.
    auto ids = *genBlockIdSet();

    BloomFilter bf(ids.size(), 0.01, 7);
    for (const auto& id : ids) {
        bf.add(id);
    }

    for (const auto& id : ids) {
        RC_ASSERT(bf.mightContain(id));
    }
}

// ── Serialization round-trip unit test ─────────────────────────────────────

TEST(BloomFilterSerialization, RoundTrip) {
    BloomFilter bf(100, 0.01, 7);
    bf.add("block-aaa");
    bf.add("block-bbb");
    bf.add("block-ccc");

    auto data = bf.serialize();
    auto restored = BloomFilter::deserialize(data);

    EXPECT_TRUE(restored.mightContain("block-aaa"));
    EXPECT_TRUE(restored.mightContain("block-bbb"));
    EXPECT_TRUE(restored.mightContain("block-ccc"));
}

TEST(BloomFilterSerialization, EmptyFilterRoundTrip) {
    BloomFilter bf(50, 0.01, 7);

    auto data = bf.serialize();
    ASSERT_GE(data.size(), 4u);

    auto restored = BloomFilter::deserialize(data);
    // An empty filter should not claim to contain anything (with high probability)
    EXPECT_FALSE(restored.mightContain("nonexistent-block"));
}

TEST(BloomFilterSerialization, InvalidDataThrows) {
    // Too short
    std::vector<uint8_t> tooShort = {0x01, 0x00};
    EXPECT_THROW(BloomFilter::deserialize(tooShort), std::invalid_argument);

    // Header says 64 bits (8 bytes) but only 4 bytes of data follow
    std::vector<uint8_t> truncated = {0x40, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_THROW(BloomFilter::deserialize(truncated), std::invalid_argument);
}
