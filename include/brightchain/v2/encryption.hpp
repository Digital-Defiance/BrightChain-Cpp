#pragma once
// Encrypted block-store profile (spec section 10): blocks keep their logical digests but are stored
// under keyed names as AES-256-GCM ciphertext bound to the digest.

#include <memory>
#include <stdexcept>
#include <string>

#include "brightchain/v2/codec.hpp"

namespace brightchain::v2 {

constexpr const char* kEncryptionMarker = "BrightDB encryption 1\n";
/// Bytes a sealed block adds to its plaintext: version, nonce and tag.
constexpr std::size_t kSealOverhead = 1 + 12 + 16;

class DecryptionError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// AES-256-GCM with keys derived from one 32-byte master key.
class AesCipher {
 public:
  explicit AesCipher(const Bytes& masterKey);

  /// The on-disk file name (128 lowercase hex digits) for a logical digest.
  std::string physicalName(const Digest& logicalDigest) const;
  /// `nonce` is for known-answer tests; leave it empty to use a random one.
  Bytes seal(const Digest& logicalDigest, const Bytes& plaintext, const Bytes& nonce = {}) const;
  /// Throws DecryptionError on a wrong key, a changed block or a block moved to another name.
  Bytes open(const Digest& logicalDigest, const Bytes& sealed) const;
  Bytes sealHead(const Digest& digest, const Bytes& nonce = {}) const;
  Digest openHead(const Bytes& sealed) const;
  static constexpr std::size_t sealedHeadBytes() { return kSealOverhead + 64; }

 private:
  Bytes encrypt(const Bytes& aad, const Bytes& plaintext, Bytes nonce) const;
  Bytes decrypt(const Bytes& aad, const Bytes& sealed) const;
  Bytes nameKey_, dataKey_;
};

}  // namespace brightchain::v2
