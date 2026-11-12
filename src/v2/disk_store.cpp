#include "brightchain/v2/disk_store.hpp"

#include <fcntl.h>
#include <openssl/rand.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
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

std::string blockDir(const std::string& root, const Digest& d) { return root + "/blocks/" + d.substr(0, 2); }

void assertDigest(const Digest& d) {
  if (!isLowerHex(d, 128)) throw StorageError("Invalid BrightDB v2 block digest");
}

}  // namespace

Digest DiskStore::readHead() const { return readHexLine(path_ + "/HEAD", 128); }

Bytes DiskStore::readBlock(const Digest& digest) const {
  assertDigest(digest);
  bool missing = false;
  auto data = readBounded(blockDir(path_, digest) + "/" + digest, kMaxDocumentBytes, &missing);
  if (missing) throw MissingBlockError(digest);
  Bytes bytes(data.begin(), data.end());
  if (digestPayload(bytes) != digest) throw StorageError("BrightDB v2 block digest mismatch: " + digest);
  return bytes;
}

Digest DiskStore::installBlock(const Bytes& payload, std::set<std::string>& dirty) const {
  if (payload.empty() || payload.size() > kMaxDocumentBytes)
    throw StorageError("BrightDB v2 block payload exceeds format limits");
  auto digest = digestPayload(payload);
  auto directory = blockDir(path_, digest);
  try {
    readBlock(digest);
    dirty.insert(directory);
    return digest;
  } catch (const MissingBlockError&) {
  }
  if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) fail("Cannot create block directory", directory);
  requireDirectory(directory);
  auto temporary = temporaryName(directory);
  try {
    writeSynced(temporary, payload.data(), payload.size());
    if (::link(temporary.c_str(), (directory + "/" + digest).c_str()) != 0 && errno != EEXIST)
      fail("Cannot install block", directory + "/" + digest);
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
  for (const auto& d : dirty) syncDirectory(d);
  syncDirectory(path_ + "/blocks");
  dirty.clear();
}

bool DiskStore::hasBlock(const Digest& digest) const {
  assertDigest(digest);
  struct stat st;
  if (::lstat((blockDir(path_, digest) + "/" + digest).c_str(), &st) != 0) {
    if (errno == ENOENT) return false;
    fail("Cannot stat block", digest);
  }
  return S_ISREG(st.st_mode);
}

void DiskStore::publishHead(const Digest& digest, std::set<std::string>& dirty) const {
  readBlock(digest);
  dirty.insert(blockDir(path_, digest));
  syncDirty(dirty);

  auto temporary = temporaryName(path_);
  try {
    auto line = digest + "\n";
    writeSynced(temporary, line.data(), line.size());
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
  Writer writer(*this);
  body(writer);  // the lock is released when fd closes, including after an exception
}

DiskStore DiskStore::open(const std::string& pathArg) {
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
  DiskStore store(directory, id);
  requireDirectory(directory + "/blocks");
  if (::lstat((directory + "/LOCK").c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    throw StorageError("Invalid BrightDB v2 lock file: " + directory);
  store.readBlock(store.readHead());
  return store;
}

DiskStore DiskStore::create(const std::string& pathArg, const Bytes& initialRoot, const std::string& writerIdHex) {
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
    DiskStore store(stage, id);
    if (::mkdir((stage + "/blocks").c_str(), 0700) != 0) fail("Cannot create blocks directory", stage);
    writeSynced(stage + "/LOCK", "", 0);
    auto line = id + "\n";
    writeSynced(stage + "/WRITER", line.data(), line.size());
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
  return DiskStore(directory, id);
}

DiskStore DiskStore::openOrCreate(const std::string& path, const Bytes& initialRoot) {
  try {
    return open(path);
  } catch (const StorageError& e) {
    std::string m = e.what();
    if (m.find("does not exist") == std::string::npos && m.find("is empty") == std::string::npos) throw;
  }
  try {
    return create(path, initialRoot);
  } catch (const StorageError& e) {
    if (std::string(e.what()).find("is not empty") == std::string::npos) throw;
  }
  return open(path);
}

}  // namespace brightchain::v2
