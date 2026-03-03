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

std::string randomAlphaNum(std::mt19937& rng, size_t minLen, size_t maxLen) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
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

Document randomDocument(std::mt19937& rng) {
    Document doc;
    std::uniform_int_distribution<int> fieldCountDist(1, 5);
    std::uniform_int_distribution<int> typeDist(0, 3);
    int numFields = fieldCountDist(rng);
    for (int i = 0; i < numFields; ++i) {
        std::string key = "f" + std::to_string(i);
        switch (typeDist(rng)) {
            case 0: doc[key] = randomAlphaNum(rng, 1, 20); break;
            case 1: doc[key] = std::uniform_int_distribution<int>(-1000, 1000)(rng); break;
            case 2: doc[key] = (std::uniform_int_distribution<int>(0, 1)(rng) == 1); break;
            default: doc[key] = nullptr; break;
        }
    }
    return doc;
}

IndexSpec randomIndexSpec(std::mt19937& rng, int idx) {
    IndexSpec spec;
    spec.name = "idx_" + std::to_string(idx);
    spec.spec = Document{{randomAlphaNum(rng, 2, 8), 1}};
    spec.unique = (std::uniform_int_distribution<int>(0, 1)(rng) == 1);
    spec.sparse = (std::uniform_int_distribution<int>(0, 1)(rng) == 1);
    return spec;
}

} // anonymous namespace

class CollectionMetaPropertyTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto tmpl = (std::filesystem::temp_directory_path() / "brightchain_cmeta_prop_XXXXXX").string();
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

    Collection makeCollection(const std::string& name = "meta_prop_col") {
        return Collection(name, *store_, "testdb", *registry_, *lock_, BlockSize::Small);
    }

    std::filesystem::path testDir_;
    std::unique_ptr<DiskBlockStore> store_;
    std::unique_ptr<HeadRegistry> registry_;
    std::unique_ptr<StoreLock> lock_;
};

/**
 * Property 3: CollectionMeta serialize/deserialize round-trip
 *
 * For all valid CollectionMeta objects, serializing to JSON and
 * deserializing produces an equivalent object.
 *
 * Validates: Requirements 7.2
 *
 * Strategy: Build random CollectionMeta states by inserting random
 * documents and creating random indexes in a Collection. The Collection
 * persists CollectionMeta (mappings + indexes) as a block. A fresh
 * Collection instance loads that block and reconstructs the state.
 * We verify that the document mappings and index metadata survive
 * the round-trip exactly.
 */
TEST_F(CollectionMetaPropertyTest, MappingsAndIndexesRoundTrip) {
    constexpr int numTrials = 50;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> docCountDist(0, 20);
    std::uniform_int_distribution<int> idxCountDist(0, 5);

    for (int trial = 0; trial < numTrials; ++trial) {
        // Use a unique collection name per trial to avoid cross-contamination
        std::string colName = "meta_trial_" + std::to_string(trial);

        // Track what we insert
        std::vector<std::pair<DocumentId, Document>> insertedDocs;
        std::vector<IndexSpec> createdIndexes;

        // Build the CollectionMeta state
        {
            auto col = makeCollection(colName);

            int numDocs = docCountDist(rng);
            for (int d = 0; d < numDocs; ++d) {
                Document doc = randomDocument(rng);
                auto result = col.insertOne(doc);
                doc["_id"] = result.insertedId;
                insertedDocs.emplace_back(result.insertedId, doc);
            }

            int numIndexes = idxCountDist(rng);
            for (int i = 0; i < numIndexes; ++i) {
                IndexSpec idx = randomIndexSpec(rng, i);
                col.createIndex(idx);
                createdIndexes.push_back(idx);
            }
        }

        // Round-trip: load into a fresh Collection instance
        auto col2 = makeCollection(colName);

        // Verify all documents are retrievable with correct content
        auto allDocs = col2.find({});
        ASSERT_EQ(allDocs.size(), insertedDocs.size())
            << "Trial " << trial << ": document count mismatch";

        for (const auto& [id, expectedDoc] : insertedDocs) {
            auto retrieved = col2.findOne({{"_id", id}});
            ASSERT_TRUE(retrieved.has_value())
                << "Trial " << trial << ": missing doc _id=" << id;
            EXPECT_EQ(*retrieved, expectedDoc)
                << "Trial " << trial << ": doc mismatch for _id=" << id;
        }

        // Verify indexes survived the round-trip
        auto loadedIndexes = col2.listIndexes();
        ASSERT_EQ(loadedIndexes.size(), createdIndexes.size())
            << "Trial " << trial << ": index count mismatch";

        for (size_t i = 0; i < createdIndexes.size(); ++i) {
            EXPECT_EQ(loadedIndexes[i].name, createdIndexes[i].name)
                << "Trial " << trial << ": index name mismatch at " << i;
            EXPECT_EQ(loadedIndexes[i].spec, createdIndexes[i].spec)
                << "Trial " << trial << ": index spec mismatch at " << i;
            EXPECT_EQ(loadedIndexes[i].unique, createdIndexes[i].unique)
                << "Trial " << trial << ": index unique mismatch at " << i;
            EXPECT_EQ(loadedIndexes[i].sparse, createdIndexes[i].sparse)
                << "Trial " << trial << ": index sparse mismatch at " << i;
        }
    }
}

