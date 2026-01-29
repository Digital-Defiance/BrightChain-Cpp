#include <gtest/gtest.h>
#include "brightchain/disk_block_store.hpp"
#include <filesystem>

using namespace brightchain;

class DiskBlockStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        testPath = std::filesystem::temp_directory_path() / "brightchain_test";
        std::filesystem::remove_all(testPath);
    }

    void TearDown() override {
        std::filesystem::remove_all(testPath);
    }

    std::filesystem::path testPath;
};

TEST_F(DiskBlockStoreTest, Construction) {
    DiskBlockStore store(testPath.string(), BlockSize::Medium);
    EXPECT_EQ(store.blockSize(), BlockSize::Medium);
    EXPECT_EQ(store.storePath(), testPath.string());
    EXPECT_TRUE(std::filesystem::exists(testPath));
}

TEST_F(DiskBlockStoreTest, PutAndGet) {
    DiskBlockStore store(testPath.string(), BlockSize::Small);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    auto checksum = store.put(data);
    
    EXPECT_FALSE(checksum.toHex().empty());
    
    auto retrieved = store.get(checksum);
    EXPECT_EQ(data, retrieved);
}

TEST_F(DiskBlockStoreTest, Has) {
    DiskBlockStore store(testPath.string(), BlockSize::Tiny);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto checksum = store.put(data);
    
    EXPECT_TRUE(store.has(checksum));
    
    auto nonExistent = Checksum::fromData({9, 9, 9});
    EXPECT_FALSE(store.has(nonExistent));
}

TEST_F(DiskBlockStoreTest, Remove) {
    DiskBlockStore store(testPath.string(), BlockSize::Message);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto checksum = store.put(data);
    
    EXPECT_TRUE(store.has(checksum));
    EXPECT_TRUE(store.remove(checksum));
    EXPECT_FALSE(store.has(checksum));
    EXPECT_FALSE(store.remove(checksum)); // Already removed
}

TEST_F(DiskBlockStoreTest, GetNonExistent) {
    DiskBlockStore store(testPath.string(), BlockSize::Medium);
    
    auto nonExistent = Checksum::fromData({9, 9, 9});
    EXPECT_THROW(store.get(nonExistent), std::runtime_error);
}

TEST_F(DiskBlockStoreTest, DirectoryStructure) {
    DiskBlockStore store(testPath.string(), BlockSize::Small);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto checksum = store.put(data);
    
    std::string hex = checksum.toHex();
    auto expectedPath = testPath / "Small" / std::string(1, hex[0]) / 
                        std::string(1, hex[1]) / hex;
    
    EXPECT_TRUE(std::filesystem::exists(expectedPath));
}
