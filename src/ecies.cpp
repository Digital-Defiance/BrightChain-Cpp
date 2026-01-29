#include "brightchain/ecies.hpp"
#include "brightchain/aes_gcm.hpp"
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/kdf.h>
#include <openssl/evp.h>
#include <stdexcept>
#include <cstring>
#include <string>

namespace brightchain {

// Constants matching TypeScript implementation
static constexpr uint8_t VERSION = 0x01;
static constexpr uint8_t CIPHER_SUITE = 0x01;  // Secp256k1_Aes256Gcm_Sha256
static constexpr size_t EPHEMERAL_KEY_SIZE = 33;
static constexpr size_t HEADER_SIZE = 3;  // version + cipherSuite + type

std::vector<uint8_t> Ecies::encryptBasic(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& recipientPublicKey
) {
    return encryptInternal(plaintext, recipientPublicKey, EciesEncryptionType::Basic);
}

std::vector<uint8_t> Ecies::encryptWithLength(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& recipientPublicKey
) {
    return encryptInternal(plaintext, recipientPublicKey, EciesEncryptionType::WithLength);
}

std::vector<uint8_t> Ecies::encryptMultiple(
    const std::vector<uint8_t>& plaintext,
    const std::vector<std::vector<uint8_t>>& recipientPublicKeys
) {
    if (recipientPublicKeys.empty()) {
        throw std::runtime_error("Must have at least one recipient");
    }
    if (recipientPublicKeys.size() > 0xFFFFFFFFU) {
        throw std::runtime_error("Too many recipients");
    }

    // Generate ephemeral key pair
    auto ephemeralKeyPair = EcKeyPair::generate();
    auto ephemeralPublicKey = ephemeralKeyPair.publicKey();
    auto ephemeralPrivateKey = ephemeralKeyPair.privateKey();

    // Generate random symmetric key
    auto symmetricKey = AesGcm::generateKey();
    
    // Generate random IV
    auto iv = AesGcm::generateIV();

    // Construct AAD: version + cipherSuite + type + ephemeralPubKey
    std::vector<uint8_t> aad;
    aad.push_back(VERSION);
    aad.push_back(CIPHER_SUITE);
    aad.push_back(static_cast<uint8_t>(EciesEncryptionType::Multiple));
    aad.insert(aad.end(), ephemeralPublicKey.begin(), ephemeralPublicKey.end());

    // Encrypt plaintext with symmetric key
    AesGcm::Tag tag;
    auto ciphertext = AesGcm::encrypt(plaintext, symmetricKey, iv, tag, aad);

    // Encrypt symmetric key for each recipient
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> encryptedKeys;
    for (uint32_t i = 0; i < recipientPublicKeys.size(); ++i) {
        auto encryptedKey = encryptSymmetricKey(
            symmetricKey,
            recipientPublicKeys[i],
            ephemeralPrivateKey,
            ephemeralPublicKey,
            iv
        );
        encryptedKeys.push_back({i, encryptedKey});
    }

    // Assemble result: header + ephemeralPubKey + IV + recipientCount + encrypted keys + tag + ciphertext
    std::vector<uint8_t> result;
    
    // Header
    result.push_back(VERSION);
    result.push_back(CIPHER_SUITE);
    result.push_back(static_cast<uint8_t>(EciesEncryptionType::Multiple));
    
    // Ephemeral public key
    result.insert(result.end(), ephemeralPublicKey.begin(), ephemeralPublicKey.end());
    
    // IV
    result.insert(result.end(), iv.begin(), iv.end());
    
    // Recipient count (4 bytes, big-endian)
    uint32_t count = recipientPublicKeys.size();
    result.push_back(static_cast<uint8_t>((count >> 24) & 0xFF));
    result.push_back(static_cast<uint8_t>((count >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>(count & 0xFF));
    
    // Encrypted keys for each recipient
    for (const auto& [index, encryptedKey] : encryptedKeys) {
        // Index (4 bytes, big-endian)
        result.push_back(static_cast<uint8_t>((index >> 24) & 0xFF));
        result.push_back(static_cast<uint8_t>((index >> 16) & 0xFF));
        result.push_back(static_cast<uint8_t>((index >> 8) & 0xFF));
        result.push_back(static_cast<uint8_t>(index & 0xFF));
        
        // Encrypted key length (2 bytes, big-endian)
        uint16_t keyLen = encryptedKey.size();
        result.push_back(static_cast<uint8_t>((keyLen >> 8) & 0xFF));
        result.push_back(static_cast<uint8_t>(keyLen & 0xFF));
        
        // Encrypted key data
        result.insert(result.end(), encryptedKey.begin(), encryptedKey.end());
    }
    
    // Auth tag
    result.insert(result.end(), tag.begin(), tag.end());
    
    // Ciphertext
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());
    
    return result;
}

std::vector<uint8_t> Ecies::encryptSymmetricKey(
    const AesGcm::Key& symmetricKey,
    const std::vector<uint8_t>& recipientPublicKey,
    const std::vector<uint8_t>& ephemeralPrivateKey,
    const std::vector<uint8_t>& ephemeralPublicKey,
    const AesGcm::IV& iv
) {
    // Compute ECDH shared secret with recipient's public key
    EC_KEY* recipientKey = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!recipientKey) {
        throw std::runtime_error("Failed to create recipient EC key");
    }

    const EC_GROUP* group = EC_KEY_get0_group(recipientKey);
    EC_POINT* recipientPoint = EC_POINT_new(group);
    if (!recipientPoint ||
        EC_POINT_oct2point(group, recipientPoint, recipientPublicKey.data(),
                          recipientPublicKey.size(), nullptr) != 1) {
        EC_POINT_free(recipientPoint);
        EC_KEY_free(recipientKey);
        throw std::runtime_error("Invalid recipient public key");
    }

    size_t secretLen = 32;
    std::vector<uint8_t> sharedSecret(secretLen);

    BIGNUM* priv_bn = BN_bin2bn(ephemeralPrivateKey.data(), ephemeralPrivateKey.size(), nullptr);
    EC_KEY_set_private_key(recipientKey, priv_bn);

    if (ECDH_compute_key(sharedSecret.data(), secretLen, recipientPoint,
                         recipientKey, nullptr) != static_cast<int>(secretLen)) {
        BN_free(priv_bn);
        EC_POINT_free(recipientPoint);
        EC_KEY_free(recipientKey);
        throw std::runtime_error("Failed to compute shared secret");
    }

    BN_free(priv_bn);
    EC_POINT_free(recipientPoint);
    EC_KEY_free(recipientKey);

    // Derive encryption key using HKDF-SHA256
    AesGcm::Key encKey;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!pctx ||
        EVP_PKEY_derive_init(pctx) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(pctx, sharedSecret.data(), sharedSecret.size()) <= 0 ||
        EVP_PKEY_CTX_add1_hkdf_info(pctx,
            reinterpret_cast<const unsigned char*>("ecies-v2-key-encryption"), 23) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to setup HKDF for key encryption");
    }

