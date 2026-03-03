#include <gtest/gtest.h>
#include "brightchain/head_registry.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace brightchain::db;

class HeadRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto tmpl = (std::filesystem::temp_directory_path() / "brightchain_hr_test_XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        ASSERT_NE(mkdtemp(buf.data()), nullptr) << "mkdtemp failed";
        testDir_ = std::string(buf.data());
    }

    void TearDown() override {
        std::filesystem::remove_all(testDir_);
    }

    void writeRegistryFile(const std::string& content) {
        std::ofstream ofs(testDir_ / "head-registry.json");
        ofs << content;
    }

    std::string readRegistryFile() {
        std::ifstream ifs(testDir_ / "head-registry.json");
        return std::string(std::istreambuf_iterator<char>(ifs),
                           std::istreambuf_iterator<char>());
    }

    bool registryFileExists() {
        return std::filesystem::exists(testDir_ / "head-registry.json");
    }

    std::filesystem::path testDir_;
};

// --- Load from missing file starts empty ---

TEST_F(HeadRegistryTest, LoadMissingFileStartsEmpty) {
    // No file written — directory exists but no head-registry.json
    std::filesystem::remove(testDir_ / "head-registry.json");

    HeadRegistry reg(testDir_);
    reg.load();

    EXPECT_TRUE(reg.keys().empty());
    EXPECT_EQ(reg.getHead("anything"), std::nullopt);
}

// --- Load from invalid JSON starts empty ---

TEST_F(HeadRegistryTest, LoadInvalidJsonStartsEmpty) {
    writeRegistryFile("this is not valid json {{{");

    HeadRegistry reg(testDir_);
    reg.load();

    EXPECT_TRUE(reg.keys().empty());
}

TEST_F(HeadRegistryTest, LoadNonObjectJsonStartsEmpty) {
    writeRegistryFile("[1, 2, 3]");

    HeadRegistry reg(testDir_);
    reg.load();

    EXPECT_TRUE(reg.keys().empty());
}

// --- Load legacy format (plain string values) ---

TEST_F(HeadRegistryTest, LoadLegacyFormat) {
    nlohmann::json j = {
        {"db:users", "abc123def456"},
        {"db:orders", "789xyz000111"}
    };
    writeRegistryFile(j.dump());

    HeadRegistry reg(testDir_);
    reg.load();

    auto users = reg.getHead("db:users");
    ASSERT_TRUE(users.has_value());
    EXPECT_EQ(users->blockId, "abc123def456");
    EXPECT_EQ(users->timestamp, "");

    auto orders = reg.getHead("db:orders");
    ASSERT_TRUE(orders.has_value());
    EXPECT_EQ(orders->blockId, "789xyz000111");
}

// --- Load current format (object with blockId/timestamp) ---

TEST_F(HeadRegistryTest, LoadCurrentFormat) {
    nlohmann::json j = {
        {"db:users", {{"blockId", "aabbccdd"}, {"timestamp", "2025-01-15T10:30:00Z"}}}
    };
    writeRegistryFile(j.dump());

    HeadRegistry reg(testDir_);
    reg.load();

    auto entry = reg.getHead("db:users");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->blockId, "aabbccdd");
    EXPECT_EQ(entry->timestamp, "2025-01-15T10:30:00Z");
}

TEST_F(HeadRegistryTest, LoadMixedFormats) {
    nlohmann::json j = {
        {"db:legacy", "plainblockid"},
        {"db:current", {{"blockId", "newblockid"}, {"timestamp", "2025-06-01T00:00:00Z"}}}
    };
    writeRegistryFile(j.dump());

    HeadRegistry reg(testDir_);
    reg.load();

    auto legacy = reg.getHead("db:legacy");
    ASSERT_TRUE(legacy.has_value());
    EXPECT_EQ(legacy->blockId, "plainblockid");

    auto current = reg.getHead("db:current");
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->blockId, "newblockid");
    EXPECT_EQ(current->timestamp, "2025-06-01T00:00:00Z");
}

// --- setHead persists to disk and is reloadable ---

TEST_F(HeadRegistryTest, SetHeadPersistsToDisk) {
    HeadRegistry reg(testDir_);
    reg.load();

    reg.setHead("db:users", "block123");

    // Verify file was written
    ASSERT_TRUE(registryFileExists());

    // Load in a fresh instance to verify persistence
    HeadRegistry reg2(testDir_);
    reg2.load();

    auto entry = reg2.getHead("db:users");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->blockId, "block123");
    EXPECT_FALSE(entry->timestamp.empty());
}

