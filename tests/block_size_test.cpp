#include <gtest/gtest.h>
#include "brightchain/block_size.hpp"

using namespace brightchain;

TEST(BlockSizeTest, ValidBlockSizes) {
    EXPECT_EQ(blockSizeToLength(BlockSize::Message), 512);
    EXPECT_EQ(blockSizeToLength(BlockSize::Tiny), 1024);
    EXPECT_EQ(blockSizeToLength(BlockSize::Small), 4096);
    EXPECT_EQ(blockSizeToLength(BlockSize::Medium), 1048576);
    EXPECT_EQ(blockSizeToLength(BlockSize::Large), 67108864);
    EXPECT_EQ(blockSizeToLength(BlockSize::Huge), 268435456);
}

TEST(BlockSizeTest, ValidateBlockSize) {
    EXPECT_TRUE(validateBlockSize(512));
    EXPECT_TRUE(validateBlockSize(1024));
    EXPECT_TRUE(validateBlockSize(4096));
    EXPECT_TRUE(validateBlockSize(1048576));
    EXPECT_FALSE(validateBlockSize(1000));
    EXPECT_TRUE(validateBlockSize(1000, true)); // Allow non-standard
}

TEST(BlockSizeTest, LengthToBlockSize) {
    EXPECT_EQ(lengthToBlockSize(512), BlockSize::Message);
    EXPECT_EQ(lengthToBlockSize(1024), BlockSize::Tiny);
    EXPECT_EQ(lengthToBlockSize(4096), BlockSize::Small);
    EXPECT_THROW(lengthToBlockSize(1000, false), std::invalid_argument);
}

TEST(BlockSizeTest, LengthToClosestBlockSize) {
    EXPECT_EQ(lengthToClosestBlockSize(100), BlockSize::Message);
    EXPECT_EQ(lengthToClosestBlockSize(512), BlockSize::Message);
    EXPECT_EQ(lengthToClosestBlockSize(513), BlockSize::Tiny);
    EXPECT_EQ(lengthToClosestBlockSize(1024), BlockSize::Tiny);
    EXPECT_EQ(lengthToClosestBlockSize(5000), BlockSize::Medium);
    EXPECT_EQ(lengthToClosestBlockSize(300000000), BlockSize::Huge);
}

TEST(BlockSizeTest, BlockSizeToString) {
    EXPECT_EQ(blockSizeToString(BlockSize::Unknown), "Unknown");
    EXPECT_EQ(blockSizeToString(BlockSize::Message), "Message");
    EXPECT_EQ(blockSizeToString(BlockSize::Tiny), "Tiny");
    EXPECT_EQ(blockSizeToString(BlockSize::Small), "Small");
    EXPECT_EQ(blockSizeToString(BlockSize::Medium), "Medium");
    EXPECT_EQ(blockSizeToString(BlockSize::Large), "Large");
    EXPECT_EQ(blockSizeToString(BlockSize::Huge), "Huge");
}
