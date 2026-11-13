#include "brightchain/v2/disk_store.hpp"

#include <fcntl.h>
#include <openssl/rand.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <map>
#include <mutex>
#include <optional>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace brightchain::v2 {

namespace fs = std::filesystem;

namespace {

const std::string kFormat = "BrightDB storage 2\n";

[[noreturn]] void fail(const std::string& what, const std::string& path) {
  throw StorageError(what + ": " + path + " (" + std::strerror(errno) + ")");
}

class Fd {
 public:
  explicit Fd(int fd = -1) : fd_(fd) {}
  Fd(Fd&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
  Fd(const Fd&) = delete;
  ~Fd() { if (fd_ >= 0) ::close(fd_); }
  int get() const { return fd_; }
 private:
  int fd_;
};

bool isLowerHex(const std::string& s, std::size_t n) {
  if (s.size() != n) return false;
  for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

/// Read a regular, non-symlink file of 1..maximum bytes.
std::string readBounded(const std::string& path, std::size_t maximum, bool* missing = nullptr) {
  int raw = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (raw < 0) {
    if (errno == ENOENT && missing) { *missing = true; return {}; }
    fail("Cannot open BrightDB v2 file", path);
  }
  Fd fd(raw);
  struct stat st;
  if (::fstat(fd.get(), &st) != 0) fail("Cannot stat BrightDB v2 file", path);
  if (!S_ISREG(st.st_mode) || st.st_size < 1 || std::size_t(st.st_size) > maximum)
    throw StorageError("Invalid BrightDB v2 file: " + path);
  std::string data(std::size_t(st.st_size), '\0');
  std::size_t got = 0;
  while (got < data.size()) {
    auto n = ::read(fd.get(), data.data() + got, data.size() - got);
    if (n <= 0) throw StorageError("BrightDB v2 file changed while reading: " + path);
    got += std::size_t(n);
  }
  return data;
}

std::string readHexLine(const std::string& path, std::size_t length) {
  auto text = readBounded(path, length + 1);
  if (text.back() != '\n' || !isLowerHex(text.substr(0, text.size() - 1), length))
    throw StorageError("Invalid BrightDB v2 record: " + path);
  return text.substr(0, text.size() - 1);
}

void writeSynced(const std::string& path, const void* data, std::size_t size) {
  int raw = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (raw < 0) fail("Cannot create file", path);
  Fd fd(raw);
  const auto* p = static_cast<const char*>(data);
  std::size_t done = 0;
  while (done < size) {
    auto n = ::write(fd.get(), p + done, size - done);
    if (n < 0) { if (errno == EINTR) continue; fail("Cannot write file", path); }
    done += std::size_t(n);
  }
  if (::fsync(fd.get()) != 0) fail("Cannot sync file", path);
}

void syncDirectory(const std::string& path) {
  int raw = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
  if (raw < 0) fail("Cannot open directory", path);
  Fd fd(raw);
  if (::fsync(fd.get()) != 0) fail("Cannot sync directory", path);
}

std::string temporaryName(const std::string& directory) {
  unsigned char r[12];
  RAND_bytes(r, sizeof r);
  return directory + "/.brightdb-" + toHex(Bytes(r, r + sizeof r)) + ".tmp";
}

void requireDirectory(const std::string& path) {
  struct stat st;
  if (::lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
    throw StorageError("Expected a BrightDB v2 directory: " + path);
}


// ---- packed extension (spec section 5.1) ----------------------------------------------------

const std::string kPacksMarker = "BrightDB packs 1\n";
const std::string kPackHeader = "BrightDB pack 1\n";
const std::string kPackEnd = "BDBPEND\n";
constexpr std::size_t kNameBytes = 64, kIndexEntryBytes = 64 + 8 + 4, kTrailerBytes = 4 + 8 + 8;
constexpr std::size_t kFlushBytes = 32u * 1024 * 1024, kFlushBlocks = 4096;

struct PackLoc {
  std::string pack;
  std::uint64_t offset = 0;
  std::uint32_t length = 0;
};

void putU32(std::string& out, std::uint32_t v) {
  for (int i = 3; i >= 0; i--) out.push_back(char((v >> (8 * i)) & 0xff));
}
void putU64(std::string& out, std::uint64_t v) {
  for (int i = 7; i >= 0; i--) out.push_back(char((v >> (8 * i)) & 0xff));
}
std::uint64_t getBE(const char* p, int bytes) {
  std::uint64_t v = 0;
  for (int i = 0; i < bytes; i++) v = (v << 8) | static_cast<unsigned char>(p[i]);
  return v;
}

std::string encodePack(const std::vector<std::pair<std::string, Bytes>>& entries) {
  std::string body = kPackHeader, index;
  for (const auto& [name, bytes] : entries) {
    if (!isLowerHex(name, 128)) throw StorageError("Invalid block name for a pack");
    auto raw = fromHex(name);
    index.append(reinterpret_cast<const char*>(raw.data()), raw.size());
    putU64(index, body.size());
    putU32(index, std::uint32_t(bytes.size()));
    body.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  auto indexOffset = body.size();
  body += index;
  putU32(body, std::uint32_t(entries.size()));
  putU64(body, indexOffset);
  body += kPackEnd;
  return body;
}

std::map<std::string, std::pair<std::uint64_t, std::uint32_t>> readPackIndex(const std::string& path) {
  auto bad = [&](const char* why) { return StorageError("Invalid BrightDB v2 pack " + path + ": " + why); };
  int raw = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);
  if (raw < 0) fail("Cannot open BrightDB v2 pack", path);
  Fd fd(raw);
  struct stat st;
  if (::fstat(fd.get(), &st) != 0) fail("Cannot stat BrightDB v2 pack", path);
  auto size = std::uint64_t(st.st_size);
  if (!S_ISREG(st.st_mode) || size < kPackHeader.size() + kTrailerBytes) throw bad("too small");
  auto readAt = [&](std::uint64_t offset, std::size_t n) {
    std::string out(n, '\0');
    std::size_t got = 0;
    while (got < n) {
      auto r = ::pread(fd.get(), out.data() + got, n - got, off_t(offset + got));
      if (r <= 0) throw bad("short read");
      got += std::size_t(r);
    }
    return out;
  };
  if (readAt(0, kPackHeader.size()) != kPackHeader) throw bad("bad header");
  auto trailer = readAt(size - kTrailerBytes, kTrailerBytes);
  if (trailer.substr(12) != kPackEnd) throw bad("bad trailer (incomplete write?)");
  auto count = getBE(trailer.data(), 4);
  auto indexOffset = getBE(trailer.data() + 4, 8);
  if (indexOffset < kPackHeader.size() || indexOffset + count * kIndexEntryBytes + kTrailerBytes != size)
    throw bad("index does not fit the file");
  std::map<std::string, std::pair<std::uint64_t, std::uint32_t>> entries;
  auto raw_index = count ? readAt(indexOffset, std::size_t(count * kIndexEntryBytes)) : std::string();
  for (std::uint64_t i = 0; i < count; i++) {
    const char* row = raw_index.data() + i * kIndexEntryBytes;
    auto name = toHex(Bytes(reinterpret_cast<const unsigned char*>(row), reinterpret_cast<const unsigned char*>(row) + kNameBytes));
    auto offset = getBE(row + kNameBytes, 8);
    auto length = std::uint32_t(getBE(row + kNameBytes + 8, 4));
    if (offset < kPackHeader.size() || length < 1 || offset + length > indexOffset) throw bad("entry out of range");
    entries[name] = {offset, length};
  }
  return entries;
}

/// Read one pack entry; sets *missing when the pack file is gone.
Bytes readPackEntry(const std::string& path, std::uint64_t offset, std::uint32_t length, std::size_t maximum, bool* missing) {
  if (length > maximum) throw StorageError("Invalid BrightDB v2 pack entry in " + path);
  int raw = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);
  if (raw < 0) {
    if (errno == ENOENT && missing) { *missing = true; return {}; }
    fail("Cannot open BrightDB v2 pack", path);
  }
  Fd fd(raw);
  Bytes out(length);
  std::size_t got = 0;
  while (got < length) {
    auto r = ::pread(fd.get(), out.data() + got, length - got, off_t(offset + got));
    if (r <= 0) throw StorageError("Short read in BrightDB v2 pack " + path);
    got += std::size_t(r);
  }
  return out;
}

std::int64_t modifiedMs(const fs::path& path) {
  auto mtime = std::chrono::duration_cast<std::chrono::milliseconds>(fs::last_write_time(path).time_since_epoch()).count();
  // file_clock and system_clock may differ by a fixed offset; convert through now().
  auto skew = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch() - fs::file_time_type::clock::now().time_since_epoch())
                  .count();
  return std::int64_t(mtime + skew);
}

void setModifiedMs(const fs::path& path, std::int64_t ms) {
  auto skew = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch() - fs::file_time_type::clock::now().time_since_epoch())
                  .count();
  fs::last_write_time(path, fs::file_time_type(std::chrono::duration_cast<fs::file_time_type::duration>(std::chrono::milliseconds(ms - skew))));
}

bool pathExists(const std::string& path) {
  struct stat st;
  return ::lstat(path.c_str(), &st) == 0;
}

std::string blockDir(const std::string& root, const Digest& d) { return root + "/blocks/" + d.substr(0, 2); }

void assertDigest(const Digest& d) {
  if (!isLowerHex(d, 128)) throw StorageError("Invalid BrightDB v2 block digest");
}

}  // namespace

