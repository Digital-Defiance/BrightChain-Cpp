#include <gtest/gtest.h>
#include "brightchain/ec_key_pair.hpp"

using namespace brightchain;

TEST(EcKeyPairTest, Generate) {
    auto keyPair = EcKeyPair::generate();
    
    auto pubKey = keyPair.publicKey();
    auto privKey = keyPair.privateKey();
    
    EXPECT_EQ(pubKey.size(), 33);  // Compressed
    EXPECT_EQ(privKey.size(), 32);
}

TEST(EcKeyPairTest, FromPrivateKey) {
    auto keyPair1 = EcKeyPair::generate();
    auto privKey = keyPair1.privateKey();
    
    auto keyPair2 = EcKeyPair::fromPrivateKey(privKey);
    
    EXPECT_EQ(keyPair1.publicKey(), keyPair2.publicKey());
    EXPECT_EQ(keyPair1.privateKey(), keyPair2.privateKey());
}

TEST(EcKeyPairTest, HexConversion) {
    auto keyPair = EcKeyPair::generate();
    
    auto privHex = keyPair.privateKeyHex();
    EXPECT_EQ(privHex.length(), 64);
    
    auto keyPair2 = EcKeyPair::fromPrivateKeyHex(privHex);
    EXPECT_EQ(keyPair.privateKey(), keyPair2.privateKey());
}

TEST(EcKeyPairTest, SignVerify) {
    auto keyPair = EcKeyPair::generate();
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    
    auto signature = keyPair.sign(data);
    EXPECT_FALSE(signature.empty());
    
    EXPECT_TRUE(EcKeyPair::verify(data, signature, keyPair.publicKey()));
}

TEST(EcKeyPairTest, VerifyInvalidSignature) {
    auto keyPair = EcKeyPair::generate();
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    
    auto signature = keyPair.sign(data);
    signature[0] ^= 1;  // Tamper
    
    EXPECT_FALSE(EcKeyPair::verify(data, signature, keyPair.publicKey()));
}