TEST_F(HeadRegistryTest, SetHeadOverwritesExisting) {
    HeadRegistry reg(testDir_);
    reg.load();

    reg.setHead("db:col", "first_block");
    reg.setHead("db:col", "second_block");

    auto entry = reg.getHead("db:col");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->blockId, "second_block");
}

TEST_F(HeadRegistryTest, SetHeadStoresTimestamp) {
    HeadRegistry reg(testDir_);
    reg.load();

    reg.setHead("db:col", "someblock");

    auto entry = reg.getHead("db:col");
    ASSERT_TRUE(entry.has_value());
    // Timestamp should be ISO 8601 format ending with Z
    EXPECT_FALSE(entry->timestamp.empty());
    EXPECT_EQ(entry->timestamp.back(), 'Z');
    EXPECT_NE(entry->timestamp.find('T'), std::string::npos);
}

// --- removeHead persists removal ---

TEST_F(HeadRegistryTest, RemoveHeadPersistsRemoval) {
    HeadRegistry reg(testDir_);
    reg.load();

    reg.setHead("db:users", "block1");
    reg.setHead("db:orders", "block2");
    reg.removeHead("db:users");

    EXPECT_EQ(reg.getHead("db:users"), std::nullopt);
    EXPECT_TRUE(reg.getHead("db:orders").has_value());

    // Verify persistence
    HeadRegistry reg2(testDir_);
    reg2.load();

    EXPECT_EQ(reg2.getHead("db:users"), std::nullopt);
    EXPECT_TRUE(reg2.getHead("db:orders").has_value());
}

TEST_F(HeadRegistryTest, RemoveNonexistentKeyIsNoOp) {
    HeadRegistry reg(testDir_);
    reg.load();

    reg.setHead("db:col", "block1");
    reg.removeHead("db:nonexistent");

    EXPECT_TRUE(reg.getHead("db:col").has_value());
}

// --- clear removes all entries ---

TEST_F(HeadRegistryTest, ClearRemovesAllEntries) {
    HeadRegistry reg(testDir_);
    reg.load();

    reg.setHead("db:a", "block_a");
    reg.setHead("db:b", "block_b");
    reg.setHead("db:c", "block_c");

    EXPECT_EQ(reg.keys().size(), 3u);

    reg.clear();

    EXPECT_TRUE(reg.keys().empty());
    EXPECT_EQ(reg.getHead("db:a"), std::nullopt);

    // Verify persistence — reload should also be empty
    HeadRegistry reg2(testDir_);
    reg2.load();
    EXPECT_TRUE(reg2.keys().empty());
}

// --- Atomic write (temp file + rename) ---

TEST_F(HeadRegistryTest, AtomicWriteNoTempFileLeftBehind) {
    HeadRegistry reg(testDir_);
    reg.load();

    reg.setHead("db:col", "block1");

    // After persist, no .tmp file should remain
    auto tmpPath = testDir_ / "head-registry.json.tmp";
    EXPECT_FALSE(std::filesystem::exists(tmpPath));

    // But the registry file should exist
    EXPECT_TRUE(registryFileExists());
}

TEST_F(HeadRegistryTest, AtomicWriteProducesValidJson) {
    HeadRegistry reg(testDir_);
    reg.load();

    reg.setHead("db:users", "blockA");
    reg.setHead("db:orders", "blockB");

    std::string content = readRegistryFile();
    nlohmann::json j;
    EXPECT_NO_THROW(j = nlohmann::json::parse(content));
    EXPECT_TRUE(j.is_object());
    EXPECT_TRUE(j.contains("db:users"));
    EXPECT_TRUE(j.contains("db:orders"));
    EXPECT_EQ(j["db:users"]["blockId"].get<std::string>(), "blockA");
    EXPECT_EQ(j["db:orders"]["blockId"].get<std::string>(), "blockB");
}

// --- keys() ---

TEST_F(HeadRegistryTest, KeysReturnsAllKeys) {
    HeadRegistry reg(testDir_);
    reg.load();

    reg.setHead("db:a", "block1");
    reg.setHead("db:b", "block2");

    auto k = reg.keys();
    EXPECT_EQ(k.size(), 2u);

    std::sort(k.begin(), k.end());
    EXPECT_EQ(k[0], "db:a");
    EXPECT_EQ(k[1], "db:b");
}

// --- Lock file cleanup ---

TEST_F(HeadRegistryTest, LockFileCleanedUpAfterWrite) {
    HeadRegistry reg(testDir_);
    reg.load();

    reg.setHead("db:col", "block1");

    auto lockPath = testDir_ / "head-registry.json.lock";
    EXPECT_FALSE(std::filesystem::exists(lockPath))
        << "Lock file should be removed after write completes";
}
