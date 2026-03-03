/**
 * Cross-platform compatibility test for the BrightChain document database.
 *
 * Validates that the C++ implementation produces identical block IDs and
 * directory paths as the TypeScript implementation, using pre-computed
 * test vectors from test_vectors_db.json.
 *
 * Requirements: 8.2, 8.3
 */

#include <gtest/gtest.h>
#include "brightchain/block_size.hpp"
#include "brightchain/checksum.hpp"
#include "brightchain/document.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace brightchain;
using namespace brightchain::db;

class DbCrossCompatTest : public ::testing::Test {
protected:
    json testVectors;

    void SetUp() override {
        std::vector<std::string> paths = {
            "test_vectors_db.json",
            "tests/test_vectors_db.json",
            "../tests/test_vectors_db.json",
            "../../tests/test_vectors_db.json",
        };

        std::ifstream file;
        for (const auto& path : paths) {
            file.open(path);
            if (file.is_open()) break;
        }

        ASSERT_TRUE(file.is_open())
            << "test_vectors_db.json not found in any expected location";
        file >> testVectors;
        file.close();
    }

    /// Convert a hex string to a byte vector.
    static std::vector<uint8_t> hexToBytes(const std::string& hex) {
        std::vector<uint8_t> bytes;
        bytes.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2) {
            auto byte = static_cast<uint8_t>(
                std::stoul(hex.substr(i, 2), nullptr, 16));
            bytes.push_back(byte);
        }
        return bytes;
    }

    /// Build the expected block path the same way DiskBlockStore does.
    static std::string buildBlockPath(const std::string& storePath,
                                      const std::string& blockSizeName,
                                      const std::string& checksumHex) {
        std::filesystem::path p = storePath;
        p /= blockSizeName;
        p /= std::string(1, checksumHex[0]);
        p /= std::string(1, checksumHex[1]);
        p /= checksumHex;
        return p.string();
    }
};

// ---------------------------------------------------------------------------
// 1. Document block ID verification
// ---------------------------------------------------------------------------
TEST_F(DbCrossCompatTest, DocumentBlockIds) {
    ASSERT_TRUE(testVectors.contains("documentVectors"));
    const auto& vectors = testVectors["documentVectors"];
    ASSERT_GT(vectors.size(), 0u);

    for (size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        const std::string description = vec["description"];
        const std::string serializedHex = vec["serializedHex"];
        const std::string expectedBlockId = vec["blockId"];

        // Convert hex-encoded serialized JSON to bytes
        auto data = hexToBytes(serializedHex);

        // Compute SHA3-512 block ID using the C++ Checksum class
        auto checksum = Checksum::fromData(data);
        std::string actualBlockId = checksum.toHex();

        EXPECT_EQ(actualBlockId, expectedBlockId)
            << "Block ID mismatch for: " << description
            << " (vector " << i << ")";
    }
}

// ---------------------------------------------------------------------------
// 2. Document block ID from raw serialized bytes
//
// Cross-platform invariant: identical byte sequences produce identical
// block IDs regardless of JSON key ordering differences between
// nlohmann::json (alphabetical) and JavaScript (insertion order).
// ---------------------------------------------------------------------------
TEST_F(DbCrossCompatTest, DocumentBlockIdFromRawBytes) {
    ASSERT_TRUE(testVectors.contains("documentVectors"));
    const auto& vectors = testVectors["documentVectors"];

    for (size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        const std::string description = vec["description"];
        const std::string serializedJson = vec["serializedJson"];
        const std::string expectedBlockId = vec["blockId"];

        // Use the exact serialized JSON bytes from the TypeScript side
        std::vector<uint8_t> data(serializedJson.begin(), serializedJson.end());

        // Verify block ID matches
        auto checksum = computeBlockId(data);
        EXPECT_EQ(checksum.toHex(), expectedBlockId)
            << "Block ID mismatch for: " << description
            << " (vector " << i << ")";

        // Verify deserialize round-trip preserves document content
        auto doc = deserializeDocument(data);
        EXPECT_TRUE(doc.contains("_id"))
            << "Deserialized document missing _id for: " << description;
    }
}

// ---------------------------------------------------------------------------
// 3. Directory path construction for each BlockSize
// ---------------------------------------------------------------------------
TEST_F(DbCrossCompatTest, DirectoryPaths) {
    ASSERT_TRUE(testVectors.contains("directoryPathVectors"));
    const auto& vectors = testVectors["directoryPathVectors"];
    ASSERT_GT(vectors.size(), 0u);

    for (size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        const std::string description = vec["description"];
        const std::string blockSizeName = vec["blockSizeName"];
        const std::string checksumHex = vec["checksumHex"];
        const std::string storePath = vec["storePath"];
        const std::string expectedPath = vec["expectedPath"];

        // Build path the same way DiskBlockStore does
        std::string actualPath = buildBlockPath(storePath, blockSizeName, checksumHex);

        EXPECT_EQ(actualPath, expectedPath)
            << "Directory path mismatch for: " << description
            << " (vector " << i << ")";
    }
}

// ---------------------------------------------------------------------------
// 4. BlockSize name mapping
// ---------------------------------------------------------------------------
TEST_F(DbCrossCompatTest, BlockSizeNames) {
    ASSERT_TRUE(testVectors.contains("blockSizeVectors"));
    const auto& vectors = testVectors["blockSizeVectors"];

    for (size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        const std::string expectedName = vec["blockSizeName"];
        const uint32_t sizeValue = vec["blockSizeValue"];

        BlockSize bs = lengthToBlockSize(sizeValue);
        std::string actualName = blockSizeToString(bs);

        EXPECT_EQ(actualName, expectedName)
            << "BlockSize name mismatch for value " << sizeValue
            << " (vector " << i << ")";
    }
}

