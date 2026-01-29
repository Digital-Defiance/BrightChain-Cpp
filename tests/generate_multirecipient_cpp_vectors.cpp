#include "brightchain/ecies.hpp"
#include "brightchain/ec_key_pair.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>

using json = nlohmann::json;
using namespace brightchain;

std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    for (auto byte : bytes) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return ss.str();
}

std::string bytesToBase64(const std::vector<uint8_t>& bytes) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string ret;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (size_t n = 0; n < bytes.size(); n++) {
        char_array_3[i++] = bytes[n];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; i < 4; i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (int j = 0; j <= i; j++) ret += base64_chars[char_array_4[j]];
        while (i++ < 3) ret += '=';
    }
    return ret;
}

int main() {
    try {
        json output;
        output["description"] = "Multi-recipient ECIES (type 99) test vectors generated from C++";
        output["timestamp"] = "2025-01-29";
        output["note"] = "Each vector contains plaintext encrypted for N recipients. "
                         "Can be decrypted by each recipient using their private key.";

        std::vector<json> vectors;

        // Test 1: 3 recipients, small plaintext
        {
            std::vector<std::string> plaintexts = {
                "Hello, Multi-Recipient ECIES!",
                "The quick brown fox jumps over the lazy dog",
                "Multi-party computation test case 001",
            };

            for (const auto& plaintext : plaintexts) {
                std::vector<uint8_t> plaintextData(plaintext.begin(), plaintext.end());
                
                // Generate 3 key pairs
                auto kp1 = EcKeyPair::generate();
                auto kp2 = EcKeyPair::generate();
                auto kp3 = EcKeyPair::generate();

                std::vector<std::vector<uint8_t>> recipientKeys = {
                    kp1.publicKey(),
                    kp2.publicKey(),
                    kp3.publicKey(),
                };

                // Encrypt for all 3
                auto ciphertext = Ecies::encryptMultiple(plaintextData, recipientKeys);

                // Create test vector
                json vec;
                vec["description"] = plaintext + " (3 recipients)";
                vec["plaintextHex"] = bytesToHex(plaintextData);
                vec["ciphertextHex"] = bytesToHex(ciphertext);
                vec["recipientCount"] = 3;
                vec["recipientPublicKeys"] = {
                    bytesToHex(kp1.publicKey()),
                    bytesToHex(kp2.publicKey()),
                    bytesToHex(kp3.publicKey()),
                };
                vec["privateKeyForDecryption"] = bytesToHex(kp1.privateKey()); // For testing decryption
                vec["decryptAsRecipientIndex"] = 0;

                vectors.push_back(vec);
            }
        }

        // Test 2: 5 recipients, medium plaintext
        {
            std::string plaintext = "This is a test message for 5 recipients to decrypt";
            std::vector<uint8_t> plaintextData(plaintext.begin(), plaintext.end());

            std::vector<EcKeyPair> keyPairs;
            std::vector<std::vector<uint8_t>> recipientKeys;
            for (int i = 0; i < 5; i++) {
                keyPairs.push_back(EcKeyPair::generate());
                recipientKeys.push_back(keyPairs.back().publicKey());
            }

            auto ciphertext = Ecies::encryptMultiple(plaintextData, recipientKeys);

            json vec;
            vec["description"] = plaintext + " (5 recipients)";
            vec["plaintextHex"] = bytesToHex(plaintextData);
            vec["ciphertextHex"] = bytesToHex(ciphertext);
            vec["recipientCount"] = 5;

            json pubKeys = json::array();
            for (const auto& kp : keyPairs) {
                pubKeys.push_back(bytesToHex(kp.publicKey()));
            }
            vec["recipientPublicKeys"] = pubKeys;

            // Test decryption as recipient 2
            vec["privateKeyForDecryption"] = bytesToHex(keyPairs[2].privateKey());
            vec["decryptAsRecipientIndex"] = 2;

            vectors.push_back(vec);
        }

        // Test 3: 10 recipients, larger plaintext
        {
            std::string plaintext(1024, 'X'); // 1KB of X's
            std::vector<uint8_t> plaintextData(plaintext.begin(), plaintext.end());

            std::vector<EcKeyPair> keyPairs;
            std::vector<std::vector<uint8_t>> recipientKeys;
            for (int i = 0; i < 10; i++) {
                keyPairs.push_back(EcKeyPair::generate());
                recipientKeys.push_back(keyPairs.back().publicKey());
            }

            auto ciphertext = Ecies::encryptMultiple(plaintextData, recipientKeys);

            json vec;
            vec["description"] = "1KB plaintext (10 recipients)";
            vec["plaintextHex"] = bytesToHex(plaintextData).substr(0, 100) + "..."; // Truncate for readability
            vec["plaintextSize"] = plaintextData.size();
            vec["ciphertextHex"] = bytesToHex(ciphertext).substr(0, 100) + "...";
            vec["ciphertextSize"] = ciphertext.size();
            vec["recipientCount"] = 10;

            json pubKeys = json::array();
            for (const auto& kp : keyPairs) {
                pubKeys.push_back(bytesToHex(kp.publicKey()));
            }
            vec["recipientPublicKeys"] = pubKeys;

            // Test decryption as last recipient (index 9)
            vec["privateKeyForDecryption"] = bytesToHex(keyPairs[9].privateKey());
            vec["decryptAsRecipientIndex"] = 9;

            vectors.push_back(vec);
        }

        // Test 4: Empty plaintext with 3 recipients
        {
            std::vector<uint8_t> plaintextData;

            auto kp1 = EcKeyPair::generate();
            auto kp2 = EcKeyPair::generate();
            auto kp3 = EcKeyPair::generate();

            std::vector<std::vector<uint8_t>> recipientKeys = {
                kp1.publicKey(),
                kp2.publicKey(),
                kp3.publicKey(),
            };

            auto ciphertext = Ecies::encryptMultiple(plaintextData, recipientKeys);

            json vec;
            vec["description"] = "Empty plaintext (3 recipients)";
            vec["plaintextHex"] = "";
            vec["ciphertextHex"] = bytesToHex(ciphertext);
            vec["recipientCount"] = 3;
            vec["recipientPublicKeys"] = {
                bytesToHex(kp1.publicKey()),
                bytesToHex(kp2.publicKey()),
                bytesToHex(kp3.publicKey()),
            };
            vec["privateKeyForDecryption"] = bytesToHex(kp1.privateKey());
            vec["decryptAsRecipientIndex"] = 0;

            vectors.push_back(vec);
        }

        output["vectors"] = vectors;

        // Write to file
        std::ofstream outFile("test_vectors_multirecipient_cpp.json");
        outFile << std::setw(2) << output << std::endl;
        outFile.close();

        std::cout << "Generated " << vectors.size() << " multi-recipient test vectors" << std::endl;
        std::cout << "Saved to test_vectors_multirecipient_cpp.json" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
