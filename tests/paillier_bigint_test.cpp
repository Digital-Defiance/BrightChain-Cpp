#include <gtest/gtest.h>
#include <brightchain/paillier.hpp>
#include <brightchain/member.hpp>
#include <random>

using namespace brightchain;

class PaillierBigintTest : public ::testing::Test {
protected:
    std::vector<uint8_t> randomBigint(int bytes) {
        std::vector<uint8_t> result(bytes);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (auto& b : result) {
            b = dis(gen);
        }
        return result;
    }
};

// Test with 2048-bit keys (faster for testing)
TEST_F(PaillierBigintTest, GenerateKeys2048Bits) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    ASSERT_TRUE(member.hasVotingKeys());
    ASSERT_TRUE(member.hasVotingPrivateKey());
    // Bit length may be 2047 or 2048 depending on leading zeros
    EXPECT_GE(member.votingPublicKey()->bitLength(), 2047);
    EXPECT_LE(member.votingPublicKey()->bitLength(), 2048);
}

TEST_F(PaillierBigintTest, PublicKeyConstructor) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    EXPECT_TRUE(pub != nullptr);
    // Bit length may be 2047 or 2048 depending on leading zeros
    EXPECT_GE(pub->bitLength(), 2047);
    EXPECT_LE(pub->bitLength(), 2048);
}

TEST_F(PaillierBigintTest, PrivateKeyConstructor) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto priv = member.votingPrivateKey();
    EXPECT_TRUE(priv != nullptr);
    // Bit length may be 2047 or 2048 depending on leading zeros
    EXPECT_GE(priv->bitLength(), 2047);
    EXPECT_LE(priv->bitLength(), 2048);
}

TEST_F(PaillierBigintTest, CorrectnessEncryptDecrypt20Random) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    const int tests = 20;
    
    for (int i = 0; i < tests; i++) {
        std::vector<uint8_t> plaintext = {static_cast<uint8_t>(i % 256)};
        auto ciphertext = pub->encrypt(plaintext);
        auto decrypted = priv->decrypt(ciphertext);
        
        // Compare values, handling leading zeros
        EXPECT_EQ(plaintext.size(), 1);
        EXPECT_GE(decrypted.size(), 1);
        // Get last byte (least significant)
        uint8_t expected = plaintext[0];
        uint8_t actual = decrypted.back();
        EXPECT_EQ(actual, expected) << "Failed at i=" << i;
    }
}

TEST_F(PaillierBigintTest, HomomorphicAddition20Values) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    const int tests = 20;
    std::vector<std::vector<uint8_t>> ciphertexts;
    int expectedSum = 0;
    
    for (int i = 0; i < tests; i++) {
        std::vector<uint8_t> plaintext = {static_cast<uint8_t>(i + 1)};
        ciphertexts.push_back(pub->encrypt(plaintext));
        expectedSum += (i + 1);
    }
    
    auto encSum = pub->addition(ciphertexts);
    auto decrypted = priv->decrypt(encSum);
    
    EXPECT_EQ(decrypted[0], expectedSum % 256);
}

TEST_F(PaillierBigintTest, PlaintextAddition) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    // Encrypt first value
    auto ct1 = pub->encrypt({0x05});
    
    // Add plaintexts
    std::vector<std::vector<uint8_t>> plaintexts = {{0x03}, {0x02}};
    auto result = pub->plaintextAddition(ct1, plaintexts);
    
    auto decrypted = priv->decrypt(result);
    EXPECT_EQ(decrypted[0], 0x0A); // 5 + 3 + 2 = 10
}

TEST_F(PaillierBigintTest, HomomorphicMultiplication) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    const int tests = 10;
    bool allPassed = true;
    
    for (int i = 1; i <= tests; i++) {
        std::vector<uint8_t> plaintext = {static_cast<uint8_t>(i)};
        auto ct = pub->encrypt(plaintext);
        auto multiplied = pub->multiply(ct, i);
        auto decrypted = priv->decrypt(multiplied);
        
        int expected = (i * i) % 256;
        if (decrypted[0] != expected) {
            allPassed = false;
            break;
        }
    }
    
    EXPECT_TRUE(allPassed);
}

TEST_F(PaillierBigintTest, DifferentRandomnessProducesDifferentCiphertexts) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    
    std::vector<uint8_t> plaintext = {0x42};
    auto ct1 = pub->encrypt(plaintext);
    auto ct2 = pub->encrypt(plaintext);
    
    EXPECT_NE(ct1, ct2) << "Same plaintext should produce different ciphertexts";
}

TEST_F(PaillierBigintTest, ZeroEncryptionDecryption) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    std::vector<uint8_t> zero = {0x00};
    auto ct = pub->encrypt(zero);
    auto pt = priv->decrypt(ct);
    
    EXPECT_EQ(pt[0], 0x00);
}

TEST_F(PaillierBigintTest, MaxByteValueEncryption) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    std::vector<uint8_t> maxVal = {0xFF};
    auto ct = pub->encrypt(maxVal);
    auto pt = priv->decrypt(ct);
    
    EXPECT_EQ(pt[0], 0xFF);
}

TEST_F(PaillierBigintTest, AdditionCommutative) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    auto ct1 = pub->encrypt({0x05});
    auto ct2 = pub->encrypt({0x03});
    
    auto sum1 = pub->addition({ct1, ct2});
    auto sum2 = pub->addition({ct2, ct1});
    
    auto result1 = priv->decrypt(sum1);
    auto result2 = priv->decrypt(sum2);
    
    EXPECT_EQ(result1[0], result2[0]);
    EXPECT_EQ(result1[0], 0x08);
}

TEST_F(PaillierBigintTest, AdditionAssociative) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    auto ct1 = pub->encrypt({0x02});
    auto ct2 = pub->encrypt({0x03});
    auto ct3 = pub->encrypt({0x04});
    
    // (ct1 + ct2) + ct3
    auto temp1 = pub->addition({ct1, ct2});
    auto sum1 = pub->addition({temp1, ct3});
    
    // ct1 + (ct2 + ct3)
    auto temp2 = pub->addition({ct2, ct3});
    auto sum2 = pub->addition({ct1, temp2});
    
    auto result1 = priv->decrypt(sum1);
    auto result2 = priv->decrypt(sum2);
    
    EXPECT_EQ(result1[0], result2[0]);
    EXPECT_EQ(result1[0], 0x09);
}

TEST_F(PaillierBigintTest, MultiByteValues) {
    auto member = Member::generate(MemberType::User, "Test", "test@example.com");
    member.deriveVotingKeys(2048, 64);
    
    auto pub = member.votingPublicKey();
    auto priv = member.votingPrivateKey();
    
    std::vector<uint8_t> value = {0x12, 0x34};
    auto ct = pub->encrypt(value);
    auto pt = priv->decrypt(ct);
    
    EXPECT_EQ(pt, value);
}