    size_t keyLen = AesGcm::KEY_SIZE;
    if (EVP_PKEY_derive(pctx, encKey.data(), &keyLen) <= 0 || keyLen != AesGcm::KEY_SIZE) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to derive key encryption key");
    }
    EVP_PKEY_CTX_free(pctx);

    // Encrypt symmetric key with derived key
    AesGcm::Tag keyTag;
    std::vector<uint8_t> aad;
    aad.insert(aad.end(), ephemeralPublicKey.begin(), ephemeralPublicKey.end());
    aad.insert(aad.end(), iv.begin(), iv.end());

    // Convert the symmetric key (array) to vector for encryption
    std::vector<uint8_t> keyToEncrypt(symmetricKey.begin(), symmetricKey.end());
    auto encryptedKey = AesGcm::encrypt(keyToEncrypt, encKey, iv, keyTag, aad);
    
    // Append the authentication tag to the encrypted data
    encryptedKey.insert(encryptedKey.end(), keyTag.begin(), keyTag.end());
    return encryptedKey;
}

AesGcm::Key Ecies::decryptSymmetricKey(
    const std::vector<uint8_t>& encryptedSymmetricKey,
    const std::vector<uint8_t>& ephemeralPublicKey,
    const std::vector<uint8_t>& privateKey,
    const AesGcm::IV& iv
) {
    // Compute ECDH shared secret with ephemeral public key
    EC_KEY* ephemeralKey = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!ephemeralKey) {
        throw std::runtime_error("Failed to create ephemeral EC key");
    }

    const EC_GROUP* group = EC_KEY_get0_group(ephemeralKey);
    EC_POINT* ephemeralPoint = EC_POINT_new(group);
    if (!ephemeralPoint ||
        EC_POINT_oct2point(group, ephemeralPoint, ephemeralPublicKey.data(),
                          ephemeralPublicKey.size(), nullptr) != 1) {
        EC_POINT_free(ephemeralPoint);
        EC_KEY_free(ephemeralKey);
        throw std::runtime_error("Invalid ephemeral public key");
    }

    size_t secretLen = 32;
    std::vector<uint8_t> sharedSecret(secretLen);

    BIGNUM* priv_bn = BN_bin2bn(privateKey.data(), privateKey.size(), nullptr);
    EC_KEY_set_private_key(ephemeralKey, priv_bn);

    if (ECDH_compute_key(sharedSecret.data(), secretLen, ephemeralPoint,
                         ephemeralKey, nullptr) != static_cast<int>(secretLen)) {
        BN_free(priv_bn);
        EC_POINT_free(ephemeralPoint);
        EC_KEY_free(ephemeralKey);
        throw std::runtime_error("Failed to compute shared secret");
    }

    BN_free(priv_bn);
    EC_POINT_free(ephemeralPoint);
    EC_KEY_free(ephemeralKey);

    // Derive decryption key using HKDF-SHA256
    AesGcm::Key decKey;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!pctx ||
        EVP_PKEY_derive_init(pctx) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(pctx, sharedSecret.data(), sharedSecret.size()) <= 0 ||
        EVP_PKEY_CTX_add1_hkdf_info(pctx,
            reinterpret_cast<const unsigned char*>("ecies-v2-key-encryption"), 23) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to setup HKDF for key decryption");
    }

    size_t keyLen = AesGcm::KEY_SIZE;
    if (EVP_PKEY_derive(pctx, decKey.data(), &keyLen) <= 0 || keyLen != AesGcm::KEY_SIZE) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to derive key decryption key");
    }
    EVP_PKEY_CTX_free(pctx);

    // Decrypt symmetric key - note we can't verify tag without it, so we'll assume it's valid
    AesGcm::Tag keyTag;
    std::vector<uint8_t> aad;
    aad.insert(aad.end(), ephemeralPublicKey.begin(), ephemeralPublicKey.end());
    aad.insert(aad.end(), iv.begin(), iv.end());

    // For decryption, we need the tag which is appended to encrypted data
    if (encryptedSymmetricKey.size() < AesGcm::TAG_SIZE) {
        throw std::runtime_error("Encrypted key too short");
    }

    std::vector<uint8_t> encrypted(encryptedSymmetricKey.begin(),
                                   encryptedSymmetricKey.end() - AesGcm::TAG_SIZE);
    std::memcpy(keyTag.data(), encryptedSymmetricKey.data() + encrypted.size(), AesGcm::TAG_SIZE);

    auto decryptedKey = AesGcm::decrypt(encrypted, decKey, iv, keyTag, aad);
    if (decryptedKey.size() != AesGcm::KEY_SIZE) {
        throw std::runtime_error("Decrypted key has incorrect size");
    }
    
    AesGcm::Key result;
    std::memcpy(result.data(), decryptedKey.data(), AesGcm::KEY_SIZE);
    return result;
}

