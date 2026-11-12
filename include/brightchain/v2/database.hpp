#pragma once
// Standalone local BrightDB over the portable v2 disk profile. Same semantics as
// the TypeScript PortableBrightDb: every commit publishes one database root.

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <optional>
#include <stdexcept>
#include <vector>

#include "brightchain/v2/attachments.hpp"
#include "brightchain/v2/codec.hpp"
#include "brightchain/v2/disk_store.hpp"
#include "brightchain/v2/index_key.hpp"
#include "brightchain/v2/merge.hpp"
#include "brightchain/v2/tree.hpp"

namespace brightchain::v2 {

class DuplicateKeyError : public std::runtime_error {
 public:
  explicit DuplicateKeyError(const std::string& id) : std::runtime_error("Duplicate _id: " + id) {}
};

struct WriteOp {
  enum class Type { CreateCollection, DropCollection, Insert, Replace, Delete, CreateIndex, DropIndex };
  Type type;
  std::string collection;
  std::string id;          // Replace / Delete
  nlohmann::json document;  // Insert / Replace
  std::string name;                  // CreateIndex / DropIndex
  std::vector<std::string> fields;   // CreateIndex
};

struct GcOptions {
  std::vector<Digest> keepRoots;  // older roots to keep readable, besides the current one
  std::int64_t graceMs = 3'600'000;  // keep unreachable blocks younger than this
  bool dryRun = false;
};

struct GcResult {
  std::size_t keptReachable = 0, keptRecent = 0, removed = 0;
  std::uint64_t bytesFreed = 0;
};

struct IndexInfo {
  std::string name;
  std::vector<std::string> fields;
};

struct ReplaceResult {
  int matched = 0;
  int modified = 0;
};

/// A visible change to one document between two database roots.
struct Change {
  enum class Type { Added, Modified, Deleted };
  std::string collection;
  std::string id;
  Type type;
  std::optional<nlohmann::json> before;
  std::optional<nlohmann::json> after;
};

class Database;

/// Delivered when the database root has moved. Several commits may be covered by one event.
struct ChangeEvent {
  Digest previous;  // the last root this subscription saw
  Digest root;      // the root now current
  const Database* db;
  /// Document-level changes between `previous` and `root`.
  std::vector<Change> changes() const;
};

/// Shared by every handle opened from one Database so local commits wake subscribers at once.
struct CommitNotifier {
  std::mutex mutex;
  std::condition_variable cv;
  std::uint64_t commits = 0;
  void bump() {
    { std::lock_guard<std::mutex> lock(mutex); commits++; }
    cv.notify_all();
  }
};

/// Stops delivering events when destroyed or when stop() is called.
class Subscription {
 public:
  ~Subscription();
  Subscription(const Subscription&) = delete;
  Subscription& operator=(const Subscription&) = delete;
  void stop();

 private:
  friend class Database;
  Subscription() = default;
  struct State;
  std::shared_ptr<State> state_;
  std::thread thread_;
};

/// A read-only view pinned to one database root.
class Snapshot {
 public:
  Snapshot(DiskStore store, Digest root, DatabaseRoot block)
      : store_(std::move(store)), root_(std::move(root)), block_(std::move(block)) {}
  /// The digest identifying this database state.
  const Digest& root() const { return root_; }
  std::vector<std::string> listCollections() const;
  std::optional<nlohmann::json> findById(const std::string& collection, const std::string& id) const;
  /// Every live document in a collection (trie order).
  std::vector<nlohmann::json> findAll(const std::string& collection) const;
  /// The live collection root, or nullopt when absent in this state.
  std::optional<CollectionRoot> collectionRoot(const std::string& collection) const;
  /// Index definitions on a collection.
  std::vector<IndexInfo> listIndexes(const std::string& collection) const;
  /// Documents found through an ordered index, in index order. A document found through
  /// several entries (an array field) is returned once.
  std::vector<nlohmann::json> findByIndex(const std::string& collection, const std::string& index,
                                          const IndexQuery& query = {}) const;
  std::optional<nlohmann::json> document(const Record& record, const CollectionRoot& collection) const;

 private:
  friend class Database;
  DiskStore store_;
  Digest root_;
  DatabaseRoot block_;
};

class Database {
 public:
  /// Open or create a database. With `encryptionKey` (32 bytes) every block is encrypted at rest
  /// (spec section 10). Do not set it over a store that already encrypts.
  static Database open(const std::string& path, const std::optional<Bytes>& encryptionKey = std::nullopt);
  /// Same with a key ring, so the data key can rotate while block names stay put.
  static Database openWithKeyRing(const std::string& path, const KeyRing& ring);

