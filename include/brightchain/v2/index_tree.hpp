#pragma once
// Canonical ordered tree for secondary-index entries (spec section 7): a
// path-compressed 16-way trie over key nibbles. Equal key sets give identical
// bytes, and traversal order is bytewise key order.

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "brightchain/v2/codec.hpp"
#include "brightchain/v2/tree.hpp"

namespace brightchain::v2 {

class IndexTree {
 public:
  explicit IndexTree(BlockStore& blocks) : blocks_(blocks) {}

  /// Insert and remove entries and return the new root. Absent removes and present adds are ignored.
  std::optional<Digest> apply(const std::optional<Digest>& root, const std::vector<Bytes>& adds,
                              const std::vector<Bytes>& removes);
  /// Visit entries with lo <= key < hi (hi nullopt is unbounded), ascending or descending.
  /// `visit` returns false to stop early.
  void range(const std::optional<Digest>& root, const Bytes& lo, const std::optional<Bytes>& hi, bool reverse,
             const std::function<bool(const Bytes&)>& visit);
  /// Every entry in key order.
  void entries(const std::optional<Digest>& root, const std::function<bool(const Bytes&)>& visit) {
    range(root, {}, std::nullopt, false, visit);
  }

 private:
  struct Node;
  struct Ref {
    Digest digest;
    std::uint64_t bytes = 0;
    std::shared_ptr<Node> draft;  // present until written
    Bytes encoded;                // encoded form of the draft
  };
  struct ChildRef {
    int nibble;
    Ref ref;
  };
  struct Node {
    bool leaf = true;
    std::vector<Bytes> keys;         // leaf
    std::vector<int> skip;           // branch
    std::vector<ChildRef> children;  // branch, ascending nibble
  };

  std::shared_ptr<Node> loadNode(const Ref& ref, const std::vector<int>& prefix);
  std::shared_ptr<Node> readNode(const Digest& digest, const std::vector<int>& prefix, std::optional<std::uint64_t> expected,
                                 std::uint64_t* bytes);
  void collect(const Ref& ref, const std::vector<int>& prefix, std::vector<Bytes>& out);
  Ref makeLeaf(std::vector<Bytes> keys);
  Ref makeBranch(std::vector<int> skip, std::vector<ChildRef> children);
  std::optional<Ref> build(std::vector<Bytes> keys, std::size_t depth);
  void writeDraft(const Ref& ref);
  std::optional<Ref> update(const std::optional<Ref>& ref, const std::vector<int>& prefix, std::vector<Bytes> adds,
                            std::vector<Bytes> removes);
  std::optional<Ref> finalize(const std::vector<int>& prefix, const std::vector<int>& skip,
                              std::map<int, Ref> children);
  bool walk(const Digest& digest, const std::vector<int>& prefix, std::optional<std::uint64_t> expected,
            const Bytes& lo, const std::optional<Bytes>& hi, const std::vector<int>& lo_n,
            const std::optional<std::vector<int>>& hi_n, bool reverse, const std::function<bool(const Bytes&)>& visit);

  BlockStore& blocks_;
};

}  // namespace brightchain::v2
