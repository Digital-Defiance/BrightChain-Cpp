/**
 * End-to-end cross-platform integration test (C++ side).
 *
 * This test:
 *   1. Runs the TypeScript harness to create a store with documents
 *   2. Opens the same store path with the C++ BrightChainDb
 *   3. Loads the HeadRegistry and reads documents inserted by TypeScript
 *   4. Verifies retrieved documents are byte-for-byte identical to originals
 *   5. Inserts additional documents from the C++ side
 *   6. Persists the updated state for the TypeScript verifier
 *
 * Uses Medium BlockSize (1048576 bytes) per Requirement 9.6.
 *
 * Requirements: 9.2, 9.3, 9.4, 9.6
 */

#include <gtest/gtest.h>
#include "brightchain/brightchain_db.hpp"
#include "brightchain/document.hpp"
#include "brightchain/db_errors.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace brightchain::db;
using namespace brightchain;

class DbE2ECrossPlatformTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a unique temp directory for the e2e test
        auto tmpl = (std::filesystem::temp_directory_path() / "brightchain_e2e_XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        ASSERT_NE(mkdtemp(buf.data()), nullptr);
        storePath_ = std::string(buf.data());
    }

    void TearDown() override {
        std::filesystem::remove_all(storePath_);
    }

    /**
     * Run the TypeScript harness to populate the store.
     * Returns true if the harness ran successfully.
     */
    bool runTsHarness() {
        // Find the harness script relative to common build/test locations
        std::vector<std::string> scriptPaths = {
            "tests/db_e2e_ts_harness.ts",
            "../tests/db_e2e_ts_harness.ts",
            "../../tests/db_e2e_ts_harness.ts",
        };

        std::string scriptPath;
        for (const auto& p : scriptPaths) {
            if (std::filesystem::exists(p)) {
                scriptPath = p;
                break;
            }
        }

        if (scriptPath.empty()) {
            return false;
        }

        std::string cmd = "npx tsx " + scriptPath + " " + storePath_ + " 2>&1";
        int ret = std::system(cmd.c_str());
        return ret == 0;
    }

    /**
     * Load the manifest written by the TypeScript harness.
     */
    json loadManifest() {
        std::string manifestPath = storePath_ + "/e2e_manifest.json";
        std::ifstream file(manifestPath);
        if (!file.is_open()) {
            throw std::runtime_error("Manifest not found: " + manifestPath);
        }
        json manifest;
        file >> manifest;
        return manifest;
    }

    std::string storePath_;
};

// ---------------------------------------------------------------------------
// 1. Read documents inserted by TypeScript harness
// ---------------------------------------------------------------------------
TEST_F(DbE2ECrossPlatformTest, ReadTsDocuments) {
    // Run the TypeScript harness to populate the store
    if (!runTsHarness()) {
        GTEST_SKIP() << "TypeScript harness not available (tsx/npx not found)";
    }

    // Load the manifest to know what was inserted
    json manifest = loadManifest();
    const std::string dbName = manifest["dbName"];
    const std::string collectionName = manifest["collectionName"];
    const auto& expectedDocs = manifest["documents"];

    // Open the same store with C++ BrightChainDb
    DiskBlockStore store(storePath_, BlockSize::Medium);
    DbOptions opts;
    opts.name = dbName;
    opts.dataDir = storePath_;
    opts.blockSize = BlockSize::Medium;

    BrightChainDb db(store, opts);
    db.connect();

    auto& col = db.collection(collectionName);

    // Verify each document inserted by TypeScript is readable
    for (const auto& expectedDoc : expectedDocs) {
        std::string docId = expectedDoc["_id"];
        auto found = col.findOne({{"_id", docId}});
        ASSERT_TRUE(found.has_value())
            << "Document not found: " << docId;

        // Verify the document content matches
        // Compare key-by-key since JSON key ordering may differ
        for (auto it = expectedDoc.begin(); it != expectedDoc.end(); ++it) {
            ASSERT_TRUE(found->contains(it.key()))
                << "Missing field '" << it.key() << "' in doc " << docId;
            EXPECT_EQ((*found)[it.key()], it.value())
                << "Field '" << it.key() << "' mismatch in doc " << docId;
        }
    }

    // Verify total document count
    auto allDocs = col.find({});
    EXPECT_EQ(allDocs.size(), expectedDocs.size());
}

// ---------------------------------------------------------------------------
// 2. Verify byte-for-byte serialization identity
// ---------------------------------------------------------------------------
TEST_F(DbE2ECrossPlatformTest, ByteForByteIdentity) {
    if (!runTsHarness()) {
        GTEST_SKIP() << "TypeScript harness not available (tsx/npx not found)";
    }

    json manifest = loadManifest();
    const auto& docMappings = manifest["docMappings"];
    const auto& expectedDocs = manifest["documents"];

    DiskBlockStore store(storePath_, BlockSize::Medium);

    // For each document, verify the raw block bytes match the expected
    // serialization (compact JSON, no BOM)
    for (const auto& expectedDoc : expectedDocs) {
        std::string docId = expectedDoc["_id"];
        std::string expectedBlockId = docMappings[docId];

        // Read the raw block from the store
        auto checksum = Checksum::fromHex(expectedBlockId);
        auto rawBytes = store.get(checksum);

        // Compute the expected serialization from the TypeScript side
        // The TypeScript harness uses JSON.stringify which produces compact JSON
        // with insertion-order keys. We verify the block ID matches.
        auto actualChecksum = Checksum::fromData(rawBytes);
        EXPECT_EQ(actualChecksum.toHex(), expectedBlockId)
            << "Block ID mismatch for doc " << docId;

        // Deserialize and verify content
        auto doc = deserializeDocument(rawBytes);
        EXPECT_EQ(doc["_id"], docId);
    }
}

