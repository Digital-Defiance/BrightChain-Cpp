#include <gtest/gtest.h>
#include "brightchain/collection.hpp"
#include "brightchain/db_errors.hpp"
#include "brightchain/disk_block_store.hpp"
#include "brightchain/head_registry.hpp"
#include "brightchain/store_lock.hpp"
#include <filesystem>

using namespace brightchain::db;
using namespace brightchain;

class CollectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto tmpl = (std::filesystem::temp_directory_path() / "brightchain_col_test_XXXXXX").string();
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

    Collection makeCollection(const std::string& name = "test_col") {
        return Collection(name, *store_, "testdb", *registry_, *lock_, BlockSize::Small);
    }

    std::filesystem::path testDir_;
    std::unique_ptr<DiskBlockStore> store_;
    std::unique_ptr<HeadRegistry> registry_;
    std::unique_ptr<StoreLock> lock_;
};

// ---------------------------------------------------------------------------
// 1. InsertOneWithId
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, InsertOneWithId) {
    auto col = makeCollection();
    Document doc = {{"_id", "abc123"}, {"name", "Alice"}, {"age", 30}};
    auto result = col.insertOne(doc);

    EXPECT_EQ(result.insertedId, "abc123");

    auto found = col.findOne({{"_id", "abc123"}});
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ((*found)["name"], "Alice");
    EXPECT_EQ((*found)["age"], 30);
}

// ---------------------------------------------------------------------------
// 2. InsertOneWithoutId
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, InsertOneWithoutId) {
    auto col = makeCollection();
    Document doc = {{"name", "Bob"}, {"age", 25}};
    auto result = col.insertOne(doc);

    // Generated _id should be 32 hex chars
    EXPECT_EQ(result.insertedId.size(), 32u);
    for (char c : result.insertedId) {
        EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c)));
    }

    auto found = col.findOne({{"_id", result.insertedId}});
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ((*found)["name"], "Bob");
}

// ---------------------------------------------------------------------------
// 3. InsertMany
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, InsertMany) {
    auto col = makeCollection();
    std::vector<Document> docs = {
        {{"_id", "d1"}, {"val", 1}},
        {{"_id", "d2"}, {"val", 2}},
        {{"_id", "d3"}, {"val", 3}}
    };
    auto result = col.insertMany(docs);

    ASSERT_EQ(result.insertedIds.size(), 3u);
    EXPECT_EQ(result.insertedIds[0], "d1");
    EXPECT_EQ(result.insertedIds[1], "d2");
    EXPECT_EQ(result.insertedIds[2], "d3");

    // All should be retrievable
    for (const auto& id : result.insertedIds) {
        auto found = col.findOne({{"_id", id}});
        ASSERT_TRUE(found.has_value()) << "Missing doc with _id=" << id;
    }
}

// ---------------------------------------------------------------------------
// 4. DuplicateIdRejection
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, DuplicateIdRejection) {
    auto col = makeCollection();
    Document doc1 = {{"_id", "dup"}, {"val", 1}};
    col.insertOne(doc1);

    Document doc2 = {{"_id", "dup"}, {"val", 2}};
    EXPECT_THROW(col.insertOne(doc2), DuplicateKeyError);
}

// ---------------------------------------------------------------------------
// 5. FindOneWithFilter
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, FindOneWithFilter) {
    auto col = makeCollection();
    col.insertOne({{"_id", "a"}, {"color", "red"}});
    col.insertOne({{"_id", "b"}, {"color", "blue"}});
    col.insertOne({{"_id", "c"}, {"color", "red"}});

    auto found = col.findOne({{"color", "blue"}});
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ((*found)["_id"], "b");
}

// ---------------------------------------------------------------------------
// 6. FindOneNoMatch
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, FindOneNoMatch) {
    auto col = makeCollection();
    col.insertOne({{"_id", "a"}, {"color", "red"}});

    auto found = col.findOne({{"color", "green"}});
    EXPECT_FALSE(found.has_value());
}

