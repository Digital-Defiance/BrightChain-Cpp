#include <brightchain/cbl.hpp>
#include <brightchain/extended_cbl.hpp>
#include <gtest/gtest.h>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace brightchain;
using json = nlohmann::json;

class CBLBidirectionalTest : public ::testing::Test {
protected:
    json vectors;
    
    void SetUp() override {
        // Try multiple locations for the JSON file
        const char* paths[] = {
            "cbl_test_vectors.json",
            "../cbl_test_vectors.json",
            "../../cbl_test_vectors.json"
        };
        
        for (const char* path : paths) {
            std::ifstream f(path);
            if (f.is_open()) {
                f >> vectors;
                return;
            }
        }
        
        FAIL() << "cbl_test_vectors.json not found. Run: ./build/examples/generate_test_vectors";
    }
    
    std::vector<uint8_t> hexToBytes(const std::string& hex) {
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < hex.length(); i += 2) {
            bytes.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
        }
        return bytes;
    }
};

TEST_F(CBLBidirectionalTest, DecodeCppGeneratedCBL) {
    auto hex = vectors["cbl"]["hex"].get<std::string>();
    auto data = hexToBytes(hex);
    auto checksum = Checksum::fromData(data);
    
    ConstituentBlockListBlock cbl(BlockSize::Small, data, checksum);
    
    EXPECT_EQ(cbl.addressCount(), vectors["cbl"]["addressCount"].get<uint32_t>());
    EXPECT_EQ(cbl.tupleSize(), vectors["cbl"]["tupleSize"].get<uint32_t>());
    EXPECT_EQ(cbl.originalDataLength(), vectors["cbl"]["originalDataLength"].get<uint64_t>());
    
    auto addresses = cbl.addresses();
    EXPECT_EQ(addresses.size(), 3);
}

TEST_F(CBLBidirectionalTest, DecodeCppGeneratedExtendedCBL) {
    auto hex = vectors["extendedCbl"]["hex"].get<std::string>();
    auto data = hexToBytes(hex);
    auto checksum = Checksum::fromData(data);
    
    ExtendedCBL ecbl(BlockSize::Small, data, checksum);
    
    EXPECT_EQ(ecbl.addressCount(), vectors["extendedCbl"]["addressCount"].get<uint32_t>());
    EXPECT_EQ(ecbl.tupleSize(), vectors["extendedCbl"]["tupleSize"].get<uint32_t>());
    EXPECT_EQ(ecbl.originalDataLength(), vectors["extendedCbl"]["originalDataLength"].get<uint64_t>());
    EXPECT_EQ(ecbl.fileName(), vectors["extendedCbl"]["fileName"].get<std::string>());
    EXPECT_EQ(ecbl.mimeType(), vectors["extendedCbl"]["mimeType"].get<std::string>());
    
    auto addresses = ecbl.addresses();
    EXPECT_EQ(addresses.size(), 2);
}

TEST_F(CBLBidirectionalTest, RoundTripCBL) {
    // Create a CBL in C++
    CBLHeader header;
    header.magic = 0xBC;
    header.type = 0x02;
    header.version = 0x01;
    header.creatorId.fill(0x55);
    header.dateCreated = 9876543210000ULL;
    header.addressCount = 2;
    header.tupleSize = 2;
    header.originalDataLength = 2048;
    header.originalDataChecksum.fill(0xCC);
    header.isExtended = 0;
    header.signature.fill(0xDD);
    
    auto data = header.serialize();
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 64; ++j) {
            data.push_back(0x30 + i);
        }
    }
    
    auto checksum = Checksum::fromData(data);
    
    // Decode it back
    ConstituentBlockListBlock cbl(BlockSize::Small, data, checksum);
    EXPECT_EQ(cbl.addressCount(), 2);
    EXPECT_EQ(cbl.tupleSize(), 2);
    EXPECT_EQ(cbl.originalDataLength(), 2048);
}
