#include <gtest/gtest.h>
#include "brightchain/collection.hpp"
#include "brightchain/disk_block_store.hpp"
#include "brightchain/head_registry.hpp"
#include "brightchain/store_lock.hpp"
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace brightchain::db;
using namespace brightchain;

namespace {

// Generate a random string value
std::string randomString(std::mt19937& rng, size_t minLen, size_t maxLen) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.";
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

// Generate a random JSON document (without _id — let Collection generate it)
Document randomDocument(std::mt19937& rng) {
    Document doc;
    std::uniform_int_distribution<int> fieldCountDist(1, 8);
    std::uniform_int_distribution<int> typeDist(0, 4);

    int numFields = fieldCountDist(rng);
    for (int i = 0; i < numFields; ++i) {
        std::string key = "field_" + std::to_string(i);
        switch (typeDist(rng)) {
            case 0: // string
                doc[key] = randomString(rng, 1, 30);
                break;
            case 1: { // integer
                std::uniform_int_distribution<int> intDist(-10000, 10000);
                doc[key] = intDist(rng);
                break;
            }
            case 2: { // double
                std::uniform_real_distribution<double> dblDist(-1000.0, 1000.0);
                doc[key] = dblDist(rng);
                break;
            }
            case 3: // bool
                doc[key] = (std::uniform_int_distribution<int>(0, 1)(rng) == 1);
                break;
            case 4: // null
                doc[key] = nullptr;
                break;
        }
    }
    return doc;
}

// Generate a random document with nested objects and arrays
Document randomComplexDocument(std::mt19937& rng) {
    Document doc = randomDocument(rng);

    // Add a nested object
    Document nested;
    nested["sub_str"] = randomString(rng, 1, 15);
    std::uniform_int_distribution<int> intDist(-1000, 1000);
    nested["sub_int"] = intDist(rng);
    doc["nested"] = nested;

    // Add an array
    std::uniform_int_distribution<int> arrLenDist(0, 5);
    int arrLen = arrLenDist(rng);
    auto arr = nlohmann::json::array();
    for (int i = 0; i < arrLen; ++i) {
        arr.push_back(intDist(rng));
    }
    doc["tags"] = arr;

    return doc;
}

} // anonymous namespace

class CollectionPropertyTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto tmpl = (std::filesystem::temp_directory_path() / "brightchain_col_prop_XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        ASSERT_NE(mkdtemp(buf.data()), nullptr);
        testDir_ = std::string(buf.data());

        store_ = std::make_unique<DiskBlockStore>(testDir_, BlockSize::Small);
        registry_ = std::make_unique<HeadRegistry>(testDir_);
        registry_->load();
        lock_ = std::make_unique<StoreLock>(testDir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(testDir_);
    }

    Collection makeCollection(const std::string& name = "prop_test_col") {
        return Collection(name, *store_, "testdb", *registry_, *lock_, BlockSize::Small);
    }

    std::filesystem::path testDir_;
    std::unique_ptr<DiskBlockStore> store_;
    std::unique_ptr<HeadRegistry> registry_;
    std::unique_ptr<StoreLock> lock_;
};

/**
 * Property 2: Document insert/retrieve round-trip
 *
 * For all valid documents, inserting into a Collection and retrieving
 * by DocumentId produces an equivalent document.
 *
 * Validates: Requirements 7.1
 *
 * Strategy: Generate random documents with varying field counts and types,
 * insert each into a Collection, then retrieve by _id and verify the
 * retrieved document equals the original (with _id added).
 */
TEST_F(CollectionPropertyTest, InsertRetrieveRoundTrip) {
    constexpr int numTrials = 100;
    std::mt19937 rng(42);

    auto col = makeCollection();

    for (int trial = 0; trial < numTrials; ++trial) {
        Document original = randomDocument(rng);

        auto result = col.insertOne(original);
        DocumentId id = result.insertedId;

        // The inserted doc should now have _id set
        ASSERT_FALSE(id.empty()) << "Trial " << trial << ": empty insertedId";

        auto retrieved = col.findOne({{"_id", id}});
        ASSERT_TRUE(retrieved.has_value())
            << "Trial " << trial << ": document not found after insert, _id=" << id;

        // Build expected: original + _id field
        Document expected = original;
        expected["_id"] = id;

        EXPECT_EQ(*retrieved, expected)
            << "Trial " << trial << ": round-trip mismatch for _id=" << id
            << "\nExpected: " << expected.dump()
            << "\nRetrieved: " << retrieved->dump();
    }
}

/**
 * Property 2 variant: Complex documents with nested objects and arrays.
 */
TEST_F(CollectionPropertyTest, InsertRetrieveRoundTripComplex) {
    constexpr int numTrials = 50;
    std::mt19937 rng(99);

    auto col = makeCollection("complex_col");

    for (int trial = 0; trial < numTrials; ++trial) {
        Document original = randomComplexDocument(rng);

        auto result = col.insertOne(original);
        DocumentId id = result.insertedId;

        auto retrieved = col.findOne({{"_id", id}});
        ASSERT_TRUE(retrieved.has_value())
            << "Trial " << trial << ": complex doc not found, _id=" << id;

        Document expected = original;
        expected["_id"] = id;

        EXPECT_EQ(*retrieved, expected)
            << "Trial " << trial << ": complex round-trip mismatch for _id=" << id;
    }
}

/**
 * Property 2 variant: Documents with explicit _id survive round-trip.
 *
 * When a document already has an _id, the Collection should preserve it
 * exactly and the retrieved document should match.
 */
TEST_F(CollectionPropertyTest, InsertRetrieveRoundTripWithExplicitId) {
    constexpr int numTrials = 50;
    std::mt19937 rng(77);

    auto col = makeCollection("explicit_id_col");

    for (int trial = 0; trial < numTrials; ++trial) {
        Document original = randomDocument(rng);
        // Set an explicit _id
        std::string explicitId = "explicit_" + std::to_string(trial);
        original["_id"] = explicitId;

        auto result = col.insertOne(original);
        EXPECT_EQ(result.insertedId, explicitId)
            << "Trial " << trial << ": insertedId should match explicit _id";

        auto retrieved = col.findOne({{"_id", explicitId}});
        ASSERT_TRUE(retrieved.has_value())
            << "Trial " << trial << ": doc with explicit _id not found";

        EXPECT_EQ(*retrieved, original)
            << "Trial " << trial << ": round-trip mismatch with explicit _id";
    }
}

/**
 * Property 2 variant: Persistence round-trip across Collection instances.
 *
 * Documents inserted in one Collection instance should be retrievable
 * from a fresh Collection instance backed by the same store.
 */
TEST_F(CollectionPropertyTest, InsertRetrieveRoundTripAcrossInstances) {
    constexpr int numTrials = 30;
    std::mt19937 rng(55);

    std::vector<std::pair<DocumentId, Document>> inserted;

    // Insert documents in one instance
    {
        auto col = makeCollection("persist_prop_col");
        for (int trial = 0; trial < numTrials; ++trial) {
            Document original = randomDocument(rng);
            auto result = col.insertOne(original);
            original["_id"] = result.insertedId;
            inserted.emplace_back(result.insertedId, original);
        }
    }

    // Retrieve from a fresh instance
    auto col2 = makeCollection("persist_prop_col");
    for (const auto& [id, expected] : inserted) {
        auto retrieved = col2.findOne({{"_id", id}});
        ASSERT_TRUE(retrieved.has_value())
            << "Doc not found in fresh instance, _id=" << id;
        EXPECT_EQ(*retrieved, expected)
            << "Persistence round-trip mismatch for _id=" << id;
    }
}
