#include <gtest/gtest.h>
#include <brightchain/shamir.hpp>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace brightchain;
using json = nlohmann::json;

class ShamirCrossCompatTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Try multiple relative paths
        std::vector<std::string> paths = {
            "tests/test_vectors_shamir.json",
            "test_vectors_shamir.json",
            "../tests/test_vectors_shamir.json"
        };
        for (const auto& path : paths) {
            std::ifstream f(path);
            if (f.is_open()) {
                testVectorsPath = path;
                return;
            }
        }
        testVectorsPath = "tests/test_vectors_shamir.json";
    }
    
    std::string testVectorsPath;
};

TEST_F(ShamirCrossCompatTest, CombineTypeScriptShares) {
    // Load test vectors from TypeScript
    std::ifstream file(testVectorsPath);
    if (!file.is_open()) {
        GTEST_SKIP() << "Test vectors file not found. Run generate_test_vectors.ts first.";
        return;
    }
    
    json vectors;
    file >> vectors;
    
    for (const auto& vector : vectors["shamir"]) {
        uint8_t bits = vector["bits"];
        std::string secret = vector["secret"];
        std::vector<std::string> shares = vector["shares"];
        uint32_t threshold = vector["threshold"];
        
        ShamirSecretSharing shamir(bits);
        
        // Take threshold number of shares
        std::vector<std::string> subset(shares.begin(), shares.begin() + threshold);
        
        // Combine TypeScript-generated shares
        std::string recovered = shamir.combine(subset);
        
        EXPECT_EQ(recovered, secret) << "Failed to combine TypeScript shares (bits=" << (int)bits << ")";
    }
}

TEST_F(ShamirCrossCompatTest, GenerateSharesForTypeScript) {
    // Generate test data that TypeScript can combine
    std::string outputPath = "tests/test_vectors_cpp_shamir.json";
    
    json output;
    output["shamir"] = json::array();
    
    // Test with 8 bits
    {
        ShamirSecretSharing shamir(8);
        std::string secret = "deadbeef";
        auto shares = shamir.share(secret, 5, 3);
        
        output["shamir"].push_back({
            {"bits", 8},
            {"secret", secret},
            {"shares", shares},
            {"threshold", 3}
        });
    }
    
    // Test with 10 bits
    {
        ShamirSecretSharing shamir(10);
        std::string secret = "cafebabe";
        auto shares = shamir.share(secret, 10, 5);
        
        output["shamir"].push_back({
            {"bits", 10},
            {"secret", secret},
            {"shares", shares},
            {"threshold", 5}
        });
    }
    
    std::ofstream outFile(outputPath);
    outFile << output.dump(2);
    
    SUCCEED() << "Generated test vectors at " << outputPath;
}