struct DiskStore::Packs {
  std::mutex mu;
  bool packed = false;
  std::map<std::string, PackLoc> index;
  std::map<std::string, std::int64_t> mtimes;
  std::map<std::string, std::map<std::string, std::pair<std::uint64_t, std::uint32_t>>> entries;
  /// Blocks of the current write scope not yet in a pack (only while pendingActive).
  std::map<std::string, Bytes> pending;
  std::size_t pendingBytes = 0;
  bool pendingActive = false;
};

DiskStore::DiskStore(std::string path, std::string writerId, Cipher cipher)
    : path_(std::move(path)), writerId_(std::move(writerId)), cipher_(std::move(cipher)), packs_(std::make_shared<Packs>()) {}

std::size_t DiskStore::maxStoredBytes() const { return kMaxDocumentBytes + (cipher_ ? kSealOverhead : 0); }

std::pair<std::string, std::string> DiskStore::blockLocation(const Digest& digest) const {
  auto name = cipher_ ? cipher_->physicalName(digest) : digest;
  return {path_ + "/blocks/" + name.substr(0, 2), name};
}

Digest DiskStore::readHead() const {
  if (!cipher_) return readHexLine(path_ + "/HEAD", 128);
  auto sealed = readBounded(path_ + "/HEAD", AesCipher::sealedHeadBytes());
  try {
    return cipher_->openHead(Bytes(sealed.begin(), sealed.end()));
  } catch (const DecryptionError& e) {
    throw StorageError(std::string("Cannot decrypt HEAD: ") + e.what());
  }
}

