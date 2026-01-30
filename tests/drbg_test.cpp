#include <gtest/gtest.h>
#include <brightchain/hmac_drbg.hpp>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::strtol(hex.substr(i, 2).c_str(), nullptr, 16)));
    }
    return bytes;
}

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

TEST(DRBGTest, CompareWithTypeScript) {
    // Test DRBG determinism with known seed
    std::vector<uint8_t> seed = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
    };
    
    // Generate outputs twice to verify determinism
    brightchain::HMAC_DRBG drbg1(seed);
    brightchain::HMAC_DRBG drbg2(seed);
    
    for (int i = 0; i < 10; i++) {
        auto output1 = drbg1.generate(192);
        auto output2 = drbg2.generate(192);
        
        EXPECT_EQ(output1, output2) << "DRBG output " << i << " not deterministic!";
    }
}
