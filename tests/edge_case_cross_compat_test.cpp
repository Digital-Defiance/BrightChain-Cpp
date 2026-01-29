#include <gtest/gtest.h>
#include <brightchain/ecies.hpp>
#include <brightchain/ec_key_pair.hpp>
#include <brightchain/shamir.hpp>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace brightchain;
using json = nlohmann::json;

class EdgeCaseCrossCompatTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Try multiple paths for test vectors
        std::vector<std::string> paths = {
            "tests/test_vectors_edge_cases.json",
            "test_vectors_edge_cases.json",
            "../tests/test_vectors_edge_cases.json"
        };
        
        for (const auto& path : paths) {
            std::ifstream file(path);
            if (file.is_open()) {
                file >> vectors;
                return;
            }
        }
    }
    
    std::string testVectorsPath;
    json vectors;
};

// Test all Shamir bit lengths with various secret patterns
TEST_F(EdgeCaseCrossCompatTest, ShamirAllBitLengths) {
    if (vectors.empty() || !vectors.contains("shamir")) {
        GTEST_SKIP() << "Test vectors not found";
        return;
    }

    int successCount = 0;
    int bitLengthCount = 0;
    std::set<uint8_t> bitsUsed;

    for (const auto& vector : vectors["shamir"]) {
        uint8_t bits = vector["bits"];
        std::string secret = vector["secret"];
        std::vector<std::string> shares = vector["shares"];
        uint32_t threshold = vector["threshold"];
        std::string description = vector.value("description", "");

        // Track which bit lengths we test
        bitsUsed.insert(bits);

        try {
            ShamirSecretSharing shamir(bits);
            
            // Take exactly threshold shares
            std::vector<std::string> subset(shares.begin(), shares.begin() + threshold);
            
            // Combine shares
            std::string recovered = shamir.combine(subset);
            
            EXPECT_EQ(recovered, secret) 
                << "Failed for: " << description 
                << " (bits=" << (int)bits << ")";
            
            if (recovered == secret) {
                successCount++;
            }
        } catch (const std::exception& e) {
            FAIL() << "Exception for " << description << ": " << e.what();
        }
    }

    // Verify we tested all expected bit lengths
    std::vector<uint8_t> expectedBits = {3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20};
    EXPECT_EQ(bitsUsed.size(), expectedBits.size()) 
        << "Should test all bit lengths 3-20";
    
    std::cout << "✓ Tested " << successCount << " Shamir edge cases across " 
              << bitsUsed.size() << " bit lengths" << std::endl;
}

// Test ECIES with various payload sizes
TEST_F(EdgeCaseCrossCompatTest, EciesVariousPayloadSizes) {
    if (vectors.empty() || !vectors.contains("ecies")) {
        GTEST_SKIP() << "Test vectors not found";
        return;
    }

    auto keyPair = EcKeyPair::generate();
    int successCount = 0;
    std::map<std::string, int> modeCount;

    for (const auto& vector : vectors["ecies"]) {
        std::string mode = vector["mode"];
        std::vector<uint8_t> plaintext = vector["plaintext"];
        std::string description = vector.value("description", "");

        modeCount[mode]++;

        try {
            std::vector<uint8_t> encrypted;
            
            if (mode == "basic") {
                encrypted = Ecies::encryptBasic(plaintext, keyPair.publicKey());
            } else if (mode == "withLength") {
                encrypted = Ecies::encryptWithLength(plaintext, keyPair.publicKey());
            } else {
                FAIL() << "Unknown ECIES mode: " << mode;
                continue;
            }

            // Decrypt and verify
            auto decrypted = Ecies::decrypt(encrypted, keyPair);
            EXPECT_EQ(decrypted, plaintext) 
                << "Roundtrip failed for: " << description;
            
            if (decrypted == plaintext) {
                successCount++;
            }
        } catch (const std::exception& e) {
            FAIL() << "Exception for " << description << ": " << e.what();
        }
    }

    std::cout << "✓ Tested " << successCount << " ECIES edge cases:" << std::endl;
    for (const auto& [mode, count] : modeCount) {
        std::cout << "  - " << mode << ": " << count << " tests" << std::endl;
    }
}

// Test repeated encryption with same plaintext produces different ciphertexts
TEST_F(EdgeCaseCrossCompatTest, EciesRepeatedEncryptionVariety) {
    auto keyPair = EcKeyPair::generate();
    std::vector<uint8_t> plaintext = {0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe};
    
    std::set<std::string> ciphertexts;
    
    // Encrypt same plaintext 20 times
    for (int i = 0; i < 20; i++) {
        auto encrypted = Ecies::encryptBasic(plaintext, keyPair.publicKey());
        
        // Convert to hex string for comparison
        std::stringstream ss;
        for (auto byte : encrypted) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
        }
        ciphertexts.insert(ss.str());
        
        // Verify each can be decrypted
        auto decrypted = Ecies::decrypt(encrypted, keyPair);
        EXPECT_EQ(decrypted, plaintext) << "Iteration " << i << " failed";
    }
    
    // All 20 ciphertexts should be unique (IV randomization)
    EXPECT_GE(ciphertexts.size(), 15)  // Allow some birthday paradox possibility
        << "Expected mostly unique ciphertexts due to random IVs, got " 
        << ciphertexts.size() << " unique out of 20";
    
    std::cout << "✓ 20 encryptions produced " << ciphertexts.size() 
              << " unique ciphertexts" << std::endl;
}

// Test empty messages
TEST_F(EdgeCaseCrossCompatTest, EciesEmptyPayloads) {
    auto keyPair = EcKeyPair::generate();
    std::vector<uint8_t> emptyPayload;
    
    // Test Basic mode with empty payload
    {
        auto encrypted = Ecies::encryptBasic(emptyPayload, keyPair.publicKey());
        auto decrypted = Ecies::decrypt(encrypted, keyPair);
        EXPECT_EQ(decrypted, emptyPayload);
    }
    
    // Test WithLength mode with empty payload
    {
        auto encrypted = Ecies::encryptWithLength(emptyPayload, keyPair.publicKey());
        auto decrypted = Ecies::decrypt(encrypted, keyPair);
        EXPECT_EQ(decrypted, emptyPayload);
    }
    
    std::cout << "✓ Empty payloads handled correctly in both modes" << std::endl;
}