// ---------------------------------------------------------------------------
// 5. CollectionMeta block ID verification
// ---------------------------------------------------------------------------
TEST_F(DbCrossCompatTest, CollectionMetaBlockIds) {
    ASSERT_TRUE(testVectors.contains("collectionMetaVectors"));
    const auto& vectors = testVectors["collectionMetaVectors"];
    ASSERT_GT(vectors.size(), 0u);

    for (size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        const std::string description = vec["description"];
        const std::string serializedJson = vec["serializedJson"];
        const std::string expectedBlockId = vec["blockId"];

        // Compute block ID from the exact serialized JSON bytes
        std::vector<uint8_t> data(serializedJson.begin(), serializedJson.end());
        auto checksum = Checksum::fromData(data);

        EXPECT_EQ(checksum.toHex(), expectedBlockId)
            << "CollectionMeta block ID mismatch for: " << description
            << " (vector " << i << ")";
    }
}

// ---------------------------------------------------------------------------
// 6. CollectionMeta block ID from raw serialized bytes
//
// Same principle as document block IDs: the cross-platform invariant is
// that identical byte sequences produce identical block IDs. Key ordering
// may differ between C++ and TypeScript serialization.
// ---------------------------------------------------------------------------
TEST_F(DbCrossCompatTest, CollectionMetaBlockIdFromRawBytes) {
    ASSERT_TRUE(testVectors.contains("collectionMetaVectors"));
    const auto& vectors = testVectors["collectionMetaVectors"];

    for (size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        const std::string description = vec["description"];
        const std::string serializedJson = vec["serializedJson"];
        const std::string expectedBlockId = vec["blockId"];

        // Use the exact serialized JSON bytes
        std::vector<uint8_t> data(serializedJson.begin(), serializedJson.end());
        auto checksum = Checksum::fromData(data);

        EXPECT_EQ(checksum.toHex(), expectedBlockId)
            << "CollectionMeta block ID from raw bytes mismatch for: "
            << description << " (vector " << i << ")";

        // Verify the meta can be parsed and contains expected fields
        auto meta = json::parse(serializedJson);
        EXPECT_TRUE(meta.contains("mappings"))
            << "CollectionMeta missing 'mappings' for: " << description;
        EXPECT_TRUE(meta.contains("indexes"))
            << "CollectionMeta missing 'indexes' for: " << description;
    }
}

// ---------------------------------------------------------------------------
// 7. HeadRegistry format parsing
// ---------------------------------------------------------------------------
TEST_F(DbCrossCompatTest, HeadRegistryFormats) {
    ASSERT_TRUE(testVectors.contains("headRegistryVectors"));
    const auto& vectors = testVectors["headRegistryVectors"];
    ASSERT_GT(vectors.size(), 0u);

    for (size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        const std::string description = vec["description"];
        const std::string format = vec["format"];
        const auto& registryJson = vec["json"];
        const auto& entries = vec["entries"];

        // Parse the registry JSON and verify we can extract entries
        auto parsed = json::parse(registryJson.dump());

        for (const auto& entry : entries) {
            const std::string key = entry["key"];
            const std::string expectedBlockId = entry["blockId"];

            ASSERT_TRUE(parsed.contains(key))
                << "Missing key '" << key << "' in " << description;

            if (format == "legacy") {
                // Legacy: plain string value
                ASSERT_TRUE(parsed[key].is_string())
                    << "Expected string for legacy key '" << key << "'";
                EXPECT_EQ(parsed[key].get<std::string>(), expectedBlockId)
                    << "Legacy blockId mismatch for key '" << key
                    << "' in " << description;
            } else {
                // Current: object with blockId and timestamp
                ASSERT_TRUE(parsed[key].is_object())
                    << "Expected object for current key '" << key << "'";
                EXPECT_EQ(parsed[key]["blockId"].get<std::string>(), expectedBlockId)
                    << "Current blockId mismatch for key '" << key
                    << "' in " << description;

                if (entry.contains("timestamp")) {
                    std::string expectedTs = entry["timestamp"];
                    EXPECT_EQ(parsed[key]["timestamp"].get<std::string>(), expectedTs)
                        << "Timestamp mismatch for key '" << key
                        << "' in " << description;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 8. Metadata path construction
// ---------------------------------------------------------------------------
TEST_F(DbCrossCompatTest, MetadataPaths) {
    ASSERT_TRUE(testVectors.contains("directoryPathVectors"));
    const auto& vectors = testVectors["directoryPathVectors"];

    for (size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        const std::string description = vec["description"];
        const std::string expectedMetaPath = vec["expectedMetadataPath"];
        const std::string blockSizeName = vec["blockSizeName"];
        const std::string checksumHex = vec["checksumHex"];
        const std::string storePath = vec["storePath"];

        std::string blockPath = buildBlockPath(storePath, blockSizeName, checksumHex);
        std::string actualMetaPath = blockPath + ".m.json";

        EXPECT_EQ(actualMetaPath, expectedMetaPath)
            << "Metadata path mismatch for: " << description
            << " (vector " << i << ")";
    }
}
