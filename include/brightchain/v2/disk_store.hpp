#pragma once
// Portable local disk profile (spec section 5): FORMAT, HEAD, WRITER, LOCK and
// blocks/<hh>/<digest>, with flock(2) writer exclusion.

#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include "brightchain/v2/codec.hpp"

namespace brightchain::v2 {

class StorageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};
class MissingBlockError : public StorageError {
 public:
  explicit MissingBlockError(const std::string& digest)
      : StorageError("Missing BrightDB v2 block " + digest) {}
};
/// HEAD may or may not have been replaced; reopen to find out.
class CommitOutcomeUnknownError : public StorageError {
 public:
  using StorageError::StorageError;
};

class DiskStore {
 public:
  class Writer {
   public:
    Digest writeBlock(const Bytes& payload);
    /// Make every block written so far durable without publishing a head.
    void syncBlocks();
    void publishHead(const Digest& digest);

   private:
    friend class DiskStore;
    explicit Writer(DiskStore& store) : store_(store) {}
    DiskStore& store_;
    std::set<std::string> dirty_;
  };

  /// Open an initialized store; fails on a missing, empty or unsupported directory.
  static DiskStore open(const std::string& path);
  /// Initialize a new or empty directory with the given initial database root.
  static DiskStore create(const std::string& path, const Bytes& initialRoot,
                          const std::string& writerIdHex = "");
  static DiskStore openOrCreate(const std::string& path, const Bytes& initialRoot);

  const std::string& path() const { return path_; }
  const std::string& writerId() const { return writerId_; }
  Digest readHead() const;
  /// Read and verify a block against its digest.
  Bytes readBlock(const Digest& digest) const;
  /// Whether a block file exists. It does not read or verify the block.
  bool hasBlock(const Digest& digest) const;
  /// Run `body` holding the exclusive write lock.
  void withWriteLock(const std::function<void(Writer&)>& body);

 private:
  DiskStore(std::string path, std::string writerId) : path_(std::move(path)), writerId_(std::move(writerId)) {}
  Digest installBlock(const Bytes& payload, std::set<std::string>& dirty) const;
  void syncDirty(std::set<std::string>& dirty) const;
  void publishHead(const Digest& digest, std::set<std::string>& dirty) const;

  std::string path_;
  std::string writerId_;
};

}  // namespace brightchain::v2
