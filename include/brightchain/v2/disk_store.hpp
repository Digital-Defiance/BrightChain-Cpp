#pragma once
// Portable local disk profile (spec section 5): FORMAT, HEAD, WRITER, LOCK and
// blocks/<hh>/<digest>, with flock(2) writer exclusion.

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>
#include <string>

#include "brightchain/v2/block_source.hpp"
#include "brightchain/v2/codec.hpp"
#include "brightchain/v2/encryption.hpp"

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

class DiskStore : public BlockSource {
 public:
  class Writer : public BlockWriter {
   public:
    Digest writeBlock(const Bytes& payload) override;
    /// Make every block written so far durable without publishing a head.
    void syncBlocks() override;
    void publishHead(const Digest& digest);

   private:
    friend class DiskStore;
    explicit Writer(DiskStore& store) : store_(store) {}
    DiskStore& store_;
    std::set<std::string> dirty_;
  };

  using Cipher = std::shared_ptr<const AesCipher>;

  /// Open an initialized store; fails on a missing, empty or unsupported directory. A store
  /// created with encryption can only be opened with the same key, and a plain one without.
  static DiskStore open(const std::string& path, const Cipher& cipher = nullptr);
  /// Initialize a new or empty directory with the given initial database root.
  static DiskStore create(const std::string& path, const Bytes& initialRoot,
                          const std::string& writerIdHex = "", const Cipher& cipher = nullptr);
  static DiskStore openOrCreate(const std::string& path, const Bytes& initialRoot, const Cipher& cipher = nullptr);

  /// The cipher when blocks are encrypted at rest.
  const Cipher& cipher() const { return cipher_; }

  const std::string& path() const { return path_; }
  const std::string& writerId() const { return writerId_; }
  Digest readHead() const override;
  /// Read and verify a block against its digest.
  Bytes readBlock(const Digest& digest) const override;
  /// Whether a block file exists. It does not read or verify the block.
  bool hasBlock(const Digest& digest) const;
  /// The file name a logical digest is stored under (the digest itself, or its keyed name when encrypted).
  std::string fileNameOf(const Digest& digest) const;

  struct BlockFile {
    std::string name;
    std::uint64_t bytes = 0;
    std::int64_t modifiedMs = 0;
  };
  /// Every file under blocks/, including leftover temporary files.
  std::vector<BlockFile> listBlockFiles() const;
  /// Remove one block file by its on-disk name. The caller must hold the write lock.
  void deleteBlockFile(const std::string& name) const;
  /// Whether HEAD is sealed with an older data key than the cipher's current one.
  bool headNeedsReseal() const;
  /// Re-encrypt the given blocks under the cipher's current data key, wherever they are stored:
  /// loose files are replaced one by one (temporary file, fsync, rename) and each affected pack is
  /// rewritten once. Names do not change. `dryRun` only counts. Needs the write lock.
  struct ResealCounts {
    std::size_t resealed = 0, alreadyCurrent = 0;
  };
  ResealCounts resealBlocks(const std::vector<Digest>& digests, bool dryRun = false) const;

  /// Packed extension (spec section 5.1): new blocks go into pack files, one fsync per commit
  /// instead of one per block. Enabling adds a PACKED marker; existing loose blocks keep working.
  /// Software without pack support cannot find packed blocks, so enable it only where every reader
  /// supports it.
  bool packed() const;
  void enablePacks() const;
  /// Merge every pack into one. Needs the write lock.
  struct CompactCounts {
    std::size_t before = 0, after = 0;
  };
  CompactCounts compactPacks() const;
  /// Remove blocks by on-disk name, loose or packed (packs that lose some blocks are rewritten).
  /// Needs the write lock.
  void deleteBlockFiles(const std::vector<std::string>& names) const;
  /// Run `body` holding the exclusive write lock.
  void withWriteLock(const std::function<void(Writer&)>& body);

 private:
  struct Packs;
  DiskStore(std::string path, std::string writerId, Cipher cipher = nullptr);
  std::optional<Bytes> readStored(const std::string& name, const std::string& directory) const;
  bool refreshPacksLocked() const;
  std::string writePackLocked(const std::vector<std::pair<std::string, Bytes>>& entries, std::optional<std::int64_t> mtimeMs) const;
  /// `change` returns 0 to keep an entry, 1 to drop it, 2 when it replaced the bytes.
  void rewritePackLocked(const std::string& pack, const std::function<int(const std::string&, Bytes&)>& change) const;
  void flushPending() const;
  void replaceLoose(const Digest& digest, const std::string& directory, const std::string& file, const std::string& sealed) const;
  std::size_t maxStoredBytes() const;
  /// Directory and file name a logical digest is stored under.
  std::pair<std::string, std::string> blockLocation(const Digest& digest) const;
  Digest installBlock(const Bytes& payload, std::set<std::string>& dirty) const;
  void syncDirty(std::set<std::string>& dirty) const;
  void publishHead(const Digest& digest, std::set<std::string>& dirty) const;

  std::string path_;
  std::string writerId_;
  Cipher cipher_;
  std::shared_ptr<Packs> packs_;
};

}  // namespace brightchain::v2
