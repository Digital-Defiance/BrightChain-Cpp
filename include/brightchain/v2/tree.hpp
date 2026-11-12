#pragma once
// Canonical 16-way page tree for the catalog and primary indexes (spec section 3).

#include <functional>
#include <optional>
#include <vector>

#include "brightchain/v2/codec.hpp"

namespace brightchain::v2 {

/// Minimal block interface. Implementations verify digests on read.
struct BlockStore {
  virtual ~BlockStore() = default;
  virtual Bytes get(const Digest& digest) = 0;
  virtual void put(const Digest& digest, const Bytes& payload) = 0;
};

/// A record that differs between two trees. A missing side means the key is absent there.
struct RecordChange {
  std::string key;
  std::optional<Record> before;
  std::optional<Record> after;
};

class Tree {
 public:
  Tree(BlockStore& blocks, LeafKind kind) : blocks_(blocks), kind_(kind) {}

  std::optional<Record> get(const std::optional<Digest>& root, const std::string& key);
  /// Insert or replace records (later duplicates win) and return the new root.
  std::optional<Digest> setMany(const std::optional<Digest>& root, const std::vector<Record>& records);
  /// Visit records that differ between two roots. Subtrees with equal digests are
  /// skipped without being read, so the cost follows the size of the change.
  void diff(const std::optional<Digest>& from, const std::optional<Digest>& to,
            const std::function<void(const RecordChange&)>& visit);
  /// Visit every record. Order is the trie order, not key order.
  void forEach(const std::optional<Digest>& root, const std::function<void(const Record&)>& visit);

 private:
  struct Ref {
    Digest digest;
    std::uint64_t recordBytes;
  };
  struct Routed {
    Record record;
    std::string route;  // 128 hex nibbles of SHA3-512(key)
    std::uint64_t bytes;
  };
  Metadata readPage(const Digest& digest);
  std::optional<Ref> build(std::vector<Routed> records, std::size_t depth);
  std::optional<Ref> update(const std::optional<Ref>& ref, std::size_t depth, std::vector<Routed> updates);
  void collect(const Digest& digest, std::vector<Record>& out);
  void diffPages(const std::optional<Digest>& from, const std::optional<Digest>& to,
                 const std::function<void(const RecordChange&)>& visit);
  Ref write(const Metadata& page, std::uint64_t recordBytes);

  BlockStore& blocks_;
  LeafKind kind_;
};

}  // namespace brightchain::v2