Bytes DiskStore::readBlock(const Digest& digest) const {
  assertDigest(digest);
  auto [directory, file] = blockLocation(digest);
  auto stored = readStored(file, directory);
  if (!stored) throw MissingBlockError(digest);
  Bytes bytes = std::move(*stored);
  if (cipher_) {
    try {
      bytes = cipher_->open(digest, bytes);
    } catch (const DecryptionError& e) {
      throw StorageError("BrightDB v2 block cannot be decrypted: " + digest + " (" + e.what() + ")");
    }
  }
  if (digestPayload(bytes) != digest) throw StorageError("BrightDB v2 block digest mismatch: " + digest);
  return bytes;
}

Digest DiskStore::installBlock(const Bytes& payload, std::set<std::string>& dirty) const {
  if (payload.empty() || payload.size() > kMaxDocumentBytes)
    throw StorageError("BrightDB v2 block payload exceeds format limits");
  auto digest = digestPayload(payload);
  auto [directory, file] = blockLocation(digest);
  try {
    readBlock(digest);
    dirty.insert(directory);
    return digest;
  } catch (const MissingBlockError&) {
  }
  bool queued = false, flush = false;
  {
    std::lock_guard<std::mutex> guard(packs_->mu);
    if (packs_->pendingActive) {
      Bytes stored = cipher_ ? cipher_->seal(digest, payload) : payload;
      packs_->pendingBytes += stored.size();
      packs_->pending.emplace(file, std::move(stored));
      flush = packs_->pendingBytes >= kFlushBytes || packs_->pending.size() >= kFlushBlocks;
      queued = true;
    }
  }
  if (queued) {
    if (flush) flushPending();
    return digest;
  }
  if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) fail("Cannot create block directory", directory);
  requireDirectory(directory);
  auto temporary = temporaryName(directory);
  try {
    if (cipher_) {
      auto sealed = cipher_->seal(digest, payload);
      writeSynced(temporary, sealed.data(), sealed.size());
    } else {
      writeSynced(temporary, payload.data(), payload.size());
    }
    if (::link(temporary.c_str(), (directory + "/" + file).c_str()) != 0 && errno != EEXIST)
      fail("Cannot install block", directory + "/" + file);
    readBlock(digest);
  } catch (...) {
    ::unlink(temporary.c_str());
    throw;
  }
  ::unlink(temporary.c_str());
  dirty.insert(directory);
  return digest;
}

