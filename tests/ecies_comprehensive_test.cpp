#include <gtest/gtest.h>
#include <brightchain/ecies.hpp>
#include <brightchain/ec_key_pair.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>

using namespace brightchain;
using json = nlohmann::json;

class EciesComprehensiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Try multiple paths
        std::vector<std::string> paths = {
            "tests/test_vectors_comprehensive_ecies.json",
            "test_vectors_comprehensive_ecies.json",
            "../tests/test_vectors_comprehensive_ecies.json"
        };
        for (const auto& path : paths) {
            std::ifstream f(path);
            if (f.is_open()) {
                testVectorsPath = path;
                return;
            }
        }
        testVectorsPath = "tests/test_vectors_comprehensive_ecies.json";
    }

    std::string testVectorsPath;

    std::string randomHex(size_t bytes) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);

        std::string result;
        static const char hexChars[] = "0123456789abcdef";
        for (size_t i = 0; i < bytes * 2; ++i) {
            result += hexChars[dis(gen)];
        }
        return result;
    }
};

// Test with TypeScript-generated vectors
TEST_F(EciesComprehensiveTest, DecryptTypeScriptGeneratedVectors) {
    std::ifstream file(testVectorsPath);
    if (!file.is_open()) {
        GTEST_SKIP() << "Comprehensive test vectors not found. Run generate_comprehensive_vectors.ts first.";
        return;
    }

    json vectors;
    try {
        file >> vectors;
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Failed to parse test vectors: " << e.what();
        return;
    }

    if (!vectors.contains("ecies") || vectors["ecies"].size() == 0) {
        GTEST_SKIP() << "No ECIES vectors in test file";
        return;
    }

    int passCount = 0;
    int failCount = 0;
    std::string lastError;

    for (const auto& vector : vectors["ecies"]) {
        try {
            std::string encryptionType = vector["encryptionType"];
            std::string plaintext = vector["plaintext"];
            std::string publicKeyStr = vector["publicKey"];
            std::string ciphertext = vector["ciphertext"];
            std::string name = vector["name"];

            EciesEncryptionType type = (encryptionType == "Basic") ? 
                EciesEncryptionType::Basic : EciesEncryptionType::WithLength;

            ECIES ecies;
            std::string recovered = ecies.decryptWithComponents(
                type,
                publicKeyStr,
                ciphertext
            );

            if (recovered == plaintext) {
                passCount++;
            } else {
                failCount++;
                lastError = "Mismatch for " + name + ": expected " + plaintext.substr(0, 20) + 
                           "..., got " + recovered.substr(0, 20) + "...";
            }
        } catch (const std::exception& e) {
            failCount++;
            lastError = std::string(e.what());
        }
    }

    EXPECT_EQ(failCount, 0) << "Failed " << failCount << " tests. Last error: " << lastError 
                            << ". Passed: " << passCount;
}

// Test with random data
TEST_F(EciesComprehensiveTest, RoundTripRandomData) {
    ECIES ecies;
    ECKeyPair keyPair;

    std::vector<std::string> testData = {
        "",                                      // Empty
        "aa",                                   // 1 byte
        "aabbccdd",                             // 4 bytes
        "deadbeefcafebabe",                     // 8 bytes
        randomHex(32),                          // 32 bytes
        randomHex(256),                         // 256 bytes
    };

    int passCount = 0;

    for (const auto& data : testData) {
        for (const auto& type : {EciesEncryptionType::Basic, EciesEncryptionType::WithLength}) {
            try {
                std::string ciphertext = ecies.encrypt(type, keyPair.publicKeyHex(), data);
                std::string recovered = ecies.decryptWithComponents(type, keyPair.publicKeyHex(), ciphertext);

                EXPECT_EQ(recovered, data) << "Round-trip failed for " << data.substr(0, 20) 
                                          << "... with type " << (int)type;
                if (recovered == data) passCount++;
            } catch (const std::exception& e) {
                FAIL() << "Exception during round-trip: " << e.what();
            }
        }
    }

    EXPECT_GE(passCount, testData.size() * 2) << "Not all round-trip tests passed";
}

// Test with same key for multiple encryptions (ensure different ciphertexts due to random IV)
TEST_F(EciesComprehensiveTest, SameDataDifferentCiphertexts) {
    ECIES ecies;
    ECKeyPair keyPair;

    std::string plaintext = "deadbeefcafebabe";
    std::string ct1 = ecies.encrypt(EciesEncryptionType::Basic, keyPair.publicKeyHex(), plaintext);
    std::string ct2 = ecies.encrypt(EciesEncryptionType::Basic, keyPair.publicKeyHex(), plaintext);

    // Ciphertexts should be different (different IV/ephemeral key)
    EXPECT_NE(ct1, ct2) << "Same plaintext produced same ciphertext (IV should be random)";

    // But should decrypt to same plaintext
    std::string recovered1 = ecies.decryptWithComponents(EciesEncryptionType::Basic, 
                                                         keyPair.publicKeyHex(), ct1);
    std::string recovered2 = ecies.decryptWithComponents(EciesEncryptionType::Basic, 
                                                         keyPair.publicKeyHex(), ct2);

    EXPECT_EQ(recovered1, plaintext);
    EXPECT_EQ(recovered2, plaintext);
}

// Test WithLength vs Basic modes
TEST_F(EciesComprehensiveTest, BothEncryptionModes) {
    ECIES ecies;
    ECKeyPair keyPair;

    std::vector<std::string> testSizes = {"", "aa", "deadbeef", randomHex(100)};

    for (const auto& plaintext : testSizes) {
        // Basic mode
        std::string basicCiphertext = ecies.encrypt(EciesEncryptionType::Basic, 
                                                    keyPair.publicKeyHex(), plaintext);
        std::string basicRecovered = ecies.decryptWithComponents(EciesEncryptionType::Basic, 
                                                                 keyPair.publicKeyHex(), 
                                                                 basicCiphertext);
        EXPECT_EQ(basicRecovered, plaintext) << "Basic mode failed for size " << plaintext.length();

        // WithLength mode
        std::string withLenCiphertext = ecies.encrypt(EciesEncryptionType::WithLength, 
                                                      keyPair.publicKeyHex(), plaintext);
        std::string withLenRecovered = ecies.decryptWithComponents(EciesEncryptionType::WithLength, 
                                                                   keyPair.publicKeyHex(), 
                                                                   withLenCiphertext);
        EXPECT_EQ(withLenRecovered, plaintext) << "WithLength mode failed for size " << plaintext.length();

        // Ciphertexts should be different due to different modes
        EXPECT_NE(basicCiphertext, withLenCiphertext) << "Basic and WithLength produced same ciphertext";
    }
}