// ---------------------------------------------------------------------------
// 7. FindWithFilter
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, FindWithFilter) {
    auto col = makeCollection();
    col.insertOne({{"_id", "a"}, {"status", "active"}});
    col.insertOne({{"_id", "b"}, {"status", "inactive"}});
    col.insertOne({{"_id", "c"}, {"status", "active"}});

    auto results = col.find({{"status", "active"}});
    EXPECT_EQ(results.size(), 2u);

    // Both should have status=active
    for (const auto& doc : results) {
        EXPECT_EQ(doc["status"], "active");
    }
}

// ---------------------------------------------------------------------------
// 8. FindWithSkip
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, FindWithSkip) {
    auto col = makeCollection();
    col.insertMany({
        {{"_id", "a"}, {"val", 1}},
        {{"_id", "b"}, {"val", 2}},
        {{"_id", "c"}, {"val", 3}},
        {{"_id", "d"}, {"val", 4}}
    });

    FindOptions opts;
    opts.skip = 2;
    auto results = col.find({}, opts);
    EXPECT_EQ(results.size(), 2u);
}

// ---------------------------------------------------------------------------
// 9. FindWithLimit
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, FindWithLimit) {
    auto col = makeCollection();
    col.insertMany({
        {{"_id", "a"}, {"val", 1}},
        {{"_id", "b"}, {"val", 2}},
        {{"_id", "c"}, {"val", 3}},
        {{"_id", "d"}, {"val", 4}}
    });

    FindOptions opts;
    opts.limit = 2;
    auto results = col.find({}, opts);
    EXPECT_EQ(results.size(), 2u);
}

// ---------------------------------------------------------------------------
// 10. FindWithSkipAndLimit
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, FindWithSkipAndLimit) {
    auto col = makeCollection();
    col.insertMany({
        {{"_id", "a"}, {"val", 1}},
        {{"_id", "b"}, {"val", 2}},
        {{"_id", "c"}, {"val", 3}},
        {{"_id", "d"}, {"val", 4}},
        {{"_id", "e"}, {"val", 5}}
    });

    FindOptions opts;
    opts.skip = 1;
    opts.limit = 2;
    auto results = col.find({}, opts);
    EXPECT_EQ(results.size(), 2u);
}

// ---------------------------------------------------------------------------
// 11. UpdateOne
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, UpdateOne) {
    auto col = makeCollection();
    col.insertOne({{"_id", "u1"}, {"name", "Alice"}, {"score", 10}});

    auto result = col.updateOne(
        {{"_id", "u1"}},
        {{"$set", {{"score", 99}}}}
    );
    EXPECT_EQ(result.matchedCount, 1u);
    EXPECT_EQ(result.modifiedCount, 1u);

    auto found = col.findOne({{"_id", "u1"}});
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ((*found)["score"], 99);
    EXPECT_EQ((*found)["name"], "Alice"); // unchanged field preserved
}

// ---------------------------------------------------------------------------
// 12. UpdateOneNoMatch
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, UpdateOneNoMatch) {
    auto col = makeCollection();
    col.insertOne({{"_id", "u1"}, {"name", "Alice"}});

    auto result = col.updateOne(
        {{"_id", "nonexistent"}},
        {{"$set", {{"name", "Bob"}}}}
    );
    EXPECT_EQ(result.matchedCount, 0u);
    EXPECT_EQ(result.modifiedCount, 0u);
}

// ---------------------------------------------------------------------------
// 13. UpdateMany
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, UpdateMany) {
    auto col = makeCollection();
    col.insertMany({
        {{"_id", "m1"}, {"status", "pending"}, {"val", 1}},
        {{"_id", "m2"}, {"status", "pending"}, {"val", 2}},
        {{"_id", "m3"}, {"status", "done"}, {"val", 3}}
    });

    auto result = col.updateMany(
        {{"status", "pending"}},
        {{"$set", {{"status", "complete"}}}}
    );
    EXPECT_EQ(result.matchedCount, 2u);
    EXPECT_EQ(result.modifiedCount, 2u);

    // Verify both updated
    auto results = col.find({{"status", "complete"}});
    EXPECT_EQ(results.size(), 2u);

    // The "done" doc should be unchanged
    auto done = col.findOne({{"_id", "m3"}});
    ASSERT_TRUE(done.has_value());
    EXPECT_EQ((*done)["status"], "done");
}

