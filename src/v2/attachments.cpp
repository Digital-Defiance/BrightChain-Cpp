#include "brightchain/v2/attachments.hpp"

#include <algorithm>

namespace brightchain::v2 {

namespace {

bool isDigest(const std::string& s) {
  if (s.size() != 128) return false;
  for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

Blob readManifest(const DiskStore& store, const Digest& digest) {
  if (!isDigest(digest)) throw AttachmentError("Invalid attachment digest");
  auto block = decodeMetadata(store.readBlock(digest), digest);
  const auto* blob = std::get_if<Blob>(&block);
  if (!blob) throw AttachmentError("Digest does not identify an attachment manifest");
  return *blob;
}

}  // namespace

AttachmentRef writeAttachment(DiskStore::Writer& writer, const Bytes& bytes, const std::optional<std::string>& type) {
  if (bytes.size() > kMaxAttachmentBytes) throw AttachmentError("An attachment is too large");
  if (type && type->size() > 255) throw AttachmentError("An attachment type can be at most 255 bytes");
  Blob blob;
  blob.size = bytes.size();
  for (std::size_t offset = 0; offset < bytes.size(); offset += kBlobChunkBytes) {
    auto end = std::min(bytes.size(), offset + kBlobChunkBytes);
    blob.chunks.push_back(writer.writeBlock(Bytes(bytes.begin() + std::ptrdiff_t(offset), bytes.begin() + std::ptrdiff_t(end))));
  }
  auto digest = writer.writeBlock(encodeMetadata(blob));
  writer.syncBlocks();
  return {digest, blob.size, type};
}

void streamAttachment(const DiskStore& store, const Digest& digest, const std::function<void(const Bytes&)>& visit) {
  auto manifest = readManifest(store, digest);
  std::uint64_t remaining = manifest.size;
  for (const auto& chunk : manifest.chunks) {
    auto bytes = store.readBlock(chunk);
    std::uint64_t expected = remaining < kBlobChunkBytes ? remaining : kBlobChunkBytes;
    if (bytes.size() != expected) throw AttachmentError("Attachment chunk has the wrong length");
    remaining -= bytes.size();
    visit(bytes);
  }
}

Bytes readAttachment(const DiskStore& store, const Digest& digest) {
  Bytes out;
  streamAttachment(store, digest, [&](const Bytes& part) { out.insert(out.end(), part.begin(), part.end()); });
  return out;
}

void validateFileReferences(const DiskStore& store, const nlohmann::json& document) {
  if (!document.is_object() || !document.contains("_files")) return;
  const auto& files = document.at("_files");
  if (!files.is_object()) throw AttachmentError("_files must be an object");
  for (auto it = files.begin(); it != files.end(); ++it) {
    const auto& name = it.key();
    const auto& entry = it.value();
    if (!entry.is_object()) throw AttachmentError("_files." + name + " must be an object");
    for (auto field = entry.begin(); field != entry.end(); ++field)
      if (field.key() != "digest" && field.key() != "size" && field.key() != "type")
        throw AttachmentError("_files." + name + " has unknown properties");
    if (!entry.contains("digest") || !entry.at("digest").is_string() || !isDigest(entry.at("digest").get<std::string>()))
      throw AttachmentError("_files." + name + ".digest must be a manifest digest");
    if (!entry.contains("size") || !(entry.at("size").is_number_integer() || entry.at("size").is_number_unsigned()) ||
        entry.at("size").get<double>() < 0)
      throw AttachmentError("_files." + name + ".size must be a non-negative integer");
    if (entry.contains("type") && (!entry.at("type").is_string() || entry.at("type").get<std::string>().size() > 255))
      throw AttachmentError("_files." + name + ".type must be a string of up to 255 bytes");
    auto digest = entry.at("digest").get<std::string>();
    if (!store.hasBlock(digest)) throw AttachmentError("_files." + name + " references a missing attachment");
    auto manifest = readManifest(store, digest);
    if (manifest.size != entry.at("size").get<std::uint64_t>())
      throw AttachmentError("_files." + name + ".size does not match its attachment");
    for (const auto& chunk : manifest.chunks)
      if (!store.hasBlock(chunk)) throw AttachmentError("_files." + name + " is missing a chunk");
  }
}

}  // namespace brightchain::v2
