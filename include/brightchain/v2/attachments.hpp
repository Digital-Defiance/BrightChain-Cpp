#pragma once
// Attachments (spec section 8): files stored as 1 MiB chunk blocks plus a blob
// manifest, referenced from documents through the reserved `_files` property.

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "brightchain/v2/codec.hpp"
#include "brightchain/v2/disk_store.hpp"

namespace brightchain::v2 {

constexpr std::uint64_t kMaxAttachmentBytes = std::uint64_t(kBlobChunkBytes) * kMaxBlobChunks;

class AttachmentError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// A stored file. Put it under `_files` in a document to attach it.
struct AttachmentRef {
  Digest digest;  // digest of the blob manifest
  std::uint64_t size = 0;
  std::optional<std::string> type;
};

/// Write chunks and manifest. The blocks are durable but unreachable until a document references them.
AttachmentRef writeAttachment(DiskStore::Writer& writer, const Bytes& bytes, const std::optional<std::string>& type);
/// Visit a file's chunks in order, verifying every digest.
void streamAttachment(const DiskStore& store, const Digest& digest, const std::function<void(const Bytes&)>& visit);
Bytes readAttachment(const DiskStore& store, const Digest& digest);
/// Check `_files` before a document is written; throws AttachmentError.
void validateFileReferences(const DiskStore& store, const nlohmann::json& document);

}  // namespace brightchain::v2
