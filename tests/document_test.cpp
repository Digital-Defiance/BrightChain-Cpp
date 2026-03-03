#include <gtest/gtest.h>
#include "brightchain/document.hpp"
#include <regex>
#include <set>

using namespace brightchain::db;

// --- generateDocumentId ---

TEST(DocumentTest, GenerateDocumentIdLength) {
    auto id = generateDocumentId();
    EXPECT_EQ(id.length(), 32u);
}

TEST(DocumentTest, GenerateDocumentIdHexChars) {
    auto id = generateDocumentId();
    std::regex hexPattern("^[0-9a-f]{32}$");
    EXPECT_TRUE(std::regex_match(id, hexPattern))
        << "ID should be 32 lowercase hex chars, got: " << id;
}

TEST(DocumentTest, GenerateDocumentIdUuidV4Bits) {
    auto id = generateDocumentId();
    // Byte 6 (chars 12-13): high nibble should be 4 (version)
    EXPECT_EQ(id[12], '4') << "UUID v4 version nibble should be '4'";
    // Byte 8 (chars 16-17): high nibble should be 8, 9, a, or b (variant)
    char variant = id[16];
    EXPECT_TRUE(variant == '8' || variant == '9' || variant == 'a' || variant == 'b')
        << "UUID variant nibble should be 8/9/a/b, got: " << variant;
}

TEST(DocumentTest, GenerateDocumentIdUniqueness) {
    std::set<std::string> ids;
    for (int i = 0; i < 100; ++i) {
        ids.insert(generateDocumentId());
    }
    EXPECT_EQ(ids.size(), 100u) << "100 generated IDs should all be unique";
}


// --- ensureDocumentId ---

TEST(DocumentTest, EnsureDocumentIdAddsWhenMissing) {
    Document doc = {{"name", "test"}};
    EXPECT_FALSE(doc.contains("_id"));

    auto id = ensureDocumentId(doc);

    EXPECT_TRUE(doc.contains("_id"));
    EXPECT_EQ(doc["_id"].get<std::string>(), id);
    EXPECT_EQ(id.length(), 32u);
}

TEST(DocumentTest, EnsureDocumentIdPreservesExisting) {
    Document doc = {{"_id", "abc123"}, {"name", "test"}};
    auto id = ensureDocumentId(doc);

    EXPECT_EQ(id, "abc123");
    EXPECT_EQ(doc["_id"].get<std::string>(), "abc123");
}

// --- serializeDocument / deserializeDocument ---

TEST(DocumentTest, SerializeDeserializeRoundTrip) {
    Document original = {
        {"_id", "deadbeef01234567890abcdef1234567"},
        {"name", "Alice"},
        {"age", 30},
        {"active", true},
        {"tags", {"a", "b", "c"}}
    };

    auto bytes = serializeDocument(original);
    auto restored = deserializeDocument(bytes);

    EXPECT_EQ(original, restored);
}

TEST(DocumentTest, SerializeProducesUtf8) {
    Document doc = {{"greeting", u8"hello"}};
    auto bytes = serializeDocument(doc);

    // Should be valid UTF-8 JSON string
    std::string s(bytes.begin(), bytes.end());
    EXPECT_NO_THROW(nlohmann::json::parse(s));
}

TEST(DocumentTest, SerializeCompactNoWhitespace) {
    Document doc = {{"a", 1}, {"b", 2}};
    auto bytes = serializeDocument(doc);
    std::string s(bytes.begin(), bytes.end());

    // dump(-1) produces compact JSON — no newlines or indentation
    EXPECT_EQ(s.find('\n'), std::string::npos);
}

// --- computeBlockId ---

TEST(DocumentTest, ComputeBlockIdDeterministic) {
    Document doc = {{"_id", "test123"}, {"value", 42}};
    auto bytes = serializeDocument(doc);

    auto id1 = computeBlockId(bytes);
    auto id2 = computeBlockId(bytes);

    EXPECT_EQ(id1, id2);
    EXPECT_EQ(id1.toHex(), id2.toHex());
}

TEST(DocumentTest, ComputeBlockIdDifferentForDifferentDocs) {
    Document doc1 = {{"_id", "a"}, {"v", 1}};
    Document doc2 = {{"_id", "b"}, {"v", 2}};

    auto id1 = computeBlockId(serializeDocument(doc1));
    auto id2 = computeBlockId(serializeDocument(doc2));

    EXPECT_NE(id1, id2);
}

TEST(DocumentTest, ComputeBlockIdProducesSha3_512) {
    auto bytes = serializeDocument({{"test", true}});
    auto checksum = computeBlockId(bytes);

    // SHA3-512 produces 64 bytes = 128 hex chars
    EXPECT_EQ(checksum.toHex().length(), 128u);
}
