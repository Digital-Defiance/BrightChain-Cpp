#include <gtest/gtest.h>
#include "brightchain/shamir.hpp"

using namespace brightchain;

TEST(ShamirTest, ShareAndCombine) {
    ShamirSecretSharing shamir(8);
    
    std::string secret = "deadbeef";
    auto shares = shamir.share(secret, 5, 3);
    
    EXPECT_EQ(shares.size(), 5);
    
    // Combine with threshold shares
    std::vector<std::string> subset = {shares[0], shares[2], shares[4]};
    auto recovered = shamir.combine(subset);
    
    EXPECT_EQ(recovered, secret);
}

TEST(ShamirTest, AllSharesCombine) {
    ShamirSecretSharing shamir(8);
    
    std::string secret = "abc123";
    auto shares = shamir.share(secret, 5, 3);
    
    // All shares should also work
    auto recovered = shamir.combine(shares);
    EXPECT_EQ(recovered, secret);
}

TEST(ShamirTest, InsufficientShares) {
    ShamirSecretSharing shamir(8);
    
    std::string secret = "deadbeef";  // Valid hex string
    auto shares = shamir.share(secret, 5, 3);
    
    // Only 2 shares (less than threshold of 3)
    std::vector<std::string> subset = {shares[0], shares[1]};
    
    // With insufficient shares, Lagrange interpolation still produces a result,
    // but it won't be the original secret (mathematically guaranteed)
    auto recovered = shamir.combine(subset);
    EXPECT_NE(recovered, secret);
}

TEST(ShamirTest, DifferentBitLengths) {
    ShamirSecretSharing shamir10(10);
    
    std::string secret = "deadbeef";  // Use simple hex string
    auto shares = shamir10.share(secret, 100, 50);
    
    EXPECT_EQ(shares.size(), 100);
    
    std::vector<std::string> subset(shares.begin(), shares.begin() + 50);
    auto recovered = shamir10.combine(subset);
    
    EXPECT_EQ(recovered, secret);
}

TEST(ShamirTest, LongSecret) {
    ShamirSecretSharing shamir(8);
    
    std::string secret = "0123456789abcdef0123456789abcdef";
    auto shares = shamir.share(secret, 10, 5);
    
    std::vector<std::string> subset = {shares[1], shares[3], shares[5], shares[7], shares[9]};
    auto recovered = shamir.combine(subset);
    
    EXPECT_EQ(recovered, secret);
}
