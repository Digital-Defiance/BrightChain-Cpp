#include <brightchain/member.hpp>
#include <gtest/gtest.h>

using namespace brightchain;

TEST(MemberTest, Generate) {
    auto member = Member::generate(MemberType::User, "Alice", "alice@example.com");
    
    EXPECT_EQ(member.type(), MemberType::User);
    EXPECT_EQ(member.name(), "Alice");
    EXPECT_EQ(member.email(), "alice@example.com");
    EXPECT_TRUE(member.hasPrivateKey());
    EXPECT_EQ(member.publicKey().size(), 33);
    EXPECT_EQ(member.idBytes().size(), 16);
    EXPECT_EQ(member.idHex().length(), 32);
}

TEST(MemberTest, SignAndVerify) {
    auto member = Member::generate(MemberType::User, "Bob", "bob@example.com");
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto signature = member.sign(data);
    
    EXPECT_GT(signature.size(), 0);
    EXPECT_TRUE(member.verify(data, signature));
    
    // Wrong data should fail
    std::vector<uint8_t> wrongData = {1, 2, 3, 4, 6};
    EXPECT_FALSE(member.verify(wrongData, signature));
}

TEST(MemberTest, FromPublicKey) {
    auto fullMember = Member::generate(MemberType::User, "Charlie", "charlie@example.com");
    auto publicKey = fullMember.publicKey();
    
    auto publicOnlyMember = Member::fromPublicKey(
        MemberType::User,
        "Charlie Public",
        "charlie@example.com",
        publicKey
    );
    
    EXPECT_FALSE(publicOnlyMember.hasPrivateKey());
    EXPECT_EQ(publicOnlyMember.publicKey(), publicKey);
    EXPECT_EQ(publicOnlyMember.id(), fullMember.id()); // Same ID from same public key
    
    // Can verify but not sign
    std::vector<uint8_t> data = {1, 2, 3};
    auto signature = fullMember.sign(data);
    EXPECT_TRUE(publicOnlyMember.verify(data, signature));
    EXPECT_THROW(publicOnlyMember.sign(data), std::runtime_error);
}

TEST(MemberTest, FromKeys) {
    auto original = Member::generate(MemberType::Admin, "Dave", "dave@example.com");
    auto publicKey = original.publicKey();
    
    // Get private key from original (in real code, this would be securely stored)
    auto data = std::vector<uint8_t>{1, 2, 3};
    auto signature1 = original.sign(data);
    
    // Can't directly get private key, so we'll test with a known key
    std::vector<uint8_t> testPrivateKey(32, 0x42);
    auto testKeyPair = EcKeyPair::fromPrivateKey(testPrivateKey);
    auto testPublicKey = testKeyPair.publicKey();
    
    auto restored = Member::fromKeys(
        MemberType::Admin,
        "Dave Restored",
        "dave@example.com",
        testPublicKey,
        testPrivateKey
    );
    
    EXPECT_TRUE(restored.hasPrivateKey());
    EXPECT_EQ(restored.publicKey(), testPublicKey);
    
    // Should be able to sign
    auto signature2 = restored.sign(data);
    EXPECT_TRUE(restored.verify(data, signature2));
}

TEST(MemberTest, DeterministicId) {
    // Same public key should always generate same ID
    std::vector<uint8_t> testPrivateKey(32, 0x55);
    auto keyPair = EcKeyPair::fromPrivateKey(testPrivateKey);
    auto publicKey = keyPair.publicKey();
    
    auto member1 = Member::fromPublicKey(MemberType::User, "Test1", "test1@example.com", publicKey);
    auto member2 = Member::fromPublicKey(MemberType::User, "Test2", "test2@example.com", publicKey);
    
    EXPECT_EQ(member1.id(), member2.id());
    EXPECT_EQ(member1.idHex(), member2.idHex());
}

TEST(MemberTest, StaticVerify) {
    auto member = Member::generate(MemberType::User, "Eve", "eve@example.com");
    std::vector<uint8_t> data = {0xde, 0xad, 0xbe, 0xef};
    auto signature = member.sign(data);
    auto publicKey = member.publicKey();
    
    // Static verification
    EXPECT_TRUE(Member::verifySignature(data, signature, publicKey));
    
    // Wrong data
    std::vector<uint8_t> wrongData = {0xde, 0xad, 0xbe, 0xe0};
    EXPECT_FALSE(Member::verifySignature(wrongData, signature, publicKey));
}

