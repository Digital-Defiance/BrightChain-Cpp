#include <gtest/gtest.h>
#include <brightchain/ecies.hpp>
#include <brightchain/ec_key_pair.hpp>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace brightchain;
using json = nlohmann::json;

// Test vectors generated from TypeScript implementation
class EciesCrossCompatTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Try multiple relative paths
        std::vector<std::string> paths = {
            "tests/test_vectors_ecies.json",
            "test_vectors_ecies.json",
            "../tests/test_vectors_ecies.json"
        };
        for (const auto& path : paths) {
            std::ifstream f(path);
            if (f.is_open()) {
                testVectorsPath = path;
                return;
            }
        }
        testVectorsPath = "tests/test_vectors_ecies.json";
    }
    
    std::string testVectorsPath;
};

TEST_F(EciesCrossCompatTest, DecryptTypeScriptEncrypted) {
    // Load test vectors from TypeScript
    std::ifstream file(testVectorsPath);
    if (!file.is_open()) {
        GTEST_SKIP() << "Test vectors file not found. Run generate_test_vectors.ts first.";
        return;
    }
    
    json vectors;
    file >> vectors;
    
    for (const auto& vector : vectors["ecies"]) {
        std::string mode = vector["mode"];
        std::vector<uint8_t> privateKey = vector["privateKey"];
        std::vector<uint8_t> plaintext = vector["plaintext"];
        std::vector<uint8_t> encrypted = vector["encrypted"];
        
        // Create key pair from private key
        EcKeyPair keyPair = EcKeyPair::fromPrivateKey(privateKey);
        
        // Decrypt TypeScript-encrypted data
        std::vector<uint8_t> decrypted = Ecies::decrypt(encrypted, keyPair);
        
        EXPECT_EQ(decrypted, plaintext) << "Failed to decrypt TypeScript " << mode << " encrypted data";
    }
}

TEST_F(EciesCrossCompatTest, EncryptForTypeScript) {
    // Generate test data that TypeScript can decrypt
    std::string outputPath = "tests/test_vectors_cpp_ecies.json";
    
    json output;
    output["ecies"] = json::array();
    
    // Test Basic mode
    {
        EcKeyPair keyPair = EcKeyPair::generate();
        std::vector<uint8_t> plaintext = {0xde, 0xad, 0xbe, 0xef};
        auto encrypted = Ecies::encryptBasic(plaintext, keyPair.publicKey());
        
        output["ecies"].push_back({
            {"mode", "basic"},
            {"privateKey", keyPair.privateKey()},
            {"publicKey", keyPair.publicKey()},
            {"plaintext", plaintext},
            {"encrypted", encrypted}
        });
    }
    
    // Test WithLength mode
    {
        EcKeyPair keyPair = EcKeyPair::generate();
        std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03, 0x04, 0x05};
        auto encrypted = Ecies::encryptWithLength(plaintext, keyPair.publicKey());
        
        output["ecies"].push_back({
            {"mode", "withLength"},
            {"privateKey", keyPair.privateKey()},
            {"publicKey", keyPair.publicKey()},
            {"plaintext", plaintext},
            {"encrypted", encrypted}
        });
    }
    
    std::ofstream outFile(outputPath);
    outFile << output.dump(2);
    
    SUCCEED() << "Generated test vectors at " << outputPath;
}
