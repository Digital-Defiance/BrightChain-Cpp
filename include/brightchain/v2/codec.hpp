#pragma once
// BrightDB storage format v2: block codec. Mirrors
// BrightChain/docs/architecture/brightdb-storage-format-v2.md. The TypeScript
// implementation is the reference; fixtures under
// BrightChain/brightchain-db/fixtures/storage-v2 define the expected bytes.

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace brightchain::v2 {

constexpr std::size_t kMaxMetadataBytes = 4096;
constexpr std::size_t kMaxDocumentBytes = 1'048'576;
constexpr std::size_t kMaxKeyBytes = 1024;
constexpr std::size_t kDigestBytes = 64;
constexpr std::size_t kLeafHeaderBytes = 7;
constexpr std::size_t kRecordHeaderBytes = 2 + 8 + 16 + 1;
constexpr std::size_t kMaxIndexKeyBytes = 2048;
constexpr std::size_t kMaxIndexFields = 8;
constexpr std::size_t kMaxFieldPathBytes = 256;
/// Attachment chunk size: every chunk but the last is exactly this long.
constexpr std::size_t kBlobChunkBytes = 1'048'576;
constexpr std::size_t kMaxBlobChunks = (kMaxMetadataBytes - 5 - 8 - 2) / 64;

using Bytes = std::vector<std::uint8_t>;
/// Lowercase SHA3-512 hex (128 digits).
using Digest = std::string;

class CodecError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct Version {
  std::uint64_t counter = 0;
  std::string writerId;  // 32 lowercase hex digits
};

int compareVersions(const Version& a, const Version& b);

struct Record {
  std::string key;  // UTF-8
  Version version;
  std::optional<Digest> target;  // nullopt is a tombstone
};

struct DatabaseRoot {
  std::string databaseId;
  std::uint64_t clock = 0;
  std::optional<Digest> catalog;
};

struct CollectionRoot {
  Version generation;
  std::optional<Digest> primary;
  std::optional<Digest> indexes;  // digest of the index definitions block
};

struct BranchChild {
  int nibble = 0;
  std::uint64_t recordBytes = 0;
  Digest digest;
};

struct Branch {
  std::vector<BranchChild> children;
};

enum class LeafKind { Primary, Catalog };

struct Leaf {
  LeafKind kind = LeafKind::Primary;
  std::vector<Record> records;
};

struct IndexDefinition {
  std::string name;
  std::vector<std::string> fields;  // dotted paths, in index order
  std::optional<Digest> root;       // root of the entry tree; nullopt while empty
};

struct IndexSet {
  std::vector<IndexDefinition> definitions;
};

/// Encoded index keys in ascending byte order.
struct IndexLeaf {
  std::vector<Bytes> entries;
};

struct IndexBranch {
  std::vector<int> skip;  // nibbles shared by every key below this branch
  std::vector<BranchChild> children;
};

struct Blob {
  std::uint64_t size = 0;
  std::vector<Digest> chunks;
};

using Metadata = std::variant<DatabaseRoot, CollectionRoot, Branch, Leaf, IndexSet, IndexLeaf, IndexBranch, Blob>;

/// Strict UTF-8 validation: no overlongs, no surrogates, nothing above U+10FFFF.
bool isValidUtf8(const std::string& s);
/// Bytes an index entry occupies in a leaf and in branch byte counts.
inline std::size_t indexEntryBytes(const Bytes& key) { return 2 + key.size(); }

std::string toHex(const Bytes& bytes);
Bytes fromHex(const std::string& hex);

/// SHA3-512 of the payload as lowercase hex.
Digest digestPayload(const Bytes& payload);
/// Raw UTF-8 key bytes after validation (1-1024 bytes, well-formed UTF-8).
Bytes encodeKey(const std::string& key);
/// Unsigned-byte lexicographic key comparison.
int compareKeys(const std::string& a, const std::string& b);
std::size_t recordByteLength(const Record& record);

Bytes encodeMetadata(const Metadata& block);
Metadata decodeMetadata(const Bytes& bytes,
                        const std::optional<Digest>& expected = std::nullopt);

/// RFC 8785 serialization with the v2 number rules. Throws CodecError.
Bytes encodeDocument(const nlohmann::json& value,
                     const std::optional<std::string>& expectedKey = std::nullopt);
/// Encode for writing: supplies a generated _id when the document has none.
struct EncodedDocument {
  std::string key;
  Bytes payload;
};
EncodedDocument encodeDocumentForWrite(const nlohmann::json& value,
                                       const std::string& generatedKey,
                                       const std::optional<std::string>& expectedKey = std::nullopt);
/// Parse and verify canonical form; throws CodecError.
nlohmann::json decodeDocument(const Bytes& bytes,
                              const std::optional<std::string>& expectedKey = std::nullopt,
                              const std::optional<Digest>& expectedDigest = std::nullopt);

}  // namespace brightchain::v2