void DiskStore::syncDirty(std::set<std::string>& dirty) const {
  flushPending();
  for (const auto& d : dirty) {
    if (pathExists(d)) syncDirectory(d);  // a packed block has no loose directory
  }
  syncDirectory(path_ + "/blocks");
  dirty.clear();
}

bool DiskStore::hasBlock(const Digest& digest) const {
  assertDigest(digest);
  auto [directory, file] = blockLocation(digest);
  {
    std::lock_guard<std::mutex> guard(packs_->mu);
    if (packs_->pending.count(file) || packs_->index.count(file)) return true;
  }
  struct stat st;
  if (::lstat((directory + "/" + file).c_str(), &st) == 0) return S_ISREG(st.st_mode);
  if (errno != ENOENT) fail("Cannot stat block", digest);
  std::lock_guard<std::mutex> guard(packs_->mu);
  return refreshPacksLocked() && packs_->index.count(file) > 0;
}

std::string DiskStore::fileNameOf(const Digest& digest) const { return blockLocation(digest).second; }

std::vector<DiskStore::BlockFile> DiskStore::listBlockFiles() const {
  std::vector<BlockFile> out;
  for (const auto& prefix : fs::directory_iterator(path_ + "/blocks")) {
    if (!prefix.is_directory()) continue;
    for (const auto& entry : fs::directory_iterator(prefix.path())) {
      if (!entry.is_regular_file()) continue;
      out.push_back({entry.path().filename().string(), std::uint64_t(entry.file_size()), modifiedMs(entry.path())});
    }
  }
  std::lock_guard<std::mutex> guard(packs_->mu);
  refreshPacksLocked();
  for (const auto& [name, loc] : packs_->index) {
    auto mtime = packs_->mtimes.find(loc.pack);
    out.push_back({name, loc.length, mtime == packs_->mtimes.end() ? 0 : mtime->second});
  }
  std::error_code ec;
  if (fs::is_directory(path_ + "/packs", ec)) {
    for (const auto& entry : fs::directory_iterator(path_ + "/packs")) {
      auto name = entry.path().filename().string();
      if (!name.empty() && name[0] == '.' && entry.is_regular_file())
        out.push_back({name, std::uint64_t(entry.file_size()), modifiedMs(entry.path())});
    }
  }
  return out;
}

void DiskStore::deleteBlockFile(const std::string& name) const { deleteBlockFiles({name}); }

void DiskStore::deleteBlockFiles(const std::vector<std::string>& names) const {
  std::set<std::string> packed;
  for (const auto& name : names) {
    if (!name.empty() && name[0] == '.') {
      // A leftover temporary file may sit in any prefix directory or in packs/.
      for (const auto& prefix : fs::directory_iterator(path_ + "/blocks")) {
        std::error_code ignored;
        fs::remove(prefix.path() / name, ignored);
      }
      std::error_code ignored;
      fs::remove(fs::path(path_) / "packs" / name, ignored);
      continue;
    }
    if (!isLowerHex(name, 128)) throw StorageError("Invalid block file name");
    if (::unlink((path_ + "/blocks/" + name.substr(0, 2) + "/" + name).c_str()) != 0 && errno != ENOENT)
      fail("Cannot delete block", name);
    packed.insert(name);
  }
  if (packed.empty()) return;
  std::lock_guard<std::mutex> guard(packs_->mu);
  refreshPacksLocked();
  std::set<std::string> affected;
  for (const auto& name : packed) {
    auto it = packs_->index.find(name);
    if (it != packs_->index.end()) affected.insert(it->second.pack);
  }
  for (const auto& pack : affected)
    rewritePackLocked(pack, [&](const std::string& name, Bytes&) { return packed.count(name) ? 1 : 0; });
}

