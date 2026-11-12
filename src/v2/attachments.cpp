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

namespace {

struct Level { Digest digest; std::uint64_t size; };

/// Build the manifest tree over chunk entries and return the root reference.
AttachmentRef finish(BlockWriter& writer, std::vector<Level> level, const std::optional<std::string>& type) {
  std::uint64_t total = 0;
  for (const auto& e : level) total += e.size;
  int depth = 0;
  auto write = [&](const std::vector<Level>& run, int d) {
    Blob blob;
    for (const auto& e : run) { blob.size += e.size; blob.entries.push_back(e.digest); }
    blob.depth = d;
    return Level{writer.writeBlock(encodeMetadata(blob)), blob.size};
  };
  while (level.size() > kMaxBlobChunks) {
    std::vector<Level> next;
    for (std::size_t start = 0; start < level.size(); start += kMaxBlobChunks) {
      auto end = std::min(level.size(), start + kMaxBlobChunks);
      next.push_back(write(std::vector<Level>(level.begin() + std::ptrdiff_t(start), level.begin() + std::ptrdiff_t(end)), depth));
    }
    level = std::move(next);
    depth++;
  }
  auto root = write(level, depth);
  writer.syncBlocks();
  return {root.digest, total, type};
}

void checkType(const std::optional<std::string>& type) {
  if (type && type->size() > 255) throw AttachmentError("An attachment type can be at most 255 bytes");
}

}  // namespace

AttachmentRef writeAttachment(BlockWriter& writer, const Bytes& bytes, const std::optional<std::string>& type) {
  checkType(type);
  std::vector<Level> level;
  for (std::size_t offset = 0; offset < bytes.size(); offset += kBlobChunkBytes) {
    auto end = std::min(bytes.size(), offset + kBlobChunkBytes);
    level.push_back({writer.writeBlock(Bytes(bytes.begin() + std::ptrdiff_t(offset), bytes.begin() + std::ptrdiff_t(end))), end - offset});
  }
  return finish(writer, std::move(level), type);
}

AttachmentRef writeAttachment(BlockWriter& writer, std::istream& in, const std::optional<std::string>& type) {
  checkType(type);
  std::vector<Level> level;
  Bytes chunk(kBlobChunkBytes);
  while (in) {
    in.read(reinterpret_cast<char*>(chunk.data()), std::streamsize(chunk.size()));
    auto got = std::size_t(in.gcount());
    if (got == 0) break;
    level.push_back({writer.writeBlock(Bytes(chunk.begin(), chunk.begin() + std::ptrdiff_t(got))), got});
  }
  return finish(writer, std::move(level), type);
}

namespace {

/// A file's root must be as shallow as possible. Interior manifests may hold a small tail.
void checkRoot(const Blob& manifest) {
  if (manifest.depth > 0 && manifest.size <= blobChildSpan(manifest.depth))
    throw AttachmentError("Attachment root manifest is deeper than its size needs");
}

/// Visit every chunk of a file in order with the length it must have, checking manifest sizes.
void walkBlob(const DiskStore& store, const Digest& digest, std::optional<std::uint64_t> expectedSize,
              const std::function<void(const Digest&, std::uint64_t)>& visit) {
  auto manifest = readManifest(store, digest);
  if (expectedSize && manifest.size != *expectedSize) throw AttachmentError("Attachment manifest has the wrong size");
  auto span = blobChildSpan(manifest.depth);
  if (!expectedSize) checkRoot(manifest);
  std::uint64_t remaining = manifest.size;
  for (const auto& entry : manifest.entries) {
    auto size = remaining < span ? remaining : span;
    if (manifest.depth == 0) visit(entry, size);
    else walkBlob(store, entry, size, visit);
    remaining -= size;
  }
}

}  // namespace

void streamAttachment(const DiskStore& store, const Digest& digest, const std::function<void(const Bytes&)>& visit) {
  walkBlob(store, digest, std::nullopt, [&](const Digest& chunk, std::uint64_t length) {
    auto bytes = store.readBlock(chunk);
    if (bytes.size() != length) throw AttachmentError("Attachment chunk has the wrong length");
    visit(bytes);
  });
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
    checkRoot(manifest);
    if (manifest.size != entry.at("size").get<std::uint64_t>())
      throw AttachmentError("_files." + name + ".size does not match its attachment");
    walkBlob(store, digest, manifest.size, [&](const Digest& chunk, std::uint64_t) {
      if (!store.hasBlock(chunk)) throw AttachmentError("_files." + name + " is missing a chunk");
    });
  }
}

}  // namespace brightchain::v2