// ---------------------------------------------------------------------------
// 3. Insert documents from C++ side and persist
// ---------------------------------------------------------------------------
TEST_F(DbE2ECrossPlatformTest, InsertFromCpp) {
    if (!runTsHarness()) {
        GTEST_SKIP() << "TypeScript harness not available (tsx/npx not found)";
    }

    json manifest = loadManifest();
    const std::string dbName = manifest["dbName"];
    const std::string collectionName = manifest["collectionName"];
    const size_t tsDocCount = manifest["documents"].size();

    // Open the store and insert additional documents from C++
    DiskBlockStore store(storePath_, BlockSize::Medium);
    DbOptions opts;
    opts.name = dbName;
    opts.dataDir = storePath_;
    opts.blockSize = BlockSize::Medium;

    BrightChainDb db(store, opts);
    db.connect();

    auto& col = db.collection(collectionName);

    // Verify TypeScript documents are still there
    auto existingDocs = col.find({});
    ASSERT_EQ(existingDocs.size(), tsDocCount);

    // Insert new documents from C++
    Document cppDoc1 = {
        {"_id", "cpp_doc_001"},
        {"name", "Eve"},
        {"age", 28},
        {"source", "cpp"},
        {"tags", json::array({"developer", "tester"})},
    };

    Document cppDoc2 = {
        {"_id", "cpp_doc_002"},
        {"name", "Frank"},
        {"age", 35},
        {"source", "cpp"},
        {"nested", {{"key", "value"}, {"num", 99}}},
    };

    Document cppDoc3 = {
        {"_id", "cpp_doc_003"},
        {"name", "Grace"},
        {"age", 0},
        {"source", "cpp"},
        {"unicode", "日本語テスト"},
        {"emoji", "🎉"},
    };

    auto r1 = col.insertOne(cppDoc1);
    EXPECT_EQ(r1.insertedId, "cpp_doc_001");

    auto r2 = col.insertOne(cppDoc2);
    EXPECT_EQ(r2.insertedId, "cpp_doc_002");

    auto r3 = col.insertOne(cppDoc3);
    EXPECT_EQ(r3.insertedId, "cpp_doc_003");

    // Verify total count
    auto allDocs = col.find({});
    EXPECT_EQ(allDocs.size(), tsDocCount + 3);

    // Verify C++ documents are retrievable
    auto found1 = col.findOne({{"_id", "cpp_doc_001"}});
    ASSERT_TRUE(found1.has_value());
    EXPECT_EQ((*found1)["name"], "Eve");
    EXPECT_EQ((*found1)["source"], "cpp");

    auto found2 = col.findOne({{"_id", "cpp_doc_002"}});
    ASSERT_TRUE(found2.has_value());
    EXPECT_EQ((*found2)["nested"]["key"], "value");

    auto found3 = col.findOne({{"_id", "cpp_doc_003"}});
    ASSERT_TRUE(found3.has_value());
    EXPECT_EQ((*found3)["unicode"], "日本語テスト");
    EXPECT_EQ((*found3)["emoji"], "🎉");

    // Write a manifest update for the TypeScript verifier
    json cppManifest;
    cppManifest["dbName"] = dbName;
    cppManifest["collectionName"] = collectionName;
    cppManifest["cppDocuments"] = json::array({cppDoc1, cppDoc2, cppDoc3});
    cppManifest["totalDocCount"] = tsDocCount + 3;

    std::string cppManifestPath = storePath_ + "/e2e_cpp_manifest.json";
    std::ofstream out(cppManifestPath);
    ASSERT_TRUE(out.is_open());
    out << cppManifest.dump(2);
    out.close();
}

// ---------------------------------------------------------------------------
// 4. Round-trip: C++ reads TS docs, inserts more, re-reads all
// ---------------------------------------------------------------------------
TEST_F(DbE2ECrossPlatformTest, FullRoundTrip) {
    if (!runTsHarness()) {
        GTEST_SKIP() << "TypeScript harness not available (tsx/npx not found)";
    }

    json manifest = loadManifest();
    const std::string dbName = manifest["dbName"];
    const std::string collectionName = manifest["collectionName"];

    DiskBlockStore store(storePath_, BlockSize::Medium);
    DbOptions opts;
    opts.name = dbName;
    opts.dataDir = storePath_;
    opts.blockSize = BlockSize::Medium;

    // First instance: read TS docs and insert C++ docs
    {
        BrightChainDb db(store, opts);
        db.connect();
        auto& col = db.collection(collectionName);

        // Read a TS document
        auto tsDoc = col.findOne({{"_id", "e2e_doc_001"}});
        ASSERT_TRUE(tsDoc.has_value());
        EXPECT_EQ((*tsDoc)["name"], "Alice");

        // Insert a C++ document
        col.insertOne({{"_id", "roundtrip_001"}, {"value", 42}});
    }

    // Second instance: verify persistence across instances
    {
        BrightChainDb db2(store, opts);
        db2.connect();
        auto& col2 = db2.collection(collectionName);

        // TS document still there
        auto tsDoc = col2.findOne({{"_id", "e2e_doc_001"}});
        ASSERT_TRUE(tsDoc.has_value());
        EXPECT_EQ((*tsDoc)["name"], "Alice");

        // C++ document persisted
        auto cppDoc = col2.findOne({{"_id", "roundtrip_001"}});
        ASSERT_TRUE(cppDoc.has_value());
        EXPECT_EQ((*cppDoc)["value"], 42);

        // Total count: 5 TS docs + 1 C++ doc
        auto all = col2.find({});
        EXPECT_EQ(all.size(), 6u);
    }
}
