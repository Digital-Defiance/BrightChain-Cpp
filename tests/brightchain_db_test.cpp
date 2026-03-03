#include <gtest/gtest.h>
#include "brightchain/brightchain_db.hpp"
#include "brightchain/db_errors.hpp"
#include <filesystem>

using namespace brightchain::db;
using namespace brightchain;

class BrightChainDbTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto tmpl = (std::filesystem::temp_directory_path() / "brightchain_db_test_XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        ASSERT_NE(mkdtemp(buf.data()), nullptr);
        testDir_ = std::string(buf.data());

        store_ = std::make_unique<DiskBlockStore>(testDir_, BlockSize::Small);
    }

    void TearDown() override {
        std::filesystem::remove_all(testDir_);
    }

    std::filesystem::path testDir_;
    std::unique_ptr<DiskBlockStore> store_;
};

// ---------------------------------------------------------------------------
// 1. Connect loads HeadRegistry
// ---------------------------------------------------------------------------
TEST_F(BrightChainDbTest, ConnectLoadsHeadRegistry) {
    DbOptions opts;
    opts.name = "mydb";
    opts.dataDir = testDir_;
    opts.blockSize = BlockSize::Small;

    // Insert a document via one db instance, then verify a fresh instance
    // can see it after connect().
    {
        BrightChainDb db(*store_, opts);
        db.connect();
        auto& col = db.collection("users");
        col.insertOne({{"_id", "u1"}, {"name", "Alice"}});
    }

    // New instance — connect should reload the HeadRegistry from disk
    BrightChainDb db2(*store_, opts);
    db2.connect();
    auto& col2 = db2.collection("users");
    auto found = col2.findOne({{"_id", "u1"}});
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ((*found)["name"], "Alice");
}

// ---------------------------------------------------------------------------
// 2. Collection creates and returns collections
// ---------------------------------------------------------------------------
TEST_F(BrightChainDbTest, CollectionCreatesAndReturns) {
    DbOptions opts;
    opts.dataDir = testDir_;
    opts.blockSize = BlockSize::Small;

    BrightChainDb db(*store_, opts);
    db.connect();

    auto& col1 = db.collection("orders");
    auto& col2 = db.collection("orders");

    // Same reference returned for same name
    EXPECT_EQ(&col1, &col2);
    EXPECT_EQ(col1.name(), "orders");
}

// ---------------------------------------------------------------------------
// 3. ListCollections returns correct names
// ---------------------------------------------------------------------------
TEST_F(BrightChainDbTest, ListCollections) {
    DbOptions opts;
    opts.dataDir = testDir_;
    opts.blockSize = BlockSize::Small;

    BrightChainDb db(*store_, opts);
    db.connect();

    EXPECT_TRUE(db.listCollections().empty());

    db.collection("alpha");
    db.collection("beta");
    db.collection("gamma");

    auto names = db.listCollections();
    EXPECT_EQ(names.size(), 3u);

    // Check all three are present (order not guaranteed with unordered_map)
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names[0], "alpha");
    EXPECT_EQ(names[1], "beta");
    EXPECT_EQ(names[2], "gamma");
}

// ---------------------------------------------------------------------------
// 4. DropCollection removes collection
// ---------------------------------------------------------------------------
TEST_F(BrightChainDbTest, DropCollectionRemoves) {
    DbOptions opts;
    opts.dataDir = testDir_;
    opts.blockSize = BlockSize::Small;

    BrightChainDb db(*store_, opts);
    db.connect();

    auto& col = db.collection("temp");
    col.insertOne({{"_id", "t1"}, {"val", 42}});

    db.dropCollection("temp");

    auto names = db.listCollections();
    EXPECT_TRUE(names.empty());

    // Re-creating the collection should start empty
    auto& col2 = db.collection("temp");
    auto results = col2.find({});
    EXPECT_TRUE(results.empty());
}

// ---------------------------------------------------------------------------
// 5. DropCollection with nonexistent name is a no-op
// ---------------------------------------------------------------------------
TEST_F(BrightChainDbTest, DropCollectionNonexistent) {
    DbOptions opts;
    opts.dataDir = testDir_;
    opts.blockSize = BlockSize::Small;

    BrightChainDb db(*store_, opts);
    db.connect();

    // Should not throw
    db.dropCollection("does_not_exist");
    EXPECT_TRUE(db.listCollections().empty());
}

// ---------------------------------------------------------------------------
// 6. DropDatabase clears everything
// ---------------------------------------------------------------------------
TEST_F(BrightChainDbTest, DropDatabaseClearsEverything) {
    DbOptions opts;
    opts.dataDir = testDir_;
    opts.blockSize = BlockSize::Small;

    BrightChainDb db(*store_, opts);
    db.connect();

    auto& col1 = db.collection("users");
    col1.insertOne({{"_id", "u1"}, {"name", "Alice"}});

    auto& col2 = db.collection("orders");
    col2.insertOne({{"_id", "o1"}, {"item", "widget"}});

    db.dropDatabase();

    EXPECT_TRUE(db.listCollections().empty());

    // Re-creating collections should start empty
    auto& users = db.collection("users");
    EXPECT_TRUE(users.find({}).empty());

    auto& orders = db.collection("orders");
    EXPECT_TRUE(orders.find({}).empty());
}

// ---------------------------------------------------------------------------
// 7. Database name defaults to "brightchain"
// ---------------------------------------------------------------------------
TEST_F(BrightChainDbTest, DefaultName) {
    DbOptions opts;
    opts.dataDir = testDir_;

    BrightChainDb db(*store_, opts);
    EXPECT_EQ(db.name(), "brightchain");
}

// ---------------------------------------------------------------------------
// 8. Database name is configurable
// ---------------------------------------------------------------------------
TEST_F(BrightChainDbTest, CustomName) {
    DbOptions opts;
    opts.name = "custom_db";
    opts.dataDir = testDir_;

    BrightChainDb db(*store_, opts);
    EXPECT_EQ(db.name(), "custom_db");
}
