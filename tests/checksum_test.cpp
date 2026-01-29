#include <gtest/gtest.h>
#include "brightchain/checksum.hpp"

using namespace brightchain;

TEST(ChecksumTest, FromData) {
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto checksum = Checksum::fromData(data);
    
    EXPECT_EQ(checksum.hash().size(), Checksum::HASH_SIZE);
    EXPECT_FALSE(checksum.toHex().empty());
    EXPECT_EQ(checksum.toHex().length(), Checksum::HASH_SIZE * 2);
}

TEST(ChecksumTest, Deterministic) {
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto checksum1 = Checksum::fromData(data);
    auto checksum2 = Checksum::fromData(data);
    
    EXPECT_EQ(checksum1, checksum2);
    EXPECT_EQ(checksum1.toHex(), checksum2.toHex());
}

TEST(ChecksumTest, DifferentData) {
    std::vector<uint8_t> data1 = {1, 2, 3, 4, 5};
    std::vector<uint8_t> data2 = {1, 2, 3, 4, 6};
    
    auto checksum1 = Checksum::fromData(data1);
    auto checksum2 = Checksum::fromData(data2);
    
    EXPECT_NE(checksum1, checksum2);
    EXPECT_NE(checksum1.toHex(), checksum2.toHex());
}

TEST(ChecksumTest, HexRoundTrip) {
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto checksum1 = Checksum::fromData(data);
    std::string hex = checksum1.toHex();
    auto checksum2 = Checksum::fromHex(hex);
    
    EXPECT_EQ(checksum1, checksum2);
}

TEST(ChecksumTest, Comparison) {
    std::vector<uint8_t> data1 = {1, 2, 3};
    std::vector<uint8_t> data2 = {4, 5, 6};
    
    auto checksum1 = Checksum::fromData(data1);
    auto checksum2 = Checksum::fromData(data2);
    
    EXPECT_TRUE(checksum1 == checksum1);
    EXPECT_TRUE(checksum1 != checksum2);
}
