#include "brightchain/v2/index_tree.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "brightchain/v2/index_key.hpp"

namespace brightchain::v2 {

namespace {

constexpr std::uint64_t kLeafBudget = kMaxMetadataBytes - kLeafHeaderBytes;

int nibbleAt(const Bytes& key, std::size_t i) { return i % 2 == 0 ? key[i >> 1] >> 4 : key[i >> 1] & 15; }
std::size_t nibbleLength(const Bytes& key) { return key.size() * 2; }

int compareNibbles(const std::vector<int>& a, const std::vector<int>& b) {
  auto n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

std::vector<int> toNibbles(const Bytes& bytes) {
  std::vector<int> out;
  for (std::size_t i = 0; i < bytes.size() * 2; i++) out.push_back(nibbleAt(bytes, i));
  return out;
}

std::uint64_t leafBytes(const std::vector<Bytes>& keys) {
  std::uint64_t total = 0;
  for (const auto& k : keys) total += indexEntryBytes(k);
  return total;
}

std::vector<int> concat(const std::vector<int>& a, const std::vector<int>& b) {
  auto out = a;
  out.insert(out.end(), b.begin(), b.end());
  return out;
}

std::vector<Bytes> sortedUnique(std::vector<Bytes> keys) {
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  return keys;
}

}  // namespace

std::shared_ptr<IndexTree::Node> IndexTree::readNode(const Digest& digest, const std::vector<int>& prefix,
                                                     std::optional<std::uint64_t> expected, std::uint64_t* bytesOut) {
  auto page = decodeMetadata(blocks_.get(digest), digest);
  auto node = std::make_shared<Node>();
  std::uint64_t bytes = 0;
  if (const auto* leaf = std::get_if<IndexLeaf>(&page)) {
    node->leaf = true;
    node->keys = leaf->entries;
    bytes = leafBytes(node->keys);
    for (const auto& key : node->keys) {
      auto nibbles = toNibbles(key);
      for (std::size_t i = 0; i < prefix.size(); i++)
        if (i >= nibbles.size() || nibbles[i] != prefix[i]) throw CodecError("BrightDB v2 index entry is on the wrong path");
    }
  } else if (const auto* branch = std::get_if<IndexBranch>(&page)) {
    node->leaf = false;
    node->skip = branch->skip;
    for (const auto& c : branch->children) {
      Ref ref;
      ref.digest = c.digest;
      ref.bytes = c.recordBytes;
      node->children.push_back({c.nibble, std::move(ref)});
      bytes += c.recordBytes;
    }
  } else {
    throw CodecError("BrightDB v2 index tree expected an index leaf or branch");
  }
  if (expected && *expected != bytes) throw CodecError("BrightDB v2 index child byte count does not match its page");
  if (bytesOut) *bytesOut = bytes;
  return node;
}

std::shared_ptr<IndexTree::Node> IndexTree::loadNode(const Ref& ref, const std::vector<int>& prefix) {
  if (ref.draft) return ref.draft;
  return readNode(ref.digest, prefix, ref.bytes, nullptr);
}

void IndexTree::collect(const Ref& ref, const std::vector<int>& prefix, std::vector<Bytes>& out) {
  auto node = loadNode(ref, prefix);
  if (node->leaf) {
    out.insert(out.end(), node->keys.begin(), node->keys.end());
    return;
  }
  auto path = concat(prefix, node->skip);
  for (const auto& c : node->children) {
    auto childPrefix = path;
    childPrefix.push_back(c.nibble);
    collect(c.ref, childPrefix, out);
  }
}

IndexTree::Ref IndexTree::makeLeaf(std::vector<Bytes> keys) {
  Ref ref;
  ref.encoded = encodeMetadata(IndexLeaf{keys});
  ref.digest = digestPayload(ref.encoded);
  ref.bytes = leafBytes(keys);
  ref.draft = std::make_shared<Node>();
  ref.draft->leaf = true;
  ref.draft->keys = std::move(keys);
  return ref;
}

IndexTree::Ref IndexTree::makeBranch(std::vector<int> skip, std::vector<ChildRef> children) {
  std::sort(children.begin(), children.end(), [](const ChildRef& a, const ChildRef& b) { return a.nibble < b.nibble; });
  IndexBranch page;
  page.skip = skip;
  std::uint64_t total = 0;
  for (const auto& c : children) {
    page.children.push_back({c.nibble, c.ref.bytes, c.ref.digest});
    total += c.ref.bytes;
  }
  Ref ref;
  ref.encoded = encodeMetadata(page);
  ref.digest = digestPayload(ref.encoded);
  ref.bytes = total;
  ref.draft = std::make_shared<Node>();
  ref.draft->leaf = false;
  ref.draft->skip = std::move(skip);
  ref.draft->children = std::move(children);
  return ref;
}

std::optional<IndexTree::Ref> IndexTree::build(std::vector<Bytes> keys, std::size_t depth) {
  if (keys.empty()) return std::nullopt;
  if (leafBytes(keys) <= kLeafBudget) return makeLeaf(std::move(keys));

  const auto& first = keys.front();
  const auto& last = keys.back();
  std::size_t shared = 0;
  while (depth + shared < nibbleLength(first) && depth + shared < nibbleLength(last) &&
         nibbleAt(first, depth + shared) == nibbleAt(last, depth + shared))
    shared++;
  auto split = depth + shared;
  std::vector<int> skip;
  for (auto i = depth; i < split; i++) skip.push_back(nibbleAt(first, i));

  std::map<int, std::vector<Bytes>> groups;
  for (auto& key : keys) {
    if (nibbleLength(key) <= split) throw IndexError("An index key cannot be a prefix of another");
    groups[nibbleAt(key, split)].push_back(std::move(key));
  }
  std::vector<ChildRef> children;
  for (auto& [nibble, group] : groups) children.push_back({nibble, *build(std::move(group), split + 1)});
  return makeBranch(std::move(skip), std::move(children));
}

void IndexTree::writeDraft(const Ref& ref) {
  if (!ref.draft) return;
  if (!ref.draft->leaf) for (const auto& c : ref.draft->children) writeDraft(c.ref);
  blocks_.put(ref.digest, ref.encoded);
}

std::optional<IndexTree::Ref> IndexTree::finalize(const std::vector<int>& prefix, const std::vector<int>& skip,
                                                  std::map<int, Ref> children) {
  if (children.empty()) return std::nullopt;
  std::uint64_t total = 0;
  for (const auto& [_, ref] : children) total += ref.bytes;
  auto path = concat(prefix, skip);

  if (total <= kLeafBudget) {
    std::vector<Bytes> keys;
    for (const auto& [nibble, ref] : children) {
      auto childPrefix = path;
      childPrefix.push_back(nibble);
      collect(ref, childPrefix, keys);
    }
    return build(std::move(keys), prefix.size());
  }
  if (children.size() == 1) {
    const auto& [nibble, ref] = *children.begin();
    auto childPrefix = path;
    childPrefix.push_back(nibble);
    auto child = loadNode(ref, childPrefix);
    if (child->leaf) throw CodecError("BrightDB v2 index leaf exceeds the leaf budget");
    auto merged = skip;
    merged.push_back(nibble);
    merged.insert(merged.end(), child->skip.begin(), child->skip.end());
    return makeBranch(std::move(merged), child->children);
  }
  std::vector<ChildRef> entries;
  for (auto& [nibble, ref] : children) entries.push_back({nibble, ref});
  return makeBranch(skip, std::move(entries));
}

std::optional<IndexTree::Ref> IndexTree::update(const std::optional<Ref>& ref, const std::vector<int>& prefix,
                                                std::vector<Bytes> adds, std::vector<Bytes> removes) {
  if (adds.empty() && removes.empty()) return ref;
  auto depth = prefix.size();
  if (!ref) return build(std::move(adds), depth);

  auto node = loadNode(*ref, prefix);
  if (node->leaf) {
    std::set<Bytes> keys(node->keys.begin(), node->keys.end());
    for (const auto& k : removes) keys.erase(k);
    for (const auto& k : adds) keys.insert(k);
    return build(std::vector<Bytes>(keys.begin(), keys.end()), depth);
  }

  auto skipLength = node->skip.size();
  auto mismatch = [&](const Bytes& key) -> std::size_t {
    for (std::size_t i = 0; i < skipLength; i++)
      if (depth + i >= nibbleLength(key) || nibbleAt(key, depth + i) != node->skip[i]) return i;
    return skipLength;
  };

  std::vector<Bytes> diverging;
  for (const auto& k : adds) if (mismatch(k) < skipLength) diverging.push_back(k);
  std::map<int, Ref> children;
  for (const auto& c : node->children) children[c.nibble] = c.ref;

  if (diverging.empty()) {
    auto split = depth + skipLength;
    auto path = concat(prefix, node->skip);
    struct Group { std::vector<Bytes> adds, removes; };
    std::map<int, Group> groups;
    auto place = [&](const Bytes& key, bool isAdd) {
      if (nibbleLength(key) <= split) throw IndexError("An index key cannot be a prefix of another");
      auto& g = groups[nibbleAt(key, split)];
      (isAdd ? g.adds : g.removes).push_back(key);
    };
    for (const auto& k : adds) place(k, true);
    // A remove that leaves this branch's prefix cannot be present here.
    for (const auto& k : removes) if (mismatch(k) == skipLength) place(k, false);
    for (auto& [nibble, group] : groups) {
      std::optional<Ref> existing;
      if (auto it = children.find(nibble); it != children.end()) existing = it->second;
      auto childPrefix = path;
      childPrefix.push_back(nibble);
      auto next = update(existing, childPrefix, std::move(group.adds), std::move(group.removes));
      if (next) children[nibble] = *next; else children.erase(nibble);
    }
    return finalize(prefix, node->skip, std::move(children));
  }

  // A new key leaves the shared prefix at position j: split the branch there.
  std::size_t j = skipLength;
  for (const auto& k : diverging) j = std::min(j, mismatch(k));
  auto split = depth + j;
  int oldNibble = node->skip[j];
  auto stillInPrefix = [&](const Bytes& key) {
    for (std::size_t i = 0; i < j; i++)
      if (nibbleAt(key, depth + i) != node->skip[i]) return false;
    return true;
  };
  std::vector<Bytes> oldAdds, oldRemoves;
  std::map<int, std::vector<Bytes>> others;
  for (const auto& key : adds) {
    if (nibbleLength(key) <= split) throw IndexError("An index key cannot be a prefix of another");
    int nibble = nibbleAt(key, split);
    if (nibble == oldNibble) oldAdds.push_back(key); else others[nibble].push_back(key);
  }
  for (const auto& key : removes)
    if (nibbleLength(key) > split && stillInPrefix(key) && nibbleAt(key, split) == oldNibble) oldRemoves.push_back(key);

  std::vector<int> reducedSkip(node->skip.begin() + std::ptrdiff_t(j) + 1, node->skip.end());
  std::vector<ChildRef> oldChildren(node->children);
  auto reduced = makeBranch(reducedSkip, std::move(oldChildren));
  auto oldPath = prefix;
  oldPath.insert(oldPath.end(), node->skip.begin(), node->skip.begin() + std::ptrdiff_t(j));
  oldPath.push_back(oldNibble);
  auto oldResult = update(reduced, oldPath, std::move(oldAdds), std::move(oldRemoves));
  std::map<int, Ref> merged;
  if (oldResult) merged[oldNibble] = *oldResult;
  for (auto& [nibble, group] : others) {
    auto built = build(std::move(group), split + 1);
    if (built) merged[nibble] = *built;
  }
  std::vector<int> topSkip(node->skip.begin(), node->skip.begin() + std::ptrdiff_t(j));
  return finalize(prefix, topSkip, std::move(merged));
}

std::optional<Digest> IndexTree::apply(const std::optional<Digest>& root, const std::vector<Bytes>& adds,
                                       const std::vector<Bytes>& removes) {
  std::set<Bytes> addSet(adds.begin(), adds.end());
  std::vector<Bytes> effectiveRemoves;
  for (const auto& k : removes) if (!addSet.count(k)) effectiveRemoves.push_back(k);
  std::optional<Ref> current;
  if (root) {
    std::uint64_t bytes = 0;
    readNode(*root, {}, std::nullopt, &bytes);
    Ref ref;
    ref.digest = *root;
    ref.bytes = bytes;
    current = ref;
  }
  auto next = update(current, {}, sortedUnique(std::vector<Bytes>(addSet.begin(), addSet.end())),
                     sortedUnique(std::move(effectiveRemoves)));
  if (next) writeDraft(*next);
  return next ? std::optional<Digest>(next->digest) : std::nullopt;
}

bool IndexTree::walk(const Digest& digest, const std::vector<int>& prefix, std::optional<std::uint64_t> expected,
                     const Bytes& lo, const std::optional<Bytes>& hi, const std::vector<int>& loN,
                     const std::optional<std::vector<int>>& hiN, bool reverse,
                     const std::function<bool(const Bytes&)>& visit) {
  auto node = readNode(digest, prefix, expected, nullptr);
  if (node->leaf) {
    auto emit = [&](const Bytes& key) {
      if (!lo.empty() && key < lo) return true;
      if (hi && !(key < *hi)) return true;
      return visit(key);
    };
    if (reverse) {
      for (auto it = node->keys.rbegin(); it != node->keys.rend(); ++it) if (!emit(*it)) return false;
    } else {
      for (const auto& key : node->keys) if (!emit(key)) return false;
    }
    return true;
  }
  auto path = concat(prefix, node->skip);
  auto mayContain = [&](const std::vector<int>& q) {
    if (hiN && compareNibbles(q, *hiN) >= 0) return false;
    if (!loN.empty()) {
      std::vector<int> bound(loN.begin(), loN.begin() + std::ptrdiff_t(std::min(q.size(), loN.size())));
      if (compareNibbles(q, bound) < 0) return false;
    }
    return true;
  };
  auto visitChild = [&](const ChildRef& c) {
    auto childPrefix = path;
    childPrefix.push_back(c.nibble);
    if (!mayContain(childPrefix)) return true;
    return walk(c.ref.digest, childPrefix, c.ref.bytes, lo, hi, loN, hiN, reverse, visit);
  };
  if (reverse) {
    for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) if (!visitChild(*it)) return false;
  } else {
    for (const auto& c : node->children) if (!visitChild(c)) return false;
  }
  return true;
}

void IndexTree::range(const std::optional<Digest>& root, const Bytes& lo, const std::optional<Bytes>& hi, bool reverse,
                      const std::function<bool(const Bytes&)>& visit) {
  if (!root) return;
  std::vector<int> loN = toNibbles(lo);
  std::optional<std::vector<int>> hiN;
  if (hi) hiN = toNibbles(*hi);
  walk(*root, {}, std::nullopt, lo, hi, loN, hiN, reverse, visit);
}

}  // namespace brightchain::v2