bool DiskStore::headNeedsReseal() const {
  if (!cipher_) return false;
  auto data = readBounded(path_ + "/HEAD", AesCipher::sealedHeadBytes());
  return !data.empty() && static_cast<unsigned char>(data[0]) != cipher_->currentVersion();
}

void DiskStore::replaceLoose(const Digest& digest, const std::string& directory, const std::string& file, const std::string& sealed) const {
  auto next = cipher_->reseal(digest, Bytes(sealed.begin(), sealed.end()));
  if (next.empty()) return;
  auto temporary = temporaryName(directory);
  try {
    writeSynced(temporary, next.data(), next.size());
    if (::rename(temporary.c_str(), (directory + "/" + file).c_str()) != 0) fail("Cannot replace block", directory + "/" + file);
    syncDirectory(directory);
  } catch (...) {
    ::unlink(temporary.c_str());
    throw;
  }
}

DiskStore::ResealCounts DiskStore::resealBlocks(const std::vector<Digest>& digests, bool dryRun) const {
  if (!cipher_) throw StorageError("Re-sealing needs an encrypted store");
  int current = cipher_->currentVersion();
  std::map<std::string, Digest> byName;
  for (const auto& digest : digests) {
    assertDigest(digest);
    byName[blockLocation(digest).second] = digest;
  }
  std::set<std::string> stale, touchedPacks;
  {
    std::lock_guard<std::mutex> guard(packs_->mu);
    refreshPacksLocked();
    for (const auto& [name, digest] : byName) {
      auto [directory, file] = blockLocation(digest);
      bool missing = false;
      auto loose = readBounded(directory + "/" + file, maxStoredBytes(), &missing);
      if (!missing && static_cast<unsigned char>(loose[0]) != current) {
        stale.insert(name);
        if (!dryRun) replaceLoose(digest, directory, file, loose);
      }
      auto entry = packs_->index.find(name);
      if (entry != packs_->index.end()) {
        auto sealed = readPackEntry(path_ + "/packs/" + entry->second.pack, entry->second.offset, entry->second.length, maxStoredBytes(), nullptr);
        if (sealed[0] != current) {
          stale.insert(name);
          touchedPacks.insert(entry->second.pack);
        }
      }
    }
    if (!dryRun) {
      for (const auto& pack : touchedPacks) {
        rewritePackLocked(pack, [&](const std::string& name, Bytes& bytes) {
          auto found = byName.find(name);
          if (found == byName.end() || bytes[0] == current) return 0;
          auto next = cipher_->reseal(found->second, bytes);
          if (next.empty()) return 0;
          bytes = std::move(next);
          return 2;
        });
      }
    }
  }
  if (!dryRun)
    for (const auto& [name, digest] : byName) readBlock(digest);  // verify every block still opens
  return {stale.size(), byName.size() - stale.size()};
}

bool DiskStore::packed() const {
  std::lock_guard<std::mutex> guard(packs_->mu);
  return packs_->packed;
}

void DiskStore::enablePacks() const {
  std::lock_guard<std::mutex> guard(packs_->mu);
  if (packs_->packed) return;
  auto marker = path_ + "/PACKED";
  if (!pathExists(marker)) {
    writeSynced(marker, kPacksMarker.data(), kPacksMarker.size());
    syncDirectory(path_);
  }
  packs_->packed = true;
}

DiskStore::CompactCounts DiskStore::compactPacks() const {
  std::lock_guard<std::mutex> guard(packs_->mu);
  refreshPacksLocked();
  std::vector<std::string> before;
  for (const auto& [pack, mtime] : packs_->mtimes) before.push_back(pack);
  if (before.size() <= 1) return {before.size(), before.size()};
  std::vector<std::pair<std::string, Bytes>> entries;
  std::set<std::string> seen;
  for (const auto& [name, loc] : packs_->index) {
    if (!seen.insert(name).second) continue;
    entries.emplace_back(name, readPackEntry(path_ + "/packs/" + loc.pack, loc.offset, loc.length, maxStoredBytes(), nullptr));
  }
  auto created = writePackLocked(entries, std::nullopt);
  for (const auto& pack : before)
    if (pack != created) ::unlink((path_ + "/packs/" + pack).c_str());
  syncDirectory(path_ + "/packs");
  refreshPacksLocked();
  return {before.size(), 1};
}