  /// Copy this database into a new directory as an independent replica: same database ID, new
  /// writer ID, so both sides can write and later merge each other.
  Database fork(const std::string& path) const;
  /// Merge another replica of this database into this one (the other's current root, or `root`).
  /// Per document the greater version wins, a deletion beats an equal version, and for equal live
  /// versions the greater digest wins; a newer collection generation replaces an older one. Any
  /// order or direction of merges gives the same root. Nothing is published if a needed block is
  /// missing or fails its digest.
  MergeResult merge(const Database& other, const std::optional<Digest>& root = std::nullopt);
  /// Merge from any block source, such as an HttpBlockSource.
  MergeResult merge(const BlockSource& source, const std::optional<Digest>& root = std::nullopt);
  /// This database as a block source, for BlockServer or another database's merge.
  std::shared_ptr<const BlockSource> blockSource() const;

  /// Delete blocks that no retained root reaches (the current root, `keepRoots`, and everything they
  /// reach, including documents, indexes and attachments). Runs under the write lock. Unreachable
  /// blocks younger than `graceMs` are kept so a stored file has time to be referenced by a document.
  GcResult collectGarbage(const GcOptions& options = {});

  /// The digest of the current database state. Equal roots mean equal data.
  Digest root() const;
  Snapshot snapshot() const;
  /// A read-only view at an earlier root; throws when the root is unknown, not a
  /// database root, from another database, or its blocks are missing.
  Snapshot at(const Digest& root) const;
  /// Document-level changes from one root to another, skipping equal subtrees. A
  /// dropped or recreated collection reports its old documents as deleted and
  /// its new documents as added.
  std::vector<Change> diff(const Digest& from, const Digest& to) const;

  /// Call `listener` after the root moves, whether the commit came from this
  /// process or from another using the same directory. Events for one
  /// subscription are delivered one at a time, in order, on its own thread.
  /// `onError` receives listener exceptions and read failures.
  std::unique_ptr<Subscription> subscribe(
      std::function<void(const ChangeEvent&)> listener,
      std::chrono::milliseconds interval = std::chrono::milliseconds(250),
      std::function<void(const std::exception&)> onError = nullptr) const;

  std::vector<std::string> listCollections();
  std::optional<nlohmann::json> findById(const std::string& collection, const std::string& id);
  /// Every live document in a collection (trie order).
  std::vector<nlohmann::json> findAll(const std::string& collection);

  std::vector<IndexInfo> listIndexes(const std::string& collection) { return snapshot().listIndexes(collection); }
  std::vector<nlohmann::json> findByIndex(const std::string& collection, const std::string& index,
                                          const IndexQuery& query = {}) {
    return snapshot().findByIndex(collection, index, query);
  }
  /// Build an index over the existing documents and keep it current from then on.
  void createIndex(const std::string& collection, const std::string& name, const std::vector<std::string>& fields);
  void dropIndex(const std::string& collection, const std::string& name);

  /// Store a file of any size and return a reference to put under `_files` in a document.
  /// The blocks are durable at once but are not part of the database until a committed
  /// document references them.
  AttachmentRef putAttachment(const Bytes& bytes, const std::optional<std::string>& type = std::nullopt);
  /// Same, reading the file from a stream as it is written, so it never has to fit in memory.
  AttachmentRef putAttachment(std::istream& in, const std::optional<std::string>& type = std::nullopt);
  Bytes getAttachment(const Digest& digest) const;
  void streamAttachment(const Digest& digest, const std::function<void(const Bytes&)>& visit) const;

  std::string insertOne(const std::string& collection, const nlohmann::json& document);
  std::vector<std::string> insertMany(const std::string& collection, const std::vector<nlohmann::json>& documents);
  ReplaceResult replaceById(const std::string& collection, const std::string& id, const nlohmann::json& document);
  int deleteById(const std::string& collection, const std::string& id);
  /// Apply operations in order and publish them as one commit.
  void writeBatch(const std::vector<WriteOp>& operations);

 private:
  explicit Database(DiskStore store);
  static Database openWithCipher(const std::string& path, const DiskStore::Cipher& cipher);
  DatabaseRoot readRoot() const;

  DiskStore store_;
  std::shared_ptr<CommitNotifier> notifier_ = std::make_shared<CommitNotifier>();
};

}  // namespace brightchain::v2
