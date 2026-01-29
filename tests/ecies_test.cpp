#include <gtest/gtest.h>
#include "brightchain/ecies.hpp"
#include "brightchain/ec_key_pair.hpp"

using namespace brightchain;

TEST(EciesTest, EncryptDecryptBasic) {
    auto keyPair = EcKeyPair::generate();
    std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    
    auto encrypted = Ecies::encryptBasic(plaintext, keyPair.publicKey());
    EXPECT_GT(encrypted.size(), plaintext.size());
    
    auto decrypted = Ecies::decrypt(encrypted, keyPair);
    EXPECT_EQ(decrypted, plaintext);
}

TEST(EciesTest, EncryptDecryptWithLength) {
    auto keyPair = EcKeyPair::generate();
    std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5};
    
    auto encrypted = Ecies::encryptWithLength(plaintext, keyPair.publicKey());
    EXPECT_GT(encrypted.size(), plaintext.size() + 8);  // +8 for length prefix
    
    auto decrypted = Ecies::decrypt(encrypted, keyPair);
    EXPECT_EQ(decrypted, plaintext);
}

TEST(EciesTest, DifferentKeysCannotDecrypt) {
    auto keyPair1 = EcKeyPair::generate();
    auto keyPair2 = EcKeyPair::generate();
    
    std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5};
    auto encrypted = Ecies::encryptBasic(plaintext, keyPair1.publicKey());
    
    EXPECT_THROW(Ecies::decrypt(encrypted, keyPair2), std::runtime_error);
}

TEST(EciesTest, CompressedPublicKey) {
    auto keyPair = EcKeyPair::generate();
    auto pubKey = keyPair.publicKey();
    
    // Verify compressed format (33 bytes, starts with 0x02 or 0x03)
    EXPECT_EQ(pubKey.size(), 33);
    EXPECT_TRUE(pubKey[0] == 0x02 || pubKey[0] == 0x03);
}

TEST(EciesTest, LargeMessage) {
    auto keyPair = EcKeyPair::generate();
    std::vector<uint8_t> plaintext(10000, 0x42);
    
    auto encrypted = Ecies::encryptBasic(plaintext, keyPair.publicKey());
    auto decrypted = Ecies::decrypt(encrypted, keyPair);
    
    EXPECT_EQ(decrypted, plaintext);
}

TEST(EciesTest, EmptyMessage) {
    auto keyPair = EcKeyPair::generate();
    std::vector<uint8_t> plaintext;
    
    auto encrypted = Ecies::encryptBasic(plaintext, keyPair.publicKey());
    auto decrypted = Ecies::decrypt(encrypted, keyPair);
    
    EXPECT_EQ(decrypted, plaintext);
}

TEST(EciesTest, HeaderFormat) {
    auto keyPair = EcKeyPair::generate();
    std::vector<uint8_t> plaintext = {1, 2, 3};
    
    auto encrypted = Ecies::encryptBasic(plaintext, keyPair.publicKey());
    
    // Check header: version(1) + cipherSuite(1) + type(1)
    EXPECT_EQ(encrypted[0], 0x01);  // Version
    EXPECT_EQ(encrypted[1], 0x01);  // Cipher suite
    EXPECT_EQ(encrypted[2], 33);    // Basic type
}

TEST(EciesTest, WithLengthHeaderFormat) {
    auto keyPair = EcKeyPair::generate();
    std::vector<uint8_t> plaintext = {1, 2, 3};
    
    auto encrypted = Ecies::encryptWithLength(plaintext, keyPair.publicKey());
    
    // Check header
    EXPECT_EQ(encrypted[0], 0x01);  // Version
    EXPECT_EQ(encrypted[1], 0x01);  // Cipher suite
    EXPECT_EQ(encrypted[2], 66);    // WithLength type
}