bool DiskStore::refreshPacksLocked() const {
  std::set<std::string> present;
  std::error_code ec;
  if (fs::is_directory(path_ + "/packs", ec)) {
    for (const auto& entry : fs::directory_iterator(path_ + "/packs")) {
      auto name = entry.path().filename().string();
      if (name.size() > 5 && name.compare(name.size() - 5, 5, ".pack") == 0 && name[0] != '.') present.insert(name);
    }
  }
  bool removed = false;
  for (auto it = packs_->mtimes.begin(); it != packs_->mtimes.end();) {
    if (!present.count(it->first)) {
      packs_->entries.erase(it->first);
      it = packs_->mtimes.erase(it);
      removed = true;
    } else {
      ++it;
    }
  }
  if (removed) {
    // A block may also live in a pack that stays: rebuild the lookup from the packs that remain.
    packs_->index.clear();
    for (const auto& [pack, entries] : packs_->entries)
      for (const auto& [name, loc] : entries) packs_->index.emplace(name, PackLoc{pack, loc.first, loc.second});
  }
  bool added = false;
  for (const auto& pack : present) {
    if (packs_->mtimes.count(pack)) continue;
    auto path = path_ + "/packs/" + pack;
    std::map<std::string, std::pair<std::uint64_t, std::uint32_t>> entries;
    try {
      entries = readPackIndex(path);
    } catch (const StorageError&) {
      if (!pathExists(path)) continue;  // removed while listing
      throw;
    }
    packs_->mtimes[pack] = modifiedMs(path);
    for (const auto& [name, loc] : entries) packs_->index.emplace(name, PackLoc{pack, loc.first, loc.second});
    packs_->entries[pack] = std::move(entries);
    added = true;
  }
  return added;
}

std::optional<Bytes> DiskStore::readStored(const std::string& name, const std::string& directory) const {
  std::lock_guard<std::mutex> guard(packs_->mu);
  if (auto waiting = packs_->pending.find(name); waiting != packs_->pending.end()) return waiting->second;
  for (int attempt = 0; attempt < 2; attempt++) {
    if (auto entry = packs_->index.find(name); entry != packs_->index.end()) {
      bool missing = false;
      auto data = readPackEntry(path_ + "/packs/" + entry->second.pack, entry->second.offset, entry->second.length, maxStoredBytes(), &missing);
      if (!missing) return data;
      refreshPacksLocked();  // the pack was rewritten or removed by another process
    }
    bool missing = false;
    auto data = readBounded(directory + "/" + name, maxStoredBytes(), &missing);
    if (!missing) return Bytes(data.begin(), data.end());
    if (!refreshPacksLocked() && !packs_->index.count(name)) return std::nullopt;
  }
  return std::nullopt;
}

void DiskStore::flushPending() const {
  std::lock_guard<std::mutex> guard(packs_->mu);
  if (packs_->pending.empty()) return;
  std::vector<std::pair<std::string, Bytes>> entries(packs_->pending.begin(), packs_->pending.end());
  writePackLocked(entries, std::nullopt);
  packs_->pending.clear();
  packs_->pendingBytes = 0;
}

std::string DiskStore::writePackLocked(const std::vector<std::pair<std::string, Bytes>>& entries, std::optional<std::int64_t> mtimeMs) const {
  auto packsDir = path_ + "/packs";
  if (::mkdir(packsDir.c_str(), 0700) == 0) {
    syncDirectory(path_);
  } else if (errno != EEXIST) {
    fail("Cannot create packs directory", packsDir);
  }
  unsigned char r[16];
  RAND_bytes(r, sizeof r);
  auto file = toHex(Bytes(r, r + sizeof r)) + ".pack";
  auto temporary = temporaryName(packsDir);
  try {
    auto bytes = encodePack(entries);
    writeSynced(temporary, bytes.data(), bytes.size());
    if (mtimeMs) setModifiedMs(temporary, *mtimeMs);
    if (::rename(temporary.c_str(), (packsDir + "/" + file).c_str()) != 0) fail("Cannot install pack", packsDir + "/" + file);
    syncDirectory(packsDir);
  } catch (...) {
    ::unlink(temporary.c_str());
    throw;
  }
  refreshPacksLocked();
  return file;
}