std::vector<uint8_t> Ecies::encryptInternal(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& recipientPublicKey,
    EciesEncryptionType type
) {
    // Generate ephemeral key pair
    auto ephemeralKeyPair = EcKeyPair::generate();
    auto ephemeralPublicKey = ephemeralKeyPair.publicKey();
    auto ephemeralPrivateKey = ephemeralKeyPair.privateKey();

    // Compute ECDH shared secret
    EC_KEY* recipientKey = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!recipientKey) {
        throw std::runtime_error("Failed to create recipient EC key");
    }

    const EC_GROUP* group = EC_KEY_get0_group(recipientKey);
    EC_POINT* recipientPoint = EC_POINT_new(group);
    if (!recipientPoint || 
        EC_POINT_oct2point(group, recipientPoint, recipientPublicKey.data(), 
                          recipientPublicKey.size(), nullptr) != 1) {
        EC_POINT_free(recipientPoint);
        EC_KEY_free(recipientKey);
        throw std::runtime_error("Invalid recipient public key");
    }

    size_t secretLen = 32;
    std::vector<uint8_t> sharedSecret(secretLen);
    
    BIGNUM* priv_bn = BN_bin2bn(ephemeralPrivateKey.data(), ephemeralPrivateKey.size(), nullptr);
    EC_KEY_set_private_key(recipientKey, priv_bn);
    
    if (ECDH_compute_key(sharedSecret.data(), secretLen, recipientPoint, 
                         recipientKey, nullptr) != static_cast<int>(secretLen)) {
        BN_free(priv_bn);
        EC_POINT_free(recipientPoint);
        EC_KEY_free(recipientKey);
        throw std::runtime_error("Failed to compute shared secret");
    }

    BN_free(priv_bn);
    EC_POINT_free(recipientPoint);
    EC_KEY_free(recipientKey);

    // Derive AES key using HKDF-SHA256
    AesGcm::Key aesKey;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!pctx ||
        EVP_PKEY_derive_init(pctx) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(pctx, sharedSecret.data(), sharedSecret.size()) <= 0 ||
        EVP_PKEY_CTX_add1_hkdf_info(pctx, 
            reinterpret_cast<const unsigned char*>("ecies-v2-key-derivation"), 23) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to setup HKDF");
    }

    size_t keyLen = AesGcm::KEY_SIZE;
    if (EVP_PKEY_derive(pctx, aesKey.data(), &keyLen) <= 0 || keyLen != AesGcm::KEY_SIZE) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to derive AES key");
    }
    EVP_PKEY_CTX_free(pctx);

    // Generate random IV
    auto iv = AesGcm::generateIV();

    // Construct AAD: version + cipherSuite + type + ephemeralPublicKey
    std::vector<uint8_t> aad;
    aad.push_back(VERSION);
    aad.push_back(CIPHER_SUITE);
    aad.push_back(static_cast<uint8_t>(type));
    aad.insert(aad.end(), ephemeralPublicKey.begin(), ephemeralPublicKey.end());

    // Encrypt with AES-256-GCM
    AesGcm::Tag tag;
    auto ciphertext = AesGcm::encrypt(plaintext, aesKey, iv, tag, aad);

    // Format: version(1) + cipherSuite(1) + type(1) + ephemeralPublicKey(33) + IV(12) + authTag(16) + [length(8)] + ciphertext
    std::vector<uint8_t> result;
    size_t totalSize = HEADER_SIZE + EPHEMERAL_KEY_SIZE + AesGcm::IV_SIZE + AesGcm::TAG_SIZE + ciphertext.size();
    if (type == EciesEncryptionType::WithLength) {
        totalSize += 8;  // length prefix
    }
    result.reserve(totalSize);

    // Header
    result.push_back(VERSION);
    result.push_back(CIPHER_SUITE);
    result.push_back(static_cast<uint8_t>(type));

    // Ephemeral public key
    result.insert(result.end(), ephemeralPublicKey.begin(), ephemeralPublicKey.end());

    // IV
    result.insert(result.end(), iv.begin(), iv.end());

    // Auth tag
    result.insert(result.end(), tag.begin(), tag.end());

    // Length prefix (if WithLength mode)
    if (type == EciesEncryptionType::WithLength) {
        uint64_t len = ciphertext.size();
        for (int i = 7; i >= 0; --i) {
            result.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    // Ciphertext
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());

    return result;
}

