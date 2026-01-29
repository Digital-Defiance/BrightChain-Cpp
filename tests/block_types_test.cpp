#include <brightchain/raw_data_block.hpp>
#include <brightchain/cbl.hpp>
#include <brightchain/extended_cbl.hpp>
#include <brightchain/super_cbl.hpp>
#include <gtest/gtest.h>

using namespace brightchain;

TEST(BlockTypesTest, RawDataBlock) {
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto checksum = Checksum::fromData(data);
    
    RawDataBlock block(BlockSize::Message, data, checksum);
    
    EXPECT_EQ(block.blockSize(), BlockSize::Message);
    EXPECT_EQ(block.blockType(), BlockType::RawData);
    EXPECT_EQ(block.data(), data);
    EXPECT_NO_THROW(block.validateSync());
}

TEST(BlockTypesTest, CBLHeader) {
    CBLHeader header;
    header.magic = BlockHeaderConstants::MAGIC_PREFIX;
    header.version = BlockHeaderConstants::VERSION;
    header.type = static_cast<uint8_t>(StructuredBlockType::CBL);
    header.creatorId.fill(0x42);
    header.dateCreated = 1234567890;
    header.addressCount = 5;
    header.tupleSize = 3;
    header.originalDataLength = 1024;
    header.originalDataChecksum.fill(0xAB);
    header.signature.fill(0xCD);
    
    auto serialized = header.serialize();
    EXPECT_EQ(serialized.size(), CBLHeader::SIZE);
    
    auto deserialized = CBLHeader::deserialize(serialized);
    EXPECT_EQ(deserialized.magic, header.magic);
    EXPECT_EQ(deserialized.version, header.version);
    EXPECT_EQ(deserialized.addressCount, header.addressCount);
    EXPECT_EQ(deserialized.tupleSize, header.tupleSize);
}

TEST(BlockTypesTest, CBLBlock) {
    // Create a CBL header
    CBLHeader header;
    header.magic = BlockHeaderConstants::MAGIC_PREFIX;
    header.version = BlockHeaderConstants::VERSION;
    header.type = static_cast<uint8_t>(StructuredBlockType::CBL);
    header.creatorId.fill(0x42);
    header.dateCreated = 1234567890;
    header.addressCount = 2;
    header.tupleSize = 2;
    header.originalDataLength = 128;
    header.originalDataChecksum.fill(0xAB);
    header.signature.fill(0xCD);
    
    // Serialize header and add two checksums
    auto data = header.serialize();
    Checksum::HashArray hash1, hash2;
    hash1.fill(0x11);
    hash2.fill(0x22);
    data.insert(data.end(), hash1.begin(), hash1.end());
    data.insert(data.end(), hash2.begin(), hash2.end());
    
    auto checksum = Checksum::fromData(data);
    ConstituentBlockListBlock cbl(BlockSize::Small, data, checksum);
    
    EXPECT_EQ(cbl.addressCount(), 2);
    EXPECT_EQ(cbl.tupleSize(), 2);
    EXPECT_EQ(cbl.originalDataLength(), 128);
    
    auto addresses = cbl.addresses();
    EXPECT_EQ(addresses.size(), 2);
}

TEST(BlockTypesTest, ExtendedCBLMetadata) {
    ExtendedCBLMetadata metadata;
    metadata.fileName = "test.txt";
    metadata.mimeType = "text/plain";
    
    auto serialized = metadata.serialize();
    auto deserialized = ExtendedCBLMetadata::deserialize(serialized, 0);
    
    EXPECT_EQ(deserialized.fileName, metadata.fileName);
    EXPECT_EQ(deserialized.mimeType, metadata.mimeType);
}

TEST(BlockTypesTest, ExtendedCBL) {
    // ExtendedCBL structure is complex - see CBLBidirectionalTest for full tests
    // This just verifies metadata serialization
    ExtendedCBLMetadata metadata;
    metadata.fileName = "document.pdf";
    metadata.mimeType = "application/pdf";
    
    auto serialized = metadata.serialize();
    auto deserialized = ExtendedCBLMetadata::deserialize(serialized, 0);
    
    EXPECT_EQ(deserialized.fileName, "document.pdf");
    EXPECT_EQ(deserialized.mimeType, "application/pdf");
}

TEST(BlockTypesTest, SuperCBL) {
    SuperCBLHeader header;
    header.magic = BlockHeaderConstants::MAGIC_PREFIX;
    header.version = BlockHeaderConstants::VERSION;
    header.type = static_cast<uint8_t>(StructuredBlockType::SuperCBL);
    header.creatorId.fill(0x77);
    header.dateCreated = 1234567890000ULL;
    header.subCblCount = 3;
    header.totalBlockCount = 15;
    header.depth = 2;
    header.originalDataLength = 10240;
    header.originalDataChecksum.fill(0xBB);
    header.signature.fill(0xCC);
    
    auto serialized = header.serialize();
    EXPECT_EQ(serialized.size(), SuperCBLHeader::SIZE);
    
    auto deserialized = SuperCBLHeader::deserialize(serialized);
    EXPECT_EQ(deserialized.magic, header.magic);
    EXPECT_EQ(deserialized.subCblCount, header.subCblCount);
    EXPECT_EQ(deserialized.depth, header.depth);
}

TEST(BlockTypesTest, SuperCBLBlock) {
    SuperCBLHeader header;
    header.magic = BlockHeaderConstants::MAGIC_PREFIX;
    header.version = BlockHeaderConstants::VERSION;
    header.type = static_cast<uint8_t>(StructuredBlockType::SuperCBL);
    header.creatorId.fill(0x88);
    header.dateCreated = 1234567890000ULL;
    header.subCblCount = 2;
    header.totalBlockCount = 10;
    header.depth = 1;
    header.originalDataLength = 5120;
    header.originalDataChecksum.fill(0xDD);
    header.signature.fill(0xEE);
    
    auto data = header.serialize();
    Checksum::HashArray hash1, hash2;
    hash1.fill(0x44);
    hash2.fill(0x55);
    data.insert(data.end(), hash1.begin(), hash1.end());
    data.insert(data.end(), hash2.begin(), hash2.end());
    
    auto checksum = Checksum::fromData(data);
    SuperCBL scbl(BlockSize::Small, data, checksum);
    
    EXPECT_EQ(scbl.subCblCount(), 2);
    EXPECT_EQ(scbl.totalBlockCount(), 10);
    EXPECT_EQ(scbl.depth(), 1);
    EXPECT_EQ(scbl.originalDataLength(), 5120);
    
    auto checksums = scbl.subCblChecksums();
    EXPECT_EQ(checksums.size(), 2);
}