void DiskStore::rewritePackLocked(const std::string& pack, const std::function<int(const std::string&, Bytes&)>& change) const {
  auto path = path_ + "/packs/" + pack;
  auto index = readPackIndex(path);
  std::vector<std::pair<std::string, Bytes>> kept;
  bool changed = false;
  for (const auto& [name, loc] : index) {
    auto bytes = readPackEntry(path, loc.first, loc.second, maxStoredBytes(), nullptr);
    auto action = change(name, bytes);
    if (action == 1) {
      changed = true;
    } else {
      if (action == 2) changed = true;
      kept.emplace_back(name, std::move(bytes));
    }
  }
  if (!changed) return;
  auto mtime = modifiedMs(path);
  if (!kept.empty()) writePackLocked(kept, mtime);
  if (::unlink(path.c_str()) != 0) fail("Cannot remove old pack", path);
  syncDirectory(path_ + "/packs");
  refreshPacksLocked();
}

void DiskStore::publishHead(const Digest& digest, std::set<std::string>& dirty) const {
  flushPending();
  readBlock(digest);
  dirty.insert(blockLocation(digest).first);
  syncDirty(dirty);

  auto temporary = temporaryName(path_);
  try {
    if (cipher_) {
      auto sealed = cipher_->sealHead(digest);
      writeSynced(temporary, sealed.data(), sealed.size());
    } else {
      auto line = digest + "\n";
      writeSynced(temporary, line.data(), line.size());
    }
    if (::rename(temporary.c_str(), (path_ + "/HEAD").c_str()) != 0) fail("Cannot replace HEAD", path_);
    try {
      syncDirectory(path_);
    } catch (const StorageError& e) {
      throw CommitOutcomeUnknownError(
          std::string("BrightDB v2 head was replaced but directory sync failed; reopen to determine the commit outcome: ") + e.what());
    }
  } catch (...) {
    ::unlink(temporary.c_str());
    throw;
  }
}

Digest DiskStore::Writer::writeBlock(const Bytes& payload) { return store_.installBlock(payload, dirty_); }
void DiskStore::Writer::syncBlocks() { store_.syncDirty(dirty_); }
void DiskStore::Writer::publishHead(const Digest& digest) { store_.publishHead(digest, dirty_); }

void DiskStore::withWriteLock(const std::function<void(Writer&)>& body) {
  auto lockPath = path_ + "/LOCK";
  int raw = ::open(lockPath.c_str(), O_RDWR | O_NOFOLLOW | O_NONBLOCK);
  if (raw < 0) fail("Cannot open BrightDB v2 lock file", lockPath);
  Fd fd(raw);
  struct stat st;
  if (::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode)) throw StorageError("Invalid BrightDB v2 lock file: " + path_);
  while (::flock(fd.get(), LOCK_EX) != 0) {
    if (errno != EINTR) fail("Could not acquire the BrightDB v2 write lock", lockPath);
  }
  {
    std::lock_guard<std::mutex> guard(packs_->mu);
    packs_->pending.clear();
    packs_->pendingBytes = 0;
    packs_->pendingActive = packs_->packed;
  }
  struct EndScope {
    Packs& packs;
    ~EndScope() {
      std::lock_guard<std::mutex> guard(packs.mu);
      packs.pending.clear();
      packs.pendingBytes = 0;
      packs.pendingActive = false;
    }
  } endScope{*packs_};
  Writer writer(*this);
  body(writer);  // the lock is released when fd closes, including after an exception
  flushPending();
}