TEST(MemberTest, MemberTypes) {
    auto admin = Member::generate(MemberType::Admin, "Admin", "admin@example.com");
    auto system = Member::generate(MemberType::System, "System", "system@example.com");
    auto user = Member::generate(MemberType::User, "User", "user@example.com");
    auto anon = Member::generate(MemberType::Anonymous, "Anon", "anon@example.com");
    
    EXPECT_EQ(admin.type(), MemberType::Admin);
    EXPECT_EQ(system.type(), MemberType::System);
    EXPECT_EQ(user.type(), MemberType::User);
    EXPECT_EQ(anon.type(), MemberType::Anonymous);
}

TEST(MemberTest, InvalidPublicKey) {
    std::vector<uint8_t> invalidKey(32, 0xFF); // Wrong size
    
    EXPECT_THROW(
        Member::fromPublicKey(MemberType::User, "Invalid", "invalid@example.com", invalidKey),
        std::invalid_argument
    );
}

TEST(MemberTest, CrossMemberVerification) {
    auto alice = Member::generate(MemberType::User, "Alice", "alice@example.com");
    auto bob = Member::generate(MemberType::User, "Bob", "bob@example.com");
    
    std::vector<uint8_t> message = {0x48, 0x65, 0x6c, 0x6c, 0x6f}; // "Hello"
    auto aliceSignature = alice.sign(message);
    
    // Bob can verify Alice's signature using her public key
    EXPECT_TRUE(Member::verifySignature(message, aliceSignature, alice.publicKey()));
    
    // Bob's public key won't verify Alice's signature
    EXPECT_FALSE(Member::verifySignature(message, aliceSignature, bob.publicKey()));
}

TEST(MemberTest, GenerateMnemonic) {
    auto mnemonic = Member::generateMnemonic();
    
    // Should be 12 words (11 spaces between them)
    int spaceCount = 0;
    for (char c : mnemonic) {
        if (c == ' ') spaceCount++;
    }
    EXPECT_EQ(spaceCount, 11);
    
    // Should be valid
    EXPECT_TRUE(Member::validateMnemonic(mnemonic));
}

TEST(MemberTest, ValidateMnemonic) {
    // Valid 12-word mnemonic
    std::string validMnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    EXPECT_TRUE(Member::validateMnemonic(validMnemonic));
    
    // Invalid mnemonic
    std::string invalidMnemonic = "invalid mnemonic phrase that should not work";
    EXPECT_FALSE(Member::validateMnemonic(invalidMnemonic));
    
    // Empty mnemonic
    EXPECT_FALSE(Member::validateMnemonic(""));
}

TEST(MemberTest, FromMnemonic) {
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    
    auto member = Member::fromMnemonic(mnemonic, MemberType::User, "Test", "test@example.com");
    
    EXPECT_TRUE(member.hasPrivateKey());
    EXPECT_EQ(member.publicKey().size(), 33);
    EXPECT_EQ(member.type(), MemberType::User);
    
    // Should be able to sign
    std::vector<uint8_t> data = {1, 2, 3};
    auto signature = member.sign(data);
    EXPECT_TRUE(member.verify(data, signature));
}

TEST(MemberTest, MnemonicDeterministic) {
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    
    // Create two members from same mnemonic
    auto member1 = Member::fromMnemonic(mnemonic, MemberType::User, "Test1", "test1@example.com");
    auto member2 = Member::fromMnemonic(mnemonic, MemberType::User, "Test2", "test2@example.com");
    
    // Should have same keys and ID
    EXPECT_EQ(member1.publicKey(), member2.publicKey());
    EXPECT_EQ(member1.id(), member2.id());
    
    // Should produce same signatures
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto sig1 = member1.sign(data);
    auto sig2 = member2.sign(data);
    
    // Both should verify with either member
    EXPECT_TRUE(member1.verify(data, sig1));
    EXPECT_TRUE(member1.verify(data, sig2));
    EXPECT_TRUE(member2.verify(data, sig1));
    EXPECT_TRUE(member2.verify(data, sig2));
}

TEST(MemberTest, MnemonicUniqueness) {
    // Different mnemonics should produce different keys
    auto mnemonic1 = Member::generateMnemonic();
    auto mnemonic2 = Member::generateMnemonic();
    
    EXPECT_NE(mnemonic1, mnemonic2);
    
    auto member1 = Member::fromMnemonic(mnemonic1, MemberType::User, "Test1", "test1@example.com");
    auto member2 = Member::fromMnemonic(mnemonic2, MemberType::User, "Test2", "test2@example.com");
    
    EXPECT_NE(member1.publicKey(), member2.publicKey());
    EXPECT_NE(member1.id(), member2.id());
}
