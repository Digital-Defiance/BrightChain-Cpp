#include <iostream>
#include "brightchain/ecies.hpp"
#include "brightchain/ec_key_pair.hpp"

int main() {
    try {
        std::cout << "BrightChain ECIES Encryption Example\n";
        std::cout << "====================================\n\n";
        
        // Generate key pair
        std::cout << "Generating key pair...\n";
        auto keyPair = brightchain::EcKeyPair::generate();
        
        std::cout << "Public key (hex): " << keyPair.publicKeyHex() << "\n";
        std::cout << "Public key size: " << keyPair.publicKey().size() << " bytes (compressed)\n\n";
        
        // Create message
        std::string message = "Hello, BrightChain! This is a secret message.";
        std::vector<uint8_t> plaintext(message.begin(), message.end());
        
        std::cout << "Original message: " << message << "\n";
        std::cout << "Message size: " << plaintext.size() << " bytes\n\n";
        
        // Encrypt with Basic mode
        std::cout << "Encrypting with Basic mode...\n";
        auto encryptedBasic = brightchain::Ecies::encryptBasic(plaintext, keyPair.publicKey());
        std::cout << "Encrypted size: " << encryptedBasic.size() << " bytes\n";
        std::cout << "Overhead: " << (encryptedBasic.size() - plaintext.size()) << " bytes\n\n";
        
        // Encrypt with WithLength mode
        std::cout << "Encrypting with WithLength mode...\n";
        auto encryptedWithLength = brightchain::Ecies::encryptWithLength(plaintext, keyPair.publicKey());
        std::cout << "Encrypted size: " << encryptedWithLength.size() << " bytes\n";
        std::cout << "Overhead: " << (encryptedWithLength.size() - plaintext.size()) << " bytes\n\n";
        
        // Decrypt Basic
        std::cout << "Decrypting Basic mode...\n";
        auto decryptedBasic = brightchain::Ecies::decrypt(encryptedBasic, keyPair);
        std::string recoveredBasic(decryptedBasic.begin(), decryptedBasic.end());
        std::cout << "Decrypted: " << recoveredBasic << "\n";
        std::cout << "✓ Basic mode successful!\n\n";
        
        // Decrypt WithLength
        std::cout << "Decrypting WithLength mode...\n";
        auto decryptedWithLength = brightchain::Ecies::decrypt(encryptedWithLength, keyPair);
        std::string recoveredWithLength(decryptedWithLength.begin(), decryptedWithLength.end());
        std::cout << "Decrypted: " << recoveredWithLength << "\n";
        std::cout << "✓ WithLength mode successful!\n\n";
        
        // Verify
        if (recoveredBasic == message && recoveredWithLength == message) {
            std::cout << "✓ All encryption modes verified!\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