DiskStore DiskStore::open(const std::string& pathArg, const Cipher& cipher) {
  std::error_code ec;
  auto directory = fs::absolute(pathArg, ec).lexically_normal().string();
  while (directory.size() > 1 && directory.back() == '/') directory.pop_back();
  struct stat st;
  if (::lstat(directory.c_str(), &st) != 0) {
    if (errno == ENOENT) throw StorageError("BrightDB v2 directory does not exist: " + directory);
    fail("Cannot stat", directory);
  }
  requireDirectory(directory);
  bool missing = false;
  auto marker = readBounded(directory + "/FORMAT", kFormat.size(), &missing);
  if (missing) {
    if (fs::is_empty(directory)) throw StorageError("BrightDB v2 directory is empty: " + directory);
    throw StorageError("Unsupported or incomplete BrightDB store: " + directory);
  }
  if (marker != kFormat) throw StorageError("Unsupported BrightDB storage format: " + directory);
  auto id = readHexLine(directory + "/WRITER", 32);
  if (id == std::string(32, '0')) throw StorageError("BrightDB v2 requires a nonzero 16-byte writer ID");
  bool missingMarker = false;
  auto encryptionMarker = readBounded(directory + "/ENCRYPTION", std::strlen(kEncryptionMarker), &missingMarker);
  bool encrypted = !missingMarker;
  if (encrypted && encryptionMarker != kEncryptionMarker)
    throw StorageError("Unsupported BrightDB encryption profile: " + directory);
  if (encrypted && !cipher) throw StorageError("BrightDB directory is encrypted; open it with an encryption key: " + directory);
  if (!encrypted && cipher) throw StorageError("BrightDB directory is not encrypted; open it without an encryption key: " + directory);
  DiskStore store(directory, id, cipher);
  requireDirectory(directory + "/blocks");
  bool missingPacked = false;
  auto packedMarker = readBounded(directory + "/PACKED", kPacksMarker.size(), &missingPacked);
  if (!missingPacked) {
    if (packedMarker != kPacksMarker) throw StorageError("Unsupported BrightDB packs profile: " + directory);
    store.packs_->packed = true;
  }
  if (::lstat((directory + "/LOCK").c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    throw StorageError("Invalid BrightDB v2 lock file: " + directory);
  store.readBlock(store.readHead());
  return store;
}

DiskStore DiskStore::create(const std::string& pathArg, const Bytes& initialRoot, const std::string& writerIdHex,
                            const Cipher& cipher) {
  std::error_code ec;
  auto directory = fs::absolute(pathArg, ec).lexically_normal().string();
  while (directory.size() > 1 && directory.back() == '/') directory.pop_back();
  std::string id = writerIdHex;
  if (id.empty()) {
    unsigned char r[16];
    do { RAND_bytes(r, sizeof r); id = toHex(Bytes(r, r + sizeof r)); } while (id == std::string(32, '0'));
  }
  if (!isLowerHex(id, 32) || id == std::string(32, '0'))
    throw StorageError("BrightDB v2 requires a nonzero 16-byte writer ID");

  auto parent = fs::path(directory).parent_path();
  fs::create_directories(parent);
  std::string pattern = (parent / ".brightdb-init-XXXXXX").string();
  if (!::mkdtemp(pattern.data())) fail("Cannot create staging directory", pattern);
  std::string stage = pattern;
  try {
    DiskStore store(stage, id, cipher);
    if (::mkdir((stage + "/blocks").c_str(), 0700) != 0) fail("Cannot create blocks directory", stage);
    writeSynced(stage + "/LOCK", "", 0);
    auto line = id + "\n";
    writeSynced(stage + "/WRITER", line.data(), line.size());
    if (cipher) writeSynced(stage + "/ENCRYPTION", kEncryptionMarker, std::strlen(kEncryptionMarker));
    writeSynced(stage + "/FORMAT", kFormat.data(), kFormat.size());
    std::set<std::string> dirty;
    auto head = store.installBlock(initialRoot, dirty);
    store.publishHead(head, dirty);
    if (::rename(stage.c_str(), directory.c_str()) != 0) {
      if (errno == EEXIST || errno == ENOTEMPTY) throw StorageError("BrightDB directory is not empty: " + directory);
      fail("Cannot move initialized store into place", directory);
    }
    syncDirectory(parent.string());
  } catch (...) {
    std::error_code ignored;
    fs::remove_all(stage, ignored);
    throw;
  }
  return DiskStore(directory, id, cipher);
}

DiskStore DiskStore::openOrCreate(const std::string& path, const Bytes& initialRoot, const Cipher& cipher) {
  try {
    return open(path, cipher);
  } catch (const StorageError& e) {
    std::string m = e.what();
    if (m.find("does not exist") == std::string::npos && m.find("is empty") == std::string::npos) throw;
  }
  try {
    return create(path, initialRoot, "", cipher);
  } catch (const StorageError& e) {
    if (std::string(e.what()).find("is not empty") == std::string::npos) throw;
  }
  return open(path, cipher);
}

}  // namespace brightchain::v2
