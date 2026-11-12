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

class Database {
 public:
  static Database open(const std::string& path);

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
