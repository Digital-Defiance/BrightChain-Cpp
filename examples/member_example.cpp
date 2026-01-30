#include <brightchain/member.hpp>
#include <iostream>
#include <iomanip>

using namespace brightchain;

int main() {
    std::cout << "BrightChain Member Example\n\n";

    // 1. Generate and use mnemonic
    std::cout << "1. Generating 12-word mnemonic...\n";
    auto mnemonic = Member::generateMnemonic();
    std::cout << "   Mnemonic: " << mnemonic << "\n";
    std::cout << "   Valid: " << (Member::validateMnemonic(mnemonic) ? "yes" : "no") << "\n\n";

    // 2. Create member from mnemonic
    std::cout << "2. Creating member from mnemonic...\n";
    auto alice = Member::fromMnemonic(mnemonic, MemberType::User, "Alice", "alice@example.com");
    std::cout << "   Alice ID: " << alice.idHex() << "\n\n";

    // 3. Recreate same member from same mnemonic
    std::cout << "3. Recreating member from same mnemonic...\n";
    auto aliceAgain = Member::fromMnemonic(mnemonic, MemberType::User, "Alice", "alice@example.com");
    std::cout << "   Alice ID (again): " << aliceAgain.idHex() << "\n";
    std::cout << "   Same ID: " << (alice.id() == aliceAgain.id() ? "yes" : "no") << "\n\n";

    // 4. Create other members
    std::cout << "4. Creating other members...\n";
    auto bob = Member::generate(MemberType::User, "Bob", "bob@example.com");
    auto admin = Member::generate(MemberType::Admin, "Admin", "admin@example.com");

    std::cout << "   Bob ID:   " << bob.idHex() << "\n";
    std::cout << "   Admin ID: " << admin.idHex() << "\n\n";

    // 5. Sign and verify
    std::cout << "5. Alice signs a message...\n";
    std::vector<uint8_t> message = {0x48, 0x65, 0x6c, 0x6c, 0x6f}; // "Hello"
    auto signature = alice.sign(message);
    
    std::cout << "   Message: Hello (5 bytes)\n";
    std::cout << "   Signature: " << signature.size() << " bytes\n";
    std::cout << "   Signature (hex): ";
    for (size_t i = 0; i < std::min(size_t(16), signature.size()); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(signature[i]);
    }
    std::cout << "...\n\n";

    // 6. Verify signature
    std::cout << "6. Verifying signatures...\n";
    bool aliceVerifies = alice.verify(message, signature);
    bool bobVerifies = Member::verifySignature(message, signature, alice.publicKey());
    bool wrongKey = Member::verifySignature(message, signature, bob.publicKey());
    
    std::cout << "   Alice verifies her own signature: " << (aliceVerifies ? "✓" : "✗") << "\n";
    std::cout << "   Bob verifies Alice's signature:   " << (bobVerifies ? "✓" : "✗") << "\n";
    std::cout << "   Using Bob's key (should fail):    " << (wrongKey ? "✓" : "✗") << "\n\n";

    // 7. Public-only member
    std::cout << "7. Creating public-only member...\n";
    auto alicePublic = Member::fromPublicKey(
        MemberType::User,
        "Alice (public)",
        "alice@example.com",
        alice.publicKey()
    );
    
    std::cout << "   Has private key: " << (alicePublic.hasPrivateKey() ? "yes" : "no") << "\n";
    std::cout << "   Can verify: " << (alicePublic.verify(message, signature) ? "yes" : "no") << "\n";
    std::cout << "   Same ID as Alice: " << (alicePublic.id() == alice.id() ? "yes" : "no") << "\n\n";

    // 8. Member types
    std::cout << "8. Member types:\n";
    std::cout << "   Alice: " << static_cast<int>(alice.type()) << " (User)\n";
    std::cout << "   Bob:   " << static_cast<int>(bob.type()) << " (User)\n";
    std::cout << "   Admin: " << static_cast<int>(admin.type()) << " (Admin)\n\n";

    // 9. Cross-member communication
    std::cout << "9. Cross-member communication:\n";
    std::vector<uint8_t> bobMessage = {0x48, 0x69}; // "Hi"
    auto bobSignature = bob.sign(bobMessage);
    
    std::cout << "   Bob signs: Hi\n";
    std::cout << "   Alice verifies Bob's signature: " 
              << (Member::verifySignature(bobMessage, bobSignature, bob.publicKey()) ? "✓" : "✗") 
              << "\n";
    std::cout << "   Admin verifies Bob's signature: "
              << (Member::verifySignature(bobMessage, bobSignature, bob.publicKey()) ? "✓" : "✗")
              << "\n\n";

    std::cout << "All operations completed successfully!\n";
    
    return 0;
}
