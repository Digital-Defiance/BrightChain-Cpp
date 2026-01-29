#include <brightchain/cbl.hpp>
#include <brightchain/extended_cbl.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

using namespace brightchain;
using json = nlohmann::json;

int main() {
    // Generate CBL block
    CBLHeader cblHeader;
    cblHeader.magic = 0xBC;
    cblHeader.type = 0x02;
    cblHeader.version = 0x01;
    cblHeader.creatorId.fill(0x42);
    cblHeader.dateCreated = 1234567890000ULL;
    cblHeader.addressCount = 3;
    cblHeader.tupleSize = 3;
    cblHeader.originalDataLength = 3072;
    cblHeader.originalDataChecksum.fill(0xAB);
    cblHeader.isExtended = 0;
    cblHeader.signature.fill(0xCD);

    auto cblData = cblHeader.serialize();
    
    // Add 3 addresses
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 64; ++j) {
            cblData.push_back(0x10 + i);
        }
    }

    // Generate ExtendedCBL block
    CBLHeader ecblHeader;
    ecblHeader.magic = 0xBC;
    ecblHeader.type = 0x04;
    ecblHeader.version = 0x01;
    ecblHeader.creatorId.fill(0x99);
    ecblHeader.dateCreated = 1234567890000ULL;
    ecblHeader.addressCount = 2;
    ecblHeader.tupleSize = 2;
    ecblHeader.originalDataLength = 2048;
    ecblHeader.originalDataChecksum.fill(0xEF);
    ecblHeader.isExtended = 1;
    
    // Don't add signature yet - need to add metadata first
    auto ecblData = std::vector<uint8_t>();
    ecblData.push_back(ecblHeader.magic);
    ecblData.push_back(ecblHeader.type);
    ecblData.push_back(ecblHeader.version);
    ecblData.push_back(0); // CRC8 placeholder
    
    for (auto b : ecblHeader.creatorId) ecblData.push_back(b);
    for (int i = 7; i >= 0; --i) ecblData.push_back((ecblHeader.dateCreated >> (i * 8)) & 0xFF);
    for (int i = 3; i >= 0; --i) ecblData.push_back((ecblHeader.addressCount >> (i * 8)) & 0xFF);
    ecblData.push_back(ecblHeader.tupleSize);
    for (int i = 7; i >= 0; --i) ecblData.push_back((ecblHeader.originalDataLength >> (i * 8)) & 0xFF);
    for (auto b : ecblHeader.originalDataChecksum) ecblData.push_back(b);
    ecblData.push_back(1); // isExtended
    
    // Add file metadata
    std::string fileName = "test.txt";
    std::string mimeType = "text/plain";
    uint16_t fileNameLen = fileName.length();
    ecblData.push_back((fileNameLen >> 8) & 0xFF);
    ecblData.push_back(fileNameLen & 0xFF);
    for (char c : fileName) ecblData.push_back(c);
    ecblData.push_back(mimeType.length());
    for (char c : mimeType) ecblData.push_back(c);
    
    // Calculate CRC8 before adding signature
    uint8_t crc = 0;
    for (size_t i = 4; i < ecblData.size(); ++i) {
        crc ^= ecblData[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    ecblData[3] = crc;
    
    // Add signature
    for (int i = 0; i < 64; ++i) ecblData.push_back(0xFE);
    
    // Add 2 addresses
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 64; ++j) {
            ecblData.push_back(0x20 + i);
        }
    }

    // Create JSON output
    json vectors;
    
    std::stringstream cblHex;
    for (auto b : cblData) cblHex << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    
    std::stringstream ecblHex;
    for (auto b : ecblData) ecblHex << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    
    vectors["cbl"] = {
        {"hex", cblHex.str()},
        {"addressCount", 3},
        {"tupleSize", 3},
        {"originalDataLength", 3072}
    };
    
    vectors["extendedCbl"] = {
        {"hex", ecblHex.str()},
        {"addressCount", 2},
        {"tupleSize", 2},
        {"originalDataLength", 2048},
        {"fileName", "test.txt"},
        {"mimeType", "text/plain"}
    };

    std::ofstream out("cbl_test_vectors.json");
    out << vectors.dump(2);
    out.close();

    std::cout << "Generated cbl_test_vectors.json\n";
    std::cout << "CBL size: " << cblData.size() << " bytes\n";
    std::cout << "ExtendedCBL size: " << ecblData.size() << " bytes\n";

    return 0;
}
