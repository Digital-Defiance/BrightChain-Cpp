#include "brightchain/v2/encryption.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <cstring>

namespace brightchain::v2 {

namespace {

const std::string kSalt = "BrightDB v2 encryption";
const std::string kNameLabel = "brightdb-v2-name";
const std::string kBlockAad = "BDB2-block";
const std::string kHeadAad = "BDB2-head";

Bytes hkdf(const Bytes& key, const std::string& info) {
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
  Bytes out(32);
  std::size_t length = out.size();
  bool ok = ctx && EVP_PKEY_derive_init(ctx) > 0 && EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) > 0 &&
            EVP_PKEY_CTX_set1_hkdf_salt(ctx, reinterpret_cast<const unsigned char*>(kSalt.data()), int(kSalt.size())) > 0 &&
            EVP_PKEY_CTX_set1_hkdf_key(ctx, key.data(), int(key.size())) > 0 &&
            EVP_PKEY_CTX_add1_hkdf_info(ctx, reinterpret_cast<const unsigned char*>(info.data()), int(info.size())) > 0 &&
            EVP_PKEY_derive(ctx, out.data(), &length) > 0;
  EVP_PKEY_CTX_free(ctx);
  if (!ok || length != 32) throw std::runtime_error("HKDF failed");
  return out;
}

Bytes concat(const std::string& a, const Bytes& b) {
  Bytes out(a.begin(), a.end());
  out.insert(out.end(), b.begin(), b.end());
  return out;
}

}  // namespace

AesCipher::AesCipher(const Bytes& masterKey) : AesCipher(KeyRing{masterKey, {{1, masterKey}}, 1}) {}

AesCipher::AesCipher(const KeyRing& ring) {
  auto derive = [](const Bytes& material, const std::string& info) {
    if (material.size() != 32) throw std::runtime_error("A BrightDB encryption key must be exactly 32 bytes");
    return hkdf(material, info);
  };
  nameKey_ = derive(ring.nameSecret, "name");
  for (const auto& [version, material] : ring.dataKeys) {
    if (version < 1 || version > 255) throw std::runtime_error("A key version must be an integer from 1 to 255");
    dataKeys_[version] = derive(material, "data");
  }
  if (!dataKeys_.count(ring.currentVersion)) throw std::runtime_error("The current key version has no data key");
  currentVersion_ = ring.currentVersion;
}

std::string AesCipher::physicalName(const Digest& logicalDigest) const {
  Bytes message = concat(kNameLabel, fromHex(logicalDigest));
  unsigned char out[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  if (!HMAC(EVP_sha512(), nameKey_.data(), int(nameKey_.size()), message.data(), message.size(), out, &length))
    throw std::runtime_error("HMAC failed");
  return toHex(Bytes(out, out + length));
}

Bytes AesCipher::encrypt(const Bytes& aad, const Bytes& plaintext, Bytes nonce) const {
  if (nonce.empty()) {
    nonce.resize(12);
    if (RAND_bytes(nonce.data(), 12) != 1) throw std::runtime_error("RAND_bytes failed");
  }
  if (nonce.size() != 12) throw std::runtime_error("A nonce must be 12 bytes");
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  Bytes out;
  out.push_back(std::uint8_t(currentVersion_));
  out.insert(out.end(), nonce.begin(), nonce.end());
  Bytes body(plaintext.size());
  int len = 0, total = 0;
  unsigned char tag[16];
  bool ok = ctx && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, dataKeys_.at(currentVersion_).data(), nonce.data()) == 1 &&
            EVP_EncryptUpdate(ctx, nullptr, &len, aad.data(), int(aad.size())) == 1 &&
            (plaintext.empty() || EVP_EncryptUpdate(ctx, body.data(), &len, plaintext.data(), int(plaintext.size())) == 1);
  if (ok) { total = plaintext.empty() ? 0 : len; ok = EVP_EncryptFinal_ex(ctx, body.data() + total, &len) == 1; }
  ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1;
  EVP_CIPHER_CTX_free(ctx);
  if (!ok) throw std::runtime_error("AES-GCM encryption failed");
  out.insert(out.end(), body.begin(), body.end());
  out.insert(out.end(), tag, tag + 16);
  return out;
}

Bytes AesCipher::decrypt(const Bytes& aad, const Bytes& sealed) const {
  if (sealed.size() < kSealOverhead) throw DecryptionError("Truncated encrypted block");
  auto key = dataKeys_.find(sealed[0]);
  if (key == dataKeys_.end()) throw DecryptionError("No key for encryption key version " + std::to_string(sealed[0]));
  std::size_t bodyLength = sealed.size() - kSealOverhead;
  Bytes body(bodyLength);
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  int len = 0;
  bool ok = ctx && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key->second.data(), sealed.data() + 1) == 1 &&
            EVP_DecryptUpdate(ctx, nullptr, &len, aad.data(), int(aad.size())) == 1 &&
            (bodyLength == 0 || EVP_DecryptUpdate(ctx, body.data(), &len, sealed.data() + 13, int(bodyLength)) == 1);
  int total = bodyLength == 0 ? 0 : len;
  ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<std::uint8_t*>(sealed.data() + sealed.size() - 16)) == 1 &&
       EVP_DecryptFinal_ex(ctx, body.data() + total, &len) == 1;
  EVP_CIPHER_CTX_free(ctx);
  if (!ok) throw DecryptionError("Wrong key, or the block was changed or swapped");
  return body;
}

Bytes AesCipher::seal(const Digest& logicalDigest, const Bytes& plaintext, const Bytes& nonce) const {
  return encrypt(concat(kBlockAad, fromHex(logicalDigest)), plaintext, nonce);
}

Bytes AesCipher::open(const Digest& logicalDigest, const Bytes& sealed) const {
  return decrypt(concat(kBlockAad, fromHex(logicalDigest)), sealed);
}

Bytes AesCipher::sealHead(const Digest& digest, const Bytes& nonce) const {
  return encrypt(Bytes(kHeadAad.begin(), kHeadAad.end()), fromHex(digest), nonce);
}

Digest AesCipher::openHead(const Bytes& sealed) const {
  return toHex(decrypt(Bytes(kHeadAad.begin(), kHeadAad.end()), sealed));
}

}  // namespace brightchain::v2
