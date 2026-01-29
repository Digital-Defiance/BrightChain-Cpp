#include <brightchain/disk_block_store.hpp>
#include <gtest/gtest.h>
#include <filesystem>

using namespace brightchain;

class BlockMetadataTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir = std::filesystem::temp_directory_path() / "brightchain_metadata_test";
        std::filesystem::remove_all(testDir);
    }

    void TearDown() override {
        std::filesystem::remove_all(testDir);
    }

    std::filesystem::path testDir;
};

TEST_F(BlockMetadataTest, PutWithMetadata) {
    DiskBlockStore store(testDir.string(), BlockSize::Small);
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    
    BlockMetadata metadata(BlockSize::Small, data.size());
    auto checksum = store.put(data, metadata);
    
    EXPECT_TRUE(store.has(checksum));
    EXPECT_TRUE(store.hasMetadata(checksum));
}

TEST_F(BlockMetadataTest, GetMetadata) {
    DiskBlockStore store(testDir.string(), BlockSize::Small);
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    
    BlockMetadata metadata(BlockSize::Small, data.size());
    auto checksum = store.put(data, metadata);
    
    auto retrieved = store.getMetadata(checksum);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->size, BlockSize::Small);
    EXPECT_EQ(retrieved->length_without_padding, data.size());
}

TEST_F(BlockMetadataTest, MetadataNotFound) {
    DiskBlockStore store(testDir.string(), BlockSize::Small);
    std::vector<uint8_t> data = {1, 2, 3};
    auto checksum = Checksum::fromData(data);
    
    auto retrieved = store.getMetadata(checksum);
    EXPECT_FALSE(retrieved.has_value());
    EXPECT_FALSE(store.hasMetadata(checksum));
}

TEST_F(BlockMetadataTest, PutMetadataSeparately) {
    DiskBlockStore store(testDir.string(), BlockSize::Medium);
    std::vector<uint8_t> data = {10, 20, 30};
    
    auto checksum = store.put(data);
    EXPECT_TRUE(store.hasMetadata(checksum));
    
    BlockMetadata newMetadata(BlockSize::Medium, 100);
    store.putMetadata(checksum, newMetadata);
    
    auto retrieved = store.getMetadata(checksum);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->length_without_padding, 100);
}

TEST_F(BlockMetadataTest, RemoveDeletesMetadata) {
    DiskBlockStore store(testDir.string(), BlockSize::Tiny);
    std::vector<uint8_t> data = {7, 8, 9};
    
    BlockMetadata metadata(BlockSize::Tiny, data.size());
    auto checksum = store.put(data, metadata);
    
    EXPECT_TRUE(store.hasMetadata(checksum));
    store.remove(checksum);
    EXPECT_FALSE(store.hasMetadata(checksum));
}