// ---------------------------------------------------------------------------
// 14. DeleteOne — block still exists in store after mapping removal
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, DeleteOne) {
    auto col = makeCollection();
    Document doc = {{"_id", "del1"}, {"data", "important"}};
    auto bytes = serializeDocument(doc);
    auto blockChecksum = computeBlockId(bytes);

    col.insertOne(doc);

    // Verify block exists before delete
    ASSERT_TRUE(store_->has(blockChecksum));

    auto result = col.deleteOne({{"_id", "del1"}});
    EXPECT_EQ(result.deletedCount, 1u);

    // Document should no longer be findable
    auto found = col.findOne({{"_id", "del1"}});
    EXPECT_FALSE(found.has_value());

    // But the block should still exist in the store (copy-on-write)
    EXPECT_TRUE(store_->has(blockChecksum));
}

// ---------------------------------------------------------------------------
// 15. DeleteOneNoMatch
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, DeleteOneNoMatch) {
    auto col = makeCollection();
    col.insertOne({{"_id", "keep"}, {"val", 1}});

    auto result = col.deleteOne({{"_id", "nonexistent"}});
    EXPECT_EQ(result.deletedCount, 0u);

    // Original doc still there
    auto found = col.findOne({{"_id", "keep"}});
    ASSERT_TRUE(found.has_value());
}

// ---------------------------------------------------------------------------
// 16. DeleteMany
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, DeleteMany) {
    auto col = makeCollection();
    col.insertMany({
        {{"_id", "x1"}, {"group", "A"}},
        {{"_id", "x2"}, {"group", "B"}},
        {{"_id", "x3"}, {"group", "A"}},
        {{"_id", "x4"}, {"group", "A"}}
    });

    auto result = col.deleteMany({{"group", "A"}});
    EXPECT_EQ(result.deletedCount, 3u);

    // Only group B should remain
    auto remaining = col.find({});
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0]["_id"], "x2");
}

// ---------------------------------------------------------------------------
// 17. DropClearsEverything
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, DropClearsEverything) {
    auto col = makeCollection();
    col.insertMany({
        {{"_id", "d1"}, {"val", 1}},
        {{"_id", "d2"}, {"val", 2}}
    });

    col.drop();

    // find should return empty
    auto results = col.find({});
    EXPECT_TRUE(results.empty());

    // A new collection with the same name should also be empty
    auto col2 = makeCollection();
    auto results2 = col2.find({});
    EXPECT_TRUE(results2.empty());
}

// ---------------------------------------------------------------------------
// 18. LoadFromStoreRestoresDocuments
// ---------------------------------------------------------------------------
TEST_F(CollectionTest, LoadFromStoreRestoresDocuments) {
    // Insert docs in one Collection instance
    {
        auto col = makeCollection("persist_col");
        col.insertMany({
            {{"_id", "p1"}, {"name", "Alice"}},
            {{"_id", "p2"}, {"name", "Bob"}}
        });
    }

    // Create a brand new Collection instance with the same params
    auto col2 = Collection("persist_col", *store_, "testdb", *registry_, *lock_, BlockSize::Small);

    // Documents should be retrievable from the new instance
    auto found1 = col2.findOne({{"_id", "p1"}});
    ASSERT_TRUE(found1.has_value());
    EXPECT_EQ((*found1)["name"], "Alice");

    auto found2 = col2.findOne({{"_id", "p2"}});
    ASSERT_TRUE(found2.has_value());
    EXPECT_EQ((*found2)["name"], "Bob");

    // find all should return both
    auto all = col2.find({});
    EXPECT_EQ(all.size(), 2u);
}
