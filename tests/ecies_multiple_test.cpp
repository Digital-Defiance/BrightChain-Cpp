#include <gtest/gtest.h>
#include "brightchain/ecies.hpp"
#include "brightchain/ec_key_pair.hpp"

using namespace brightchain;

TEST(EciesMultipleTest, EncryptDecryptMultiple) {
    // Create multiple recipients
    auto keyPair1 = EcKeyPair::generate();
    auto keyPair2 = EcKeyPair::generate();
    auto keyPair3 = EcKeyPair::generate();

    std::vector<std::vector<uint8_t>> recipientKeys = {
        keyPair1.publicKey(),
        keyPair2.publicKey(),
        keyPair3.publicKey()
    };

    std::vector<uint8_t> plaintext = {0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe};

    // Encrypt for multiple recipients
    auto encrypted = Ecies::encryptMultiple(plaintext, recipientKeys);
    EXPECT_GT(encrypted.size(), plaintext.size());

    // Each recipient should be able to decrypt
    {
        auto decrypted1 = Ecies::decrypt(encrypted, keyPair1);
        EXPECT_EQ(decrypted1, plaintext);
    }

    {
        auto decrypted2 = Ecies::decrypt(encrypted, keyPair2);
        EXPECT_EQ(decrypted2, plaintext);
    }

    {
        auto decrypted3 = Ecies::decrypt(encrypted, keyPair3);
        EXPECT_EQ(decrypted3, plaintext);
    }
}

TEST(EciesMultipleTest, MultipleCannotDecryptWithWrongKey) {
    auto keyPair1 = EcKeyPair::generate();
    auto keyPair2 = EcKeyPair::generate();
    auto keyPairWrong = EcKeyPair::generate();

    std::vector<std::vector<uint8_t>> recipientKeys = {
        keyPair1.publicKey(),
        keyPair2.publicKey()
    };

    std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5};
    auto encrypted = Ecies::encryptMultiple(plaintext, recipientKeys);

    // Wrong key should not be able to decrypt
    EXPECT_THROW(Ecies::decrypt(encrypted, keyPairWrong), std::runtime_error);
}

TEST(EciesMultipleTest, MultipleEmptyMessage) {
    auto keyPair1 = EcKeyPair::generate();
    auto keyPair2 = EcKeyPair::generate();

    std::vector<std::vector<uint8_t>> recipientKeys = {
        keyPair1.publicKey(),
        keyPair2.publicKey()
    };

    std::vector<uint8_t> plaintext;
    auto encrypted = Ecies::encryptMultiple(plaintext, recipientKeys);

    auto decrypted1 = Ecies::decrypt(encrypted, keyPair1);
    EXPECT_EQ(decrypted1, plaintext);

    auto decrypted2 = Ecies::decrypt(encrypted, keyPair2);
    EXPECT_EQ(decrypted2, plaintext);
}

TEST(EciesMultipleTest, MultipleLargeMessage) {
    auto keyPair1 = EcKeyPair::generate();
    auto keyPair2 = EcKeyPair::generate();
    auto keyPair3 = EcKeyPair::generate();
    auto keyPair4 = EcKeyPair::generate();

    std::vector<std::vector<uint8_t>> recipientKeys = {
        keyPair1.publicKey(),
        keyPair2.publicKey(),
        keyPair3.publicKey(),
        keyPair4.publicKey()
    };

    std::vector<uint8_t> plaintext(10000, 0x42);
    auto encrypted = Ecies::encryptMultiple(plaintext, recipientKeys);

    // All recipients should decrypt successfully
    auto decrypted1 = Ecies::decrypt(encrypted, keyPair1);
    EXPECT_EQ(decrypted1, plaintext);

    auto decrypted4 = Ecies::decrypt(encrypted, keyPair4);
    EXPECT_EQ(decrypted4, plaintext);
}

TEST(EciesMultipleTest, MultipleWithManyRecipients) {
    // Test with 10 recipients
    std::vector<EcKeyPair> keyPairs;
    std::vector<std::vector<uint8_t>> recipientKeys;
    
    for (int i = 0; i < 10; ++i) {
        auto kp = EcKeyPair::generate();
        keyPairs.push_back(std::move(kp));
        recipientKeys.push_back(keyPairs.back().publicKey());
    }

    std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03};
    auto encrypted = Ecies::encryptMultiple(plaintext, recipientKeys);

    // Test first, middle, and last recipient
    {
        auto decrypted = Ecies::decrypt(encrypted, keyPairs[0]);
        EXPECT_EQ(decrypted, plaintext);
    }

    {
        auto decrypted = Ecies::decrypt(encrypted, keyPairs[5]);
        EXPECT_EQ(decrypted, plaintext);
    }

    {
        auto decrypted = Ecies::decrypt(encrypted, keyPairs[9]);
        EXPECT_EQ(decrypted, plaintext);
    }
}

TEST(EciesMultipleTest, SingleRecipientMultiple) {
    auto keyPair = EcKeyPair::generate();
    std::vector<std::vector<uint8_t>> recipientKeys = {keyPair.publicKey()};

    std::vector<uint8_t> plaintext = {0xaa, 0xbb, 0xcc, 0xdd};
    auto encrypted = Ecies::encryptMultiple(plaintext, recipientKeys);

    auto decrypted = Ecies::decrypt(encrypted, keyPair);
    EXPECT_EQ(decrypted, plaintext);
}