std::vector<uint8_t> Ecies::decrypt(
    const std::vector<uint8_t>& ciphertext,
    const EcKeyPair& keyPair
) {
    size_t minSize = HEADER_SIZE + EPHEMERAL_KEY_SIZE + AesGcm::IV_SIZE + AesGcm::TAG_SIZE;
    if (ciphertext.size() < minSize) {
        throw std::runtime_error("Ciphertext too short");
    }

    size_t offset = 0;

    // Parse header
    uint8_t version = ciphertext[offset++];
    if (version != VERSION) {
        throw std::runtime_error("Invalid version");
    }

    uint8_t cipherSuite = ciphertext[offset++];
    if (cipherSuite != CIPHER_SUITE) {
        throw std::runtime_error("Invalid cipher suite");
    }

    EciesEncryptionType type = static_cast<EciesEncryptionType>(ciphertext[offset++]);

    // Extract ephemeral public key
    std::vector<uint8_t> ephemeralPublicKey(
        ciphertext.begin() + offset,
        ciphertext.begin() + offset + EPHEMERAL_KEY_SIZE
    );
    offset += EPHEMERAL_KEY_SIZE;

    // Extract IV
    AesGcm::IV iv;
    std::memcpy(iv.data(), ciphertext.data() + offset, AesGcm::IV_SIZE);
    offset += AesGcm::IV_SIZE;

    // Handle multiple recipient mode
    if (type == EciesEncryptionType::Multiple) {
        if (ciphertext.size() < offset + 4 + AesGcm::TAG_SIZE) {
            throw std::runtime_error("Multiple recipient ciphertext too short");
        }

        // Parse recipient count
        uint32_t recipientCount = 0;
        for (int i = 0; i < 4; ++i) {
            recipientCount = (recipientCount << 8) | ciphertext[offset++];
        }

        // Find our key in the recipient list
        AesGcm::Key symmetricKey;
        bool foundKey = false;

        for (uint32_t i = 0; i < recipientCount; ++i) {
            if (offset + 6 > ciphertext.size()) {
                throw std::runtime_error("Truncated recipient entry");
            }

            // Parse index
            uint32_t recipientIndex = 0;
            for (int j = 0; j < 4; ++j) {
                recipientIndex = (recipientIndex << 8) | ciphertext[offset++];
            }

            // Parse encrypted key length
            uint16_t encryptedKeyLen = 0;
            for (int j = 0; j < 2; ++j) {
                encryptedKeyLen = (encryptedKeyLen << 8) | ciphertext[offset++];
            }

            if (offset + encryptedKeyLen > ciphertext.size()) {
                throw std::runtime_error("Truncated encrypted key");
            }

            std::vector<uint8_t> encryptedKey(
                ciphertext.begin() + offset,
                ciphertext.begin() + offset + encryptedKeyLen
            );
            offset += encryptedKeyLen;

            // Try to decrypt with our key pair (only if we haven't found it yet)
            if (!foundKey) {
                try {
                    symmetricKey = decryptSymmetricKey(encryptedKey, ephemeralPublicKey, 
                                                      keyPair.privateKey(), iv);
                    foundKey = true;
                } catch (...) {
                    // Not for us, continue to next recipient
                    continue;
                }
            }
        }

        if (!foundKey) {
            throw std::runtime_error("Could not decrypt symmetric key with provided key pair");
        }

        // Extract auth tag
        AesGcm::Tag tag;
        if (offset + AesGcm::TAG_SIZE > ciphertext.size()) {
            throw std::runtime_error("Missing auth tag");
        }
        std::memcpy(tag.data(), ciphertext.data() + offset, AesGcm::TAG_SIZE);
        offset += AesGcm::TAG_SIZE;

        // Extract ciphertext
        std::vector<uint8_t> encrypted(ciphertext.begin() + offset, ciphertext.end());

        // Construct AAD
        std::vector<uint8_t> aad;
        aad.push_back(version);
        aad.push_back(cipherSuite);
        aad.push_back(static_cast<uint8_t>(type));
        aad.insert(aad.end(), ephemeralPublicKey.begin(), ephemeralPublicKey.end());

        // Decrypt
        return AesGcm::decrypt(encrypted, symmetricKey, iv, tag, aad);
    }

    // Handle single recipient modes (Basic, WithLength)
    if (type != EciesEncryptionType::Basic && type != EciesEncryptionType::WithLength) {
        throw std::runtime_error("Invalid or unsupported encryption type");
    }

    // Extract auth tag
    AesGcm::Tag tag;
    std::memcpy(tag.data(), ciphertext.data() + offset, AesGcm::TAG_SIZE);
    offset += AesGcm::TAG_SIZE;

    // Extract length if WithLength mode
    size_t encryptedLen;
    if (type == EciesEncryptionType::WithLength) {
        if (offset + 8 > ciphertext.size()) {
            throw std::runtime_error("Missing length prefix");
        }
        uint64_t len = 0;
        for (int i = 0; i < 8; ++i) {
            len = (len << 8) | ciphertext[offset++];
        }
        encryptedLen = static_cast<size_t>(len);
    } else {
        encryptedLen = ciphertext.size() - offset;
    }

    // Extract encrypted data
    std::vector<uint8_t> encrypted(
        ciphertext.begin() + offset,
        ciphertext.begin() + offset + encryptedLen
    );

    // Compute ECDH shared secret
    EC_KEY* ephemeralKey = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!ephemeralKey) {
        throw std::runtime_error("Failed to create ephemeral EC key");
    }

    const EC_GROUP* group = EC_KEY_get0_group(ephemeralKey);
    EC_POINT* ephemeralPoint = EC_POINT_new(group);
    if (!ephemeralPoint ||
        EC_POINT_oct2point(group, ephemeralPoint, ephemeralPublicKey.data(),
                          ephemeralPublicKey.size(), nullptr) != 1) {
        EC_POINT_free(ephemeralPoint);
        EC_KEY_free(ephemeralKey);
        throw std::runtime_error("Invalid ephemeral public key");
    }

    auto privateKey = keyPair.privateKey();
    size_t secretLen = 32;
    std::vector<uint8_t> sharedSecret(secretLen);

    BIGNUM* priv_bn = BN_bin2bn(privateKey.data(), privateKey.size(), nullptr);
    EC_KEY_set_private_key(ephemeralKey, priv_bn);

    if (ECDH_compute_key(sharedSecret.data(), secretLen, ephemeralPoint,
                         ephemeralKey, nullptr) != static_cast<int>(secretLen)) {
        BN_free(priv_bn);
        EC_POINT_free(ephemeralPoint);
        EC_KEY_free(ephemeralKey);
        throw std::runtime_error("Failed to compute shared secret");
    }

    BN_free(priv_bn);
    EC_POINT_free(ephemeralPoint);
    EC_KEY_free(ephemeralKey);

    // Derive AES key using HKDF-SHA256
    AesGcm::Key aesKey;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!pctx ||
        EVP_PKEY_derive_init(pctx) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(pctx, sharedSecret.data(), sharedSecret.size()) <= 0 ||
        EVP_PKEY_CTX_add1_hkdf_info(pctx,
            reinterpret_cast<const unsigned char*>("ecies-v2-key-derivation"), 23) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to setup HKDF");
    }

    size_t keyLen = AesGcm::KEY_SIZE;
    if (EVP_PKEY_derive(pctx, aesKey.data(), &keyLen) <= 0 || keyLen != AesGcm::KEY_SIZE) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to derive AES key");
    }
    EVP_PKEY_CTX_free(pctx);

    // Construct AAD: version + cipherSuite + type + ephemeralPublicKey
    std::vector<uint8_t> aad;
    aad.push_back(version);
    aad.push_back(cipherSuite);
    aad.push_back(static_cast<uint8_t>(type));
    aad.insert(aad.end(), ephemeralPublicKey.begin(), ephemeralPublicKey.end());

    // Decrypt with AES-256-GCM
    return AesGcm::decrypt(encrypted, aesKey, iv, tag, aad);
}

} // namespace brightchain
