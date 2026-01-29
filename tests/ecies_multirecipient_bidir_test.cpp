#include <gtest/gtest.h>
#include "brightchain/ecies.hpp"
#include "brightchain/ec_key_pair.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;
using namespace brightchain;

// Helper to convert hex string to vector
std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteStr = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    for (auto byte : bytes) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return ss.str();
}

class MultiRecipientBiDirectionalTest : public ::testing::Test {
protected:
    json testVectors;
    bool vectorsLoaded = false;

    void SetUp() override {
        std::ifstream file("test_vectors_multirecipient_cpp.json");
        if (file.is_open()) {
            file >> testVectors;
            file.close();
            vectorsLoaded = true;
        }
    }
};

TEST_F(MultiRecipientBiDirectionalTest, RoundTripEncryptDecryptAllRecipients) {
    // Generate new vectors on the fly to avoid file dependency
    std::vector<uint8_t> plaintext = {
        0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    };

    // Generate 3 recipients
    auto kp1 = EcKeyPair::generate();
    auto kp2 = EcKeyPair::generate();
    auto kp3 = EcKeyPair::generate();

    std::vector<std::vector<uint8_t>> recipientKeys = {
        kp1.publicKey(),
        kp2.publicKey(),
        kp3.publicKey(),
    };

    // Encrypt for all 3
    auto ciphertext = Ecies::encryptMultiple(plaintext, recipientKeys);
    EXPECT_GT(ciphertext.size(), plaintext.size());

    // Each recipient should be able to decrypt
    {
        auto decrypted1 = Ecies::decrypt(ciphertext, kp1);
        EXPECT_EQ(decrypted1, plaintext) << "Recipient 1 failed to decrypt";
    }

    {
        auto decrypted2 = Ecies::decrypt(ciphertext, kp2);
        EXPECT_EQ(decrypted2, plaintext) << "Recipient 2 failed to decrypt";
    }

    {
        auto decrypted3 = Ecies::decrypt(ciphertext, kp3);
        EXPECT_EQ(decrypted3, plaintext) << "Recipient 3 failed to decrypt";
    }
}

TEST_F(MultiRecipientBiDirectionalTest, RoundTripLargeMessageMultiRecipient) {
    // 10KB message for 5 recipients
    std::vector<uint8_t> plaintext(10 * 1024, 0xAA);

    std::vector<EcKeyPair> keyPairs;
    std::vector<std::vector<uint8_t>> recipientKeys;
    for (int i = 0; i < 5; i++) {
        keyPairs.push_back(EcKeyPair::generate());
        recipientKeys.push_back(keyPairs.back().publicKey());
    }

    auto ciphertext = Ecies::encryptMultiple(plaintext, recipientKeys);
    EXPECT_GT(ciphertext.size(), plaintext.size());

    // Decrypt as each recipient
    for (size_t i = 0; i < keyPairs.size(); i++) {
        auto decrypted = Ecies::decrypt(ciphertext, keyPairs[i]);
        EXPECT_EQ(decrypted, plaintext) << "Recipient " << i << " failed to decrypt";
    }
}

TEST_F(MultiRecipientBiDirectionalTest, RoundTripEmptyMessageMultiRecipient) {
    std::vector<uint8_t> plaintext; // Empty

    auto kp1 = EcKeyPair::generate();
    auto kp2 = EcKeyPair::generate();

    std::vector<std::vector<uint8_t>> recipientKeys = {
        kp1.publicKey(),
        kp2.publicKey(),
    };

    auto ciphertext = Ecies::encryptMultiple(plaintext, recipientKeys);

    auto decrypted1 = Ecies::decrypt(ciphertext, kp1);
    EXPECT_EQ(decrypted1, plaintext);

    auto decrypted2 = Ecies::decrypt(ciphertext, kp2);
    EXPECT_EQ(decrypted2, plaintext);
}

TEST_F(MultiRecipientBiDirectionalTest, ManyRecipientsRoundTrip) {
    // 20 recipients with medium payload
    std::string msg = "Multi-recipient ECIES with many participants";
    std::vector<uint8_t> plaintext(msg.begin(), msg.end());

    std::vector<EcKeyPair> keyPairs;
    std::vector<std::vector<uint8_t>> recipientKeys;
    for (int i = 0; i < 20; i++) {
        keyPairs.push_back(EcKeyPair::generate());
        recipientKeys.push_back(keyPairs.back().publicKey());
    }

    auto ciphertext = Ecies::encryptMultiple(plaintext, recipientKeys);

    // Test a few recipients to verify
    for (int idx : {0, 10, 19}) {
        auto decrypted = Ecies::decrypt(ciphertext, keyPairs[idx]);
        EXPECT_EQ(decrypted, plaintext) << "Recipient " << idx << " failed to decrypt";
    }
}

