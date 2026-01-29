#include <gtest/gtest.h>
#include <brightchain/shamir.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <set>

using namespace brightchain;
using json = nlohmann::json;

class ShamirComprehensiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Try multiple paths
        std::vector<std::string> paths = {
            "tests/test_vectors_comprehensive_shamir.json",
            "test_vectors_comprehensive_shamir.json",
            "../tests/test_vectors_comprehensive_shamir.json"
        };
        for (const auto& path : paths) {
            std::ifstream f(path);
            if (f.is_open()) {
                testVectorsPath = path;
                return;
            }
        }
        testVectorsPath = "tests/test_vectors_comprehensive_shamir.json";
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

// Test all bit lengths from 3 to 20
TEST_F(ShamirComprehensiveTest, AllBitLengths) {
    for (uint8_t bits = 3; bits <= 20; ++bits) {
        ShamirSecretSharing shamir(bits);
        std::string secret = randomHex(32);  // 256-bit secret

        try {
            auto shares = shamir.share(secret, 5, 3);
            std::vector<std::string> subset(shares.begin(), shares.begin() + 3);
            std::string recovered = shamir.combine(subset);

            EXPECT_EQ(recovered, secret) << "Failed for bits=" << (int)bits;
        } catch (const std::exception& e) {
            FAIL() << "Exception for bits=" << (int)bits << ": " << e.what();
        }
    }
}

// Test with TypeScript-generated vectors
TEST_F(ShamirComprehensiveTest, CombineTypeScriptGeneratedVectors) {
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

    if (!vectors.contains("shamir") || vectors["shamir"].size() == 0) {
        GTEST_SKIP() << "No Shamir vectors in test file";
        return;
    }

    int passCount = 0;
    int failCount = 0;

    for (const auto& vector : vectors["shamir"]) {
        try {
            uint8_t bits = vector["bits"];
            std::string secret = vector["secret"];
            std::vector<std::string> shares = vector["shares"];
            uint32_t threshold = vector["threshold"];

            ShamirSecretSharing shamir(bits);

            // Take exactly threshold shares
            std::vector<std::string> subset(shares.begin(), shares.begin() + threshold);
            std::string recovered = shamir.combine(subset);

            if (recovered == secret) {
                passCount++;
            } else {
                failCount++;
                EXPECT_EQ(recovered, secret) << "Failed for " << vector.value("description", "unknown");
            }
        } catch (const std::exception& e) {
            failCount++;
            FAIL() << "Exception: " << e.what();
        }
    }

    EXPECT_EQ(failCount, 0) << "Failed " << failCount << " out of " << (passCount + failCount) << " tests";
}

// Test various share counts and thresholds
TEST_F(ShamirComprehensiveTest, VariousShareCombinations) {
    const uint8_t bits = 8;
    ShamirSecretSharing shamir(bits);
    std::string secret = "deadbeefcafebabe";

    std::vector<std::pair<uint32_t, uint32_t>> configs = {
        {3, 2},   // 3 shares, 2 threshold
        {5, 3},   // 5 shares, 3 threshold
        {10, 5},  // 10 shares, 5 threshold
        {10, 7},  // 10 shares, 7 threshold
        {15, 8},  // 15 shares, 8 threshold
    };

    for (const auto& [numShares, threshold] : configs) {
        auto shares = shamir.share(secret, numShares, threshold);

        EXPECT_EQ(shares.size(), numShares) << "Share count mismatch";

        // Test with exactly threshold shares
        std::vector<std::string> subset(shares.begin(), shares.begin() + threshold);
        std::string recovered = shamir.combine(subset);
        EXPECT_EQ(recovered, secret) << "Failed with " << numShares << "/" << threshold;

        // Test with more than threshold shares
        if (numShares > threshold + 1) {
            std::vector<std::string> subset2(shares.begin(), shares.begin() + threshold + 1);
            std::string recovered2 = shamir.combine(subset2);
            EXPECT_EQ(recovered2, secret) << "Failed with extra shares " << numShares << "/" 
                                         << (threshold + 1);
        }
    }
}

