#include <gtest/gtest.h>
#include "brightchain/checksum.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace brightchain;

class SHA3CrossCompatTest : public ::testing::Test {
protected:
    json testVectors;

    void SetUp() override {
        // Try multiple locations for the test vectors file
        std::vector<std::string> paths = {
            "test_vectors_sha3.json",
            "tests/test_vectors_sha3.json",
            "../tests/test_vectors_sha3.json"
        };
        
        std::ifstream file;
        for (const auto& path : paths) {
            file.open(path);
            if (file.is_open()) break;
        }
        
        ASSERT_TRUE(file.is_open()) << "test_vectors_sha3.json not found in any expected location";
        file >> testVectors;
        file.close();
    }
};

TEST_F(SHA3CrossCompatTest, ValidateSHA3Vectors) {
    ASSERT_TRUE(testVectors.contains("vectors"));
    const auto& vectors = testVectors["vectors"];
    ASSERT_GT(vectors.size(), 0);

    for (size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        const std::string dataHex = vec["dataHex"];
        const std::string expectedHash = vec["hash"];
        const std::string description = vec["dataDescription"];

        // Convert hex to binary
        std::vector<uint8_t> data;
        if (!dataHex.empty()) {
            for (size_t j = 0; j < dataHex.length(); j += 2) {
                std::string byteStr = dataHex.substr(j, 2);
                uint8_t byte = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
                data.push_back(byte);
            }
        }

        // Compute checksum
        auto checksum = Checksum::fromData(data);
        std::string actualHash = checksum.toHex();

        EXPECT_EQ(actualHash, expectedHash)
            << "SHA3-512 mismatch for: " << description << " (vector " << i << ")";
    }
}

TEST_F(SHA3CrossCompatTest, EmptyDataHash) {
    const auto& vectors = testVectors["vectors"];
    auto emptyVec = std::find_if(vectors.begin(), vectors.end(),
        [](const json& v) { return v["dataDescription"] == "empty"; });

    ASSERT_NE(emptyVec, vectors.end());
    std::string expectedHash = (*emptyVec)["hash"];

    auto checksum = Checksum::fromData({});
    EXPECT_EQ(checksum.toHex(), expectedHash);
}

TEST_F(SHA3CrossCompatTest, SimpleStringHash) {
    const auto& vectors = testVectors["vectors"];
    auto simpleVec = std::find_if(vectors.begin(), vectors.end(),
        [](const json& v) { return v["dataDescription"] == "simple string \"Hello, World!\""; });

    ASSERT_NE(simpleVec, vectors.end());
    std::string expectedHash = (*simpleVec)["hash"];

    std::string data = "Hello, World!";
    std::vector<uint8_t> binData(data.begin(), data.end());
    auto checksum = Checksum::fromData(binData);
    EXPECT_EQ(checksum.toHex(), expectedHash);
}

TEST_F(SHA3CrossCompatTest, LargeBlockHash) {
    const auto& vectors = testVectors["vectors"];
    auto largeVec = std::find_if(vectors.begin(), vectors.end(),
        [](const json& v) { return v["dataDescription"] == "1KB block (0xbb repeated)"; });

    ASSERT_NE(largeVec, vectors.end());
    std::string expectedHash = (*largeVec)["hash"];

    std::vector<uint8_t> data(1024, 0xbb);
    auto checksum = Checksum::fromData(data);
    EXPECT_EQ(checksum.toHex(), expectedHash);
}

TEST_F(SHA3CrossCompatTest, ChecksumConsistency) {
    // Test that computing the same hash twice gives same result
    std::string data = "Consistency check data";
    std::vector<uint8_t> binData(data.begin(), data.end());

    auto checksum1 = Checksum::fromData(binData);
    auto checksum2 = Checksum::fromData(binData);

    EXPECT_EQ(checksum1.toHex(), checksum2.toHex());
    EXPECT_EQ(checksum1, checksum2);
}

TEST_F(SHA3CrossCompatTest, PrintHashes) {
    // Helper test to print hashes for debugging
    const auto& vectors = testVectors["vectors"];
    
    std::cout << "\n=== SHA3-512 Cross-Compatibility Check ===" << std::endl;
    for (size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        const std::string description = vec["dataDescription"];
        const std::string expectedHash = vec["hash"];

        std::cout << "\n[" << (i + 1) << "/" << vectors.size() << "] " << description
                  << "\n  Hash: " << expectedHash.substr(0, 32) << "..." << std::endl;
    }
}