/**
 * Property 3 variant: Empty CollectionMeta round-trip.
 *
 * A collection with no documents and no indexes should persist and
 * reload as empty.
 */
TEST_F(CollectionMetaPropertyTest, EmptyMetaRoundTrip) {
    // Create a collection, insert one doc then delete it to force a persist
    {
        auto col = makeCollection("empty_meta");
        auto result = col.insertOne({{"_id", "temp"}, {"val", 1}});
        col.deleteOne({{"_id", "temp"}});
    }

    auto col2 = makeCollection("empty_meta");
    auto docs = col2.find({});
    EXPECT_TRUE(docs.empty());
    EXPECT_TRUE(col2.listIndexes().empty());
}

/**
 * Property 3 variant: Mutations then round-trip.
 *
 * Apply a random sequence of inserts, deletes, and index operations,
 * then verify the final CollectionMeta state survives persist/load.
 */
TEST_F(CollectionMetaPropertyTest, MutationSequenceRoundTrip) {
    constexpr int numTrials = 30;
    std::mt19937 rng(123);
    std::uniform_int_distribution<int> opDist(0, 3); // insert, delete, addIndex, dropIndex
    std::uniform_int_distribution<int> opCountDist(5, 25);

    for (int trial = 0; trial < numTrials; ++trial) {
        std::string colName = "mut_trial_" + std::to_string(trial);

        // Track expected final state
        std::unordered_map<DocumentId, Document> expectedDocs;
        std::vector<IndexSpec> expectedIndexes;
        std::vector<DocumentId> docIdPool;
        int indexCounter = 0;

        {
            auto col = makeCollection(colName);

            int numOps = opCountDist(rng);
            for (int op = 0; op < numOps; ++op) {
                int opType = opDist(rng);

                if (opType == 0) {
                    // Insert
                    Document doc = randomDocument(rng);
                    auto result = col.insertOne(doc);
                    doc["_id"] = result.insertedId;
                    expectedDocs[result.insertedId] = doc;
                    docIdPool.push_back(result.insertedId);
                } else if (opType == 1 && !docIdPool.empty()) {
                    // Delete a random existing doc
                    std::uniform_int_distribution<size_t> idx(0, docIdPool.size() - 1);
                    size_t pick = idx(rng);
                    DocumentId id = docIdPool[pick];
                    col.deleteOne({{"_id", id}});
                    expectedDocs.erase(id);
                    docIdPool.erase(docIdPool.begin() + static_cast<long>(pick));
                } else if (opType == 2) {
                    // Add index
                    IndexSpec idxSpec = randomIndexSpec(rng, indexCounter++);
                    col.createIndex(idxSpec);
                    expectedIndexes.push_back(idxSpec);
                } else if (opType == 3 && !expectedIndexes.empty()) {
                    // Drop a random index
                    std::uniform_int_distribution<size_t> idx(0, expectedIndexes.size() - 1);
                    size_t pick = idx(rng);
                    col.dropIndex(expectedIndexes[pick].name);
                    expectedIndexes.erase(expectedIndexes.begin() + static_cast<long>(pick));
                }
            }
        }

        // Round-trip: fresh instance
        auto col2 = makeCollection(colName);

        // Verify documents
        auto allDocs = col2.find({});
        ASSERT_EQ(allDocs.size(), expectedDocs.size())
            << "Trial " << trial << ": doc count mismatch after mutations";

        for (const auto& [id, expectedDoc] : expectedDocs) {
            auto retrieved = col2.findOne({{"_id", id}});
            ASSERT_TRUE(retrieved.has_value())
                << "Trial " << trial << ": missing doc _id=" << id;
            EXPECT_EQ(*retrieved, expectedDoc)
                << "Trial " << trial << ": doc mismatch for _id=" << id;
        }

        // Verify indexes
        auto loadedIndexes = col2.listIndexes();
        ASSERT_EQ(loadedIndexes.size(), expectedIndexes.size())
            << "Trial " << trial << ": index count mismatch after mutations";

        for (size_t i = 0; i < expectedIndexes.size(); ++i) {
            EXPECT_EQ(loadedIndexes[i].name, expectedIndexes[i].name)
                << "Trial " << trial << ": index name mismatch at " << i;
            EXPECT_EQ(loadedIndexes[i].spec, expectedIndexes[i].spec)
                << "Trial " << trial << ": index spec mismatch at " << i;
            EXPECT_EQ(loadedIndexes[i].unique, expectedIndexes[i].unique)
                << "Trial " << trial << ": index unique mismatch at " << i;
            EXPECT_EQ(loadedIndexes[i].sparse, expectedIndexes[i].sparse)
                << "Trial " << trial << ": index sparse mismatch at " << i;
        }
    }
}