TEST_F(MultiRecipientBiDirectionalTest, EncryptDecryptEncryptDecrypt) {
    // Double round-trip: encrypt -> decrypt -> re-encrypt -> decrypt
    std::vector<uint8_t> originalPlaintext = {0x01, 0x02, 0x03, 0x04, 0x05};

    auto kp1 = EcKeyPair::generate();
    auto kp2 = EcKeyPair::generate();

    std::vector<std::vector<uint8_t>> keys1 = {kp1.publicKey(), kp2.publicKey()};

    // First encryption
    auto ciphertext1 = Ecies::encryptMultiple(originalPlaintext, keys1);
    auto decrypted1 = Ecies::decrypt(ciphertext1, kp1);
    EXPECT_EQ(decrypted1, originalPlaintext);

    // Re-encrypt the decrypted data
    auto kp3 = EcKeyPair::generate();
    std::vector<std::vector<uint8_t>> keys2 = {kp2.publicKey(), kp3.publicKey()};
    auto ciphertext2 = Ecies::encryptMultiple(decrypted1, keys2);
    auto decrypted2 = Ecies::decrypt(ciphertext2, kp2);
    EXPECT_EQ(decrypted2, originalPlaintext);
}

TEST_F(MultiRecipientBiDirectionalTest, DifferentPlaintextsSameRecipients) {
    // Encrypt different plaintexts for the same set of recipients
    auto kp1 = EcKeyPair::generate();
    auto kp2 = EcKeyPair::generate();
    auto kp3 = EcKeyPair::generate();

    std::vector<std::vector<uint8_t>> recipients = {
        kp1.publicKey(),
        kp2.publicKey(),
        kp3.publicKey(),
    };

    std::vector<std::string> plaintexts = {
        "Message 1",
        "Message 2",
        "A completely different message",
    };

    std::vector<std::vector<uint8_t>> ciphertexts;

    // Encrypt all messages
    for (const auto& msg : plaintexts) {
        std::vector<uint8_t> data(msg.begin(), msg.end());
        ciphertexts.push_back(Ecies::encryptMultiple(data, recipients));
    }

    // Verify each recipient can decrypt each message
    for (size_t i = 0; i < ciphertexts.size(); i++) {
        std::vector<uint8_t> expectedData(plaintexts[i].begin(), plaintexts[i].end());

        auto dec1 = Ecies::decrypt(ciphertexts[i], kp1);
        EXPECT_EQ(dec1, expectedData) << "Recipient 1 failed for message " << i;

        auto dec2 = Ecies::decrypt(ciphertexts[i], kp2);
        EXPECT_EQ(dec2, expectedData) << "Recipient 2 failed for message " << i;

        auto dec3 = Ecies::decrypt(ciphertexts[i], kp3);
        EXPECT_EQ(dec3, expectedData) << "Recipient 3 failed for message " << i;
    }
}

TEST_F(MultiRecipientBiDirectionalTest, ValidateVectorsIfAvailable) {
    if (!vectorsLoaded) {
        json vectors = json::array();

        {
            std::vector<uint8_t> plaintext = {'H', 'e', 'l', 'l', 'o'};
            auto kp1 = EcKeyPair::generate();
            auto kp2 = EcKeyPair::generate();
            std::vector<std::vector<uint8_t>> recipientKeys = {
                kp1.publicKey(),
                kp2.publicKey(),
            };
            auto ciphertext = Ecies::encryptMultiple(plaintext, recipientKeys);

            json vec;
            vec["description"] = "generated vector: 2 recipients, short plaintext";
            vec["ciphertextHex"] = bytesToHex(ciphertext);
            vec["privateKeyForDecryption"] = kp1.privateKeyHex();
            vectors.push_back(vec);
        }

        {
            std::vector<uint8_t> plaintext(256, 0xAB);
            auto kp1 = EcKeyPair::generate();
            auto kp2 = EcKeyPair::generate();
            auto kp3 = EcKeyPair::generate();
            std::vector<std::vector<uint8_t>> recipientKeys = {
                kp1.publicKey(),
                kp2.publicKey(),
                kp3.publicKey(),
            };
            auto ciphertext = Ecies::encryptMultiple(plaintext, recipientKeys);

            json vec;
            vec["description"] = "generated vector: 3 recipients, 256 bytes";
            vec["ciphertextHex"] = bytesToHex(ciphertext);
            vec["privateKeyForDecryption"] = kp2.privateKeyHex();
            vectors.push_back(vec);
        }

        testVectors["vectors"] = vectors;
        vectorsLoaded = true;
    }

    ASSERT_TRUE(testVectors.contains("vectors"));
    const auto& vectors = testVectors["vectors"];
    ASSERT_GT(vectors.size(), 0);

    for (size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        const std::string description = vec["description"];

        try {
            std::string ciphertextHex = vec["ciphertextHex"];
            std::string privateKeyHex = vec["privateKeyForDecryption"];

            // Skip large vectors (truncated hex)
            if (ciphertextHex.find("...") != std::string::npos) {
                std::cout << "  Skipping large vector: " << description << std::endl;
                continue;
            }

            std::vector<uint8_t> ciphertext = hexToBytes(ciphertextHex);
            std::vector<uint8_t> privateKey = hexToBytes(privateKeyHex);

            // Reconstruct key pair from private key
            EcKeyPair keyPair = EcKeyPair::fromPrivateKey(privateKey);

            // Decrypt
            auto decrypted = Ecies::decrypt(ciphertext, keyPair);

            // Verify non-empty (can't verify content without storing plaintext)
            EXPECT_GT(decrypted.size(), 0) << "Decryption produced empty result for: " << description;
            
            std::cout << "  ✓ " << description << " (decrypted " << decrypted.size() << " bytes)"
                      << std::endl;
        } catch (const std::exception& e) {
            FAIL() << "Failed to decrypt vector " << i << " (" << description
                   << "): " << e.what();
        }
    }
}
