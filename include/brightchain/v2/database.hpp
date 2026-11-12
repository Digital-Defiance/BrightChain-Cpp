#pragma once
// Standalone local BrightDB over the portable v2 disk profile. Same semantics as
// the TypeScript PortableBrightDb: every commit publishes one database root.

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "brightchain/v2/codec.hpp"
#include "brightchain/v2/disk_store.hpp"
#include "brightchain/v2/tree.hpp"

namespace brightchain::v2 {

class DuplicateKeyError : public std::runtime_error {
 public:
  explicit DuplicateKeyError(const std::string& id) : std::runtime_error("Duplicate _id: " + id) {}
};

struct WriteOp {
  enum class Type { CreateCollection, DropCollection, Insert, Replace, Delete };
  Type type;
  std::string collection;
  std::string id;          // Replace / Delete
  nlohmann::json document;  // Insert / Replace
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
  std::optional<nlohmann::json> document(const Record& record, const CollectionRoot& collection) const;

 private:
  friend class Database;
  DiskStore store_;
  Digest root_;
  DatabaseRoot block_;
};

class Database {
 public:
  static Database open(const std::string& path);

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

  std::vector<std::string> listCollections();
  std::optional<nlohmann::json> findById(const std::string& collection, const std::string& id);
  /// Every live document in a collection (trie order).
  std::vector<nlohmann::json> findAll(const std::string& collection);

  std::string insertOne(const std::string& collection, const nlohmann::json& document);
  std::vector<std::string> insertMany(const std::string& collection, const std::vector<nlohmann::json>& documents);
  ReplaceResult replaceById(const std::string& collection, const std::string& id, const nlohmann::json& document);
  int deleteById(const std::string& collection, const std::string& id);
  /// Apply operations in order and publish them as one commit.
  void writeBatch(const std::vector<WriteOp>& operations);

 private:
  explicit Database(DiskStore store);
  DatabaseRoot readRoot() const;

  DiskStore store_;
};

}  // namespace brightchain::v2
