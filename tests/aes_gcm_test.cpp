#include <gtest/gtest.h>
#include "brightchain/aes_gcm.hpp"

using namespace brightchain;

TEST(AesGcmTest, GenerateKey) {
    auto key = AesGcm::generateKey();
    EXPECT_EQ(key.size(), AesGcm::KEY_SIZE);
}

TEST(AesGcmTest, GenerateIV) {
    auto iv = AesGcm::generateIV();
    EXPECT_EQ(iv.size(), AesGcm::IV_SIZE);
}

TEST(AesGcmTest, EncryptDecrypt) {
    auto key = AesGcm::generateKey();
    auto iv = AesGcm::generateIV();
    
    std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5, 6, 7, 8};
    AesGcm::Tag tag;
    
    auto ciphertext = AesGcm::encrypt(plaintext, key, iv, tag);
    EXPECT_FALSE(ciphertext.empty());
    EXPECT_NE(ciphertext, plaintext);
    
    auto decrypted = AesGcm::decrypt(ciphertext, key, iv, tag);
    EXPECT_EQ(decrypted, plaintext);
}

TEST(AesGcmTest, DifferentKeys) {
    auto key1 = AesGcm::generateKey();
    auto key2 = AesGcm::generateKey();
    auto iv = AesGcm::generateIV();
    
    std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5};
    AesGcm::Tag tag;
    
    auto ciphertext = AesGcm::encrypt(plaintext, key1, iv, tag);
    
    EXPECT_THROW(AesGcm::decrypt(ciphertext, key2, iv, tag), std::runtime_error);
}

TEST(AesGcmTest, TamperedCiphertext) {
    auto key = AesGcm::generateKey();
    auto iv = AesGcm::generateIV();
    
    std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5};
    AesGcm::Tag tag;
    
    auto ciphertext = AesGcm::encrypt(plaintext, key, iv, tag);
    ciphertext[0] ^= 1;  // Tamper with ciphertext
    
    EXPECT_THROW(AesGcm::decrypt(ciphertext, key, iv, tag), std::runtime_error);
}
