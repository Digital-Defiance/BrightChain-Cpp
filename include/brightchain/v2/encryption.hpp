#pragma once
// Encrypted block-store profile (spec section 10): blocks keep their logical digests but are stored
// under keyed names as AES-256-GCM ciphertext bound to the digest.

#include <map>
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

/// Keys that survive rotation. Names come from `nameSecret`, which never changes for a database, so
/// blocks keep their names when the data key rotates. Each sealed block records the data key version that
/// sealed it; new blocks use `currentVersion`, and old ones stay readable while their version is present.
struct KeyRing {
  Bytes nameSecret;                 // 32 bytes
  std::map<int, Bytes> dataKeys;    // versions 1 to 255, each 32 bytes
  int currentVersion = 1;
};

/// AES-256-GCM. A single 32-byte master key is shorthand for a ring whose name secret and data key 1
/// are that key.
class AesCipher {
 public:
  explicit AesCipher(const Bytes& masterKey);
  explicit AesCipher(const KeyRing& ring);

  /// The on-disk file name (128 lowercase hex digits) for a logical digest.
  std::string physicalName(const Digest& logicalDigest) const;
  /// `nonce` is for known-answer tests; leave it empty to use a random one.
  Bytes seal(const Digest& logicalDigest, const Bytes& plaintext, const Bytes& nonce = {}) const;
  /// Throws DecryptionError on a wrong key, a changed block or a block moved to another name.
  Bytes open(const Digest& logicalDigest, const Bytes& sealed) const;
  /// The data key version new blocks and the head are sealed with.
  int currentVersion() const { return currentVersion_; }
  /// Re-encrypt a stored block under the current data key. Returns an empty Bytes when it already
  /// uses the current key (a sealed block is never empty). Same digest, same file name.
  Bytes reseal(const Digest& logicalDigest, const Bytes& sealed) const;
  Bytes sealHead(const Digest& digest, const Bytes& nonce = {}) const;
  Digest openHead(const Bytes& sealed) const;
  static constexpr std::size_t sealedHeadBytes() { return kSealOverhead + 64; }

 private:
  Bytes encrypt(const Bytes& aad, const Bytes& plaintext, Bytes nonce) const;
  Bytes decrypt(const Bytes& aad, const Bytes& sealed) const;
  Bytes nameKey_;
  std::map<int, Bytes> dataKeys_;
  int currentVersion_ = 1;
};

}  // namespace brightchain::v2
