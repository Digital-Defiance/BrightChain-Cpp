#include <brightchain/cbl.hpp>
#include <brightchain/extended_cbl.hpp>
#include <gtest/gtest.h>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace brightchain;
using json = nlohmann::json;

/**
 * Cross-platform CBL compatibility test.
 * Tests that C++ can decode CBL blocks created by TypeScript and vice versa.
 */
class CBLCrossCompatTest : public ::testing::Test {
protected:
    // Test vector from TypeScript implementation
    std::vector<uint8_t> createTypeScriptCBLBlock() {
        // This should match the exact format from TypeScript:
        // [Magic(1)][Type(1)][Version(1)][CRC8(1)]
        // [CreatorId(16)][DateCreated(8)][AddressCount(4)][TupleSize(1)]
        // [OriginalDataLength(8)][OriginalChecksum(64)][IsExtended(1)][Signature(64)]
        // [Addresses...]
        
        std::vector<uint8_t> data;
        
        // Structured prefix (4 bytes)
        data.push_back(0xBC); // Magic
        data.push_back(0x02); // Type: CBL
        data.push_back(0x01); // Version
        data.push_back(0x00); // CRC8 (placeholder, should be calculated)
        
        // Creator ID (16 bytes)
        for (int i = 0; i < 16; ++i) {
            data.push_back(0x42);
        }
        
        // Date created (8 bytes) - big endian
        uint64_t timestamp = 1234567890000ULL;
        for (int i = 7; i >= 0; --i) {
            data.push_back((timestamp >> (i * 8)) & 0xFF);
        }
        
        // Address count (4 bytes) - big endian
        uint32_t addressCount = 2;
        for (int i = 3; i >= 0; --i) {
            data.push_back((addressCount >> (i * 8)) & 0xFF);
        }
        
        // Tuple size (1 byte)
        data.push_back(2);
        
        // Original data length (8 bytes) - big endian
        uint64_t dataLength = 1024;
        for (int i = 7; i >= 0; --i) {
            data.push_back((dataLength >> (i * 8)) & 0xFF);
        }
        
        // Original checksum (64 bytes)
        for (int i = 0; i < 64; ++i) {
            data.push_back(0xAB);
        }
        
        // Is extended (1 byte)
        data.push_back(0);
        
        // Signature (64 bytes)
        for (int i = 0; i < 64; ++i) {
            data.push_back(0xCD);
        }
        
        // Add 2 block addresses (64 bytes each)
        for (int addr = 0; addr < 2; ++addr) {
            for (int i = 0; i < 64; ++i) {
                data.push_back(0x10 + addr);
            }
        }
        
        return data;
    }
};

TEST_F(CBLCrossCompatTest, DecodeTypeScriptCBL) {    
    // NOTE: This test uses a mock TypeScript block structure
    // Real bi-directional testing requires actual TypeScript-generated blocks
    auto tsData = createTypeScriptCBLBlock();
    auto checksum = Checksum::fromData(tsData);
    
    EXPECT_NO_THROW({
        ConstituentBlockListBlock cbl(BlockSize::Small, tsData, checksum);
        EXPECT_EQ(cbl.addressCount(), 2);
        EXPECT_EQ(cbl.tupleSize(), 2);
        EXPECT_EQ(cbl.originalDataLength(), 1024);
    });
    
    std::cout << "\nWARNING: This test uses mock data.\n";
    std::cout << "For true bi-directional testing, generate real TypeScript blocks.\n";
    std::cout << "See: generate_cbl_vectors.js\n\n";
}

TEST_F(CBLCrossCompatTest, HeaderStructureDocumentation) {
    // Document the expected header structure for cross-platform compatibility
    
    std::cout << "\n=== CBL Header Structure (TypeScript Format) ===\n";
    std::cout << "Offset | Size | Field\n";
    std::cout << "-------|------|------\n";
    std::cout << "0      | 1    | Magic Prefix (0xBC)\n";
    std::cout << "1      | 1    | Block Type (0x02=CBL, 0x04=ExtendedCBL)\n";
    std::cout << "2      | 1    | Version (0x01)\n";
    std::cout << "3      | 1    | CRC8 (of header content)\n";
    std::cout << "4      | 16   | Creator ID (GUID)\n";
    std::cout << "20     | 8    | Date Created (uint64 BE)\n";
    std::cout << "28     | 4    | Address Count (uint32 BE)\n";
    std::cout << "32     | 1    | Tuple Size\n";
    std::cout << "33     | 8    | Original Data Length (uint64 BE)\n";
    std::cout << "41     | 64   | Original Data Checksum (SHA3-512)\n";
    std::cout << "105    | 1    | Is Extended Flag\n";
    std::cout << "106    | 64   | Creator Signature (ECDSA)\n";
    std::cout << "170    | var  | Block Addresses (64 bytes each)\n";
    std::cout << "\nTotal base header size: 170 bytes\n";
    std::cout << "\n=== Extended CBL Additional Fields ===\n";
    std::cout << "After base header (before signature):\n";
    std::cout << "Offset | Size | Field\n";
    std::cout << "-------|------|------\n";
    std::cout << "106    | 2    | File Name Length (uint16 BE)\n";
    std::cout << "108    | var  | File Name (UTF-8)\n";
    std::cout << "var    | 1    | MIME Type Length\n";
    std::cout << "var+1  | var  | MIME Type (UTF-8)\n";
    std::cout << "var    | 64   | Creator Signature\n";
    std::cout << "var    | var  | Block Addresses\n\n";
}

TEST_F(CBLCrossCompatTest, CurrentImplementationStructure) {
    // Show what our current C++ implementation produces
    
    CBLHeader header;
    header.magic = BlockHeaderConstants::MAGIC_PREFIX;
    header.version = BlockHeaderConstants::VERSION;
    header.type = static_cast<uint8_t>(StructuredBlockType::CBL);
    header.creatorId.fill(0x42);
    header.dateCreated = 1234567890000ULL;
    header.addressCount = 2;
    header.tupleSize = 2;
    header.originalDataLength = 1024;
    header.originalDataChecksum.fill(0xAB);
    header.signature.fill(0xCD);
    
    auto serialized = header.serialize();
    
    std::cout << "\n=== Current C++ Implementation ===\n";
    std::cout << "Header size: " << serialized.size() << " bytes\n";
    std::cout << "Expected: 171 bytes (TypeScript: 170 bytes)\n";
    std::cout << "\nFirst 20 bytes (hex):\n";
    for (size_t i = 0; i < std::min(size_t(20), serialized.size()); ++i) {
        printf("%02X ", serialized[i]);
    }
    std::cout << "\n\nNOTE: C++ implementation needs to be updated to match TypeScript:\n";
    std::cout << "1. Add CRC8 calculation\n";
    std::cout << "2. Use big-endian byte order\n";
    std::cout << "3. Match exact field offsets\n";
    std::cout << "4. Add 'is extended' flag before signature\n\n";
}