// Test with different secret sizes
TEST_F(ShamirComprehensiveTest, VaryingSecretSizes) {
    const uint8_t bits = 8;
    ShamirSecretSharing shamir(bits);

    std::vector<size_t> sizes = {
        1,    // 4 bits
        2,    // 8 bits
        4,    // 16 bits
        8,    // 32 bits
        16,   // 64 bits
        32,   // 128 bits
        64,   // 256 bits
        128,  // 512 bits
    };

    for (size_t size : sizes) {
        std::string secret = randomHex(size);

        auto shares = shamir.share(secret, 5, 3);
        std::vector<std::string> subset(shares.begin(), shares.begin() + 3);
        std::string recovered = shamir.combine(subset);

        EXPECT_EQ(recovered, secret) << "Failed for " << (size * 4) << " bits";
    }
}

// Test edge cases
TEST_F(ShamirComprehensiveTest, EdgeCases) {
    const uint8_t bits = 8;
    ShamirSecretSharing shamir(bits);

    std::vector<std::pair<std::string, std::string>> edgeCases = {
        {"00", "Single zero byte"},
        {"ff", "Single 0xFF byte"},
        {"0000000000000000", "All zeros"},
        {"ffffffffffffffff", "All ones"},
        {"0102030405060708090a0b0c0d0e0f", "Sequential pattern"},
        {"aaaaaaaaaaaaaaaa", "Repeated pattern"},
    };

    for (const auto& [secret, description] : edgeCases) {
        auto shares = shamir.share(secret, 5, 3);
        std::vector<std::string> subset(shares.begin(), shares.begin() + 3);
        std::string recovered = shamir.combine(subset);

        EXPECT_EQ(recovered, secret) << "Failed: " << description;
    }
}

// Test that different share combinations give same result
TEST_F(ShamirComprehensiveTest, DifferentShareSubsets) {
    const uint8_t bits = 8;
    ShamirSecretSharing shamir(bits);
    std::string secret = "deadbeefcafebabe";

    auto shares = shamir.share(secret, 10, 5);

    // Combine different subsets of 5 shares
    std::vector<std::set<size_t>> subsets = {
        {0, 1, 2, 3, 4},
        {0, 1, 2, 3, 9},
        {0, 1, 5, 7, 9},
        {2, 4, 6, 8, 9},
    };

    for (const auto& subset_indices : subsets) {
        std::vector<std::string> subset;
        for (size_t i : subset_indices) {
            subset.push_back(shares[i]);
        }

        std::string recovered = shamir.combine(subset);
        EXPECT_EQ(recovered, secret) << "Failed with subset";
    }
}

// Test insufficient shares fails
TEST_F(ShamirComprehensiveTest, InsufficientSharesFails) {
    const uint8_t bits = 8;
    ShamirSecretSharing shamir(bits);
    std::string secret = "deadbeefcafebabe";

    auto shares = shamir.share(secret, 10, 5);

    // Try with less than threshold shares
    std::vector<std::string> subset(shares.begin(), shares.begin() + 4);

    EXPECT_THROW(shamir.combine(subset), std::exception) 
        << "Should fail with insufficient shares";
}

// Test mixed bit lengths
TEST_F(ShamirComprehensiveTest, MultipleBitLengthsSequence) {
    std::vector<uint8_t> bitLengths = {3, 5, 8, 10, 12, 16, 20};
    std::string secret = "deadbeefcafebabe";

    for (uint8_t bits : bitLengths) {
        ShamirSecretSharing shamir(bits);
        auto shares = shamir.share(secret, 5, 3);
        std::vector<std::string> subset(shares.begin(), shares.begin() + 3);
        std::string recovered = shamir.combine(subset);

        EXPECT_EQ(recovered, secret) << "Failed for bits=" << (int)bits;
    }
}
