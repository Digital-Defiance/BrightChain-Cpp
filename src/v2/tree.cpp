#include "brightchain/v2/tree.hpp"

#include <algorithm>
#include <map>

namespace brightchain::v2 {

namespace {
constexpr std::uint64_t kLeafBudget = kMaxMetadataBytes - kLeafHeaderBytes;
constexpr std::size_t kRouteLength = 128;

int nibbleAt(const std::string& route, std::size_t depth) {
  char c = route[depth];
  return c <= '9' ? c - '0' : c - 'a' + 10;
}
}  // namespace

Metadata Tree::readPage(const Digest& digest) {
  auto page = decodeMetadata(blocks_.get(digest), digest);
  if (const auto* leaf = std::get_if<Leaf>(&page)) {
    if (leaf->kind != kind_) throw CodecError("Page kind does not match its tree");
  } else if (!std::holds_alternative<Branch>(page)) {
    throw CodecError("Tree reference does not name a page");
  }
  return page;
}

std::optional<Record> Tree::get(const std::optional<Digest>& root, const std::string& key) {
  auto route = digestPayload(encodeKey(key));
  std::optional<Digest> digest = root;
  std::size_t depth = 0;
  std::optional<std::uint64_t> expected;
  while (digest) {
    auto page = readPage(*digest);
    if (const auto* leaf = std::get_if<Leaf>(&page)) {
      std::uint64_t bytes = 0;
      for (const auto& r : leaf->records) bytes += recordByteLength(r);
      if (expected && *expected != bytes) throw CodecError("Leaf record bytes disagree with its parent");
      for (const auto& r : leaf->records) {
        if (r.key == key) return r;
      }
      return std::nullopt;
    }
    const auto& branch = std::get<Branch>(page);
    if (depth >= kRouteLength) throw CodecError("A branch at depth 128 is invalid");
    int nib = nibbleAt(route, depth);
    const BranchChild* child = nullptr;
    for (const auto& c : branch.children) if (c.nibble == nib) child = &c;
    if (!child) return std::nullopt;
    digest = child->digest;
    expected = child->recordBytes;
    depth++;
  }
  return std::nullopt;
}

Tree::Ref Tree::write(const Metadata& page, std::uint64_t recordBytes) {
  auto bytes = encodeMetadata(page);
  auto digest = digestPayload(bytes);
  blocks_.put(digest, bytes);
  return {digest, recordBytes};
}

std::optional<Tree::Ref> Tree::build(std::vector<Routed> records, std::size_t depth) {
  if (records.empty()) return std::nullopt;
  std::uint64_t total = 0;
  for (const auto& r : records) total += r.bytes;
  if (total <= kLeafBudget) {
    Leaf leaf;
    leaf.kind = kind_;
    for (auto& r : records) leaf.records.push_back(std::move(r.record));
    return write(leaf, total);
  }
  if (depth >= kRouteLength) throw CodecError("Hash-collision capacity exceeded");
  std::map<int, std::vector<Routed>> groups;
  for (auto& r : records) groups[nibbleAt(r.route, depth)].push_back(std::move(r));
  Branch branch;
  for (auto& [nib, group] : groups) {
    auto child = build(std::move(group), depth + 1);
    branch.children.push_back({nib, child->recordBytes, child->digest});
  }
  return write(branch, total);
}

void Tree::collect(const Digest& digest, std::vector<Record>& out) {
  auto page = readPage(digest);
  if (const auto* leaf = std::get_if<Leaf>(&page)) {
    out.insert(out.end(), leaf->records.begin(), leaf->records.end());
    return;
  }
  for (const auto& c : std::get<Branch>(page).children) collect(c.digest, out);
}

std::optional<Tree::Ref> Tree::update(const std::optional<Ref>& ref, std::size_t depth,
                                      std::vector<Routed> updates) {
  if (updates.empty()) return ref;
  if (!ref) return build(std::move(updates), depth);

  auto page = readPage(ref->digest);
  if (const auto* leaf = std::get_if<Leaf>(&page)) {
    std::map<std::string, Routed> merged;
    for (const auto& r : leaf->records) {
      auto key = r.key;
      merged.emplace(key, Routed{r, digestPayload(encodeKey(r.key)), recordByteLength(r)});
    }
    for (auto& u : updates) {
      auto key = u.record.key;
      merged.insert_or_assign(key, std::move(u));
    }
    std::vector<Routed> all;
    for (auto& [_, r] : merged) all.push_back(std::move(r));
    return build(std::move(all), depth);
  }

  const auto& branch = std::get<Branch>(page);
  if (depth >= kRouteLength) throw CodecError("A branch at depth 128 is invalid");
  std::map<int, std::vector<Routed>> groups;
  for (auto& u : updates) groups[nibbleAt(u.route, depth)].push_back(std::move(u));

  std::map<int, Ref> children;
  for (const auto& c : branch.children) children[c.nibble] = {c.digest, c.recordBytes};
  for (auto& [nib, group] : groups) {
    std::optional<Ref> existing;
    if (auto it = children.find(nib); it != children.end()) existing = it->second;
    auto next = update(existing, depth + 1, std::move(group));
    if (next) children[nib] = *next; else children.erase(nib);
  }

  std::uint64_t total = 0;
  for (const auto& [_, c] : children) total += c.recordBytes;
  if (children.empty()) return std::nullopt;
  if (total <= kLeafBudget) {
    // The subtree now fits one leaf, so it must collapse.
    std::vector<Record> records;
    for (const auto& [_, c] : children) collect(c.digest, records);
    std::vector<Routed> all;
    for (auto& r : records) {
      auto route = digestPayload(encodeKey(r.key));
      auto bytes = recordByteLength(r);
      all.push_back({std::move(r), std::move(route), bytes});
    }
    return build(std::move(all), depth);
  }
  Branch rebuilt;
  for (const auto& [nib, c] : children) rebuilt.children.push_back({nib, c.recordBytes, c.digest});
  return write(rebuilt, total);
}

std::optional<Digest> Tree::setMany(const std::optional<Digest>& root, const std::vector<Record>& records) {
  if (records.empty()) return root;
  std::map<std::string, Routed> latest;
  for (const auto& r : records) {
    latest.insert_or_assign(r.key, Routed{r, digestPayload(encodeKey(r.key)), recordByteLength(r)});
  }
  std::vector<Routed> updates;
  for (auto& [_, r] : latest) updates.push_back(std::move(r));

  std::optional<Ref> current;
  if (root) {
    auto page = readPage(*root);
    std::uint64_t bytes = 0;
    if (const auto* leaf = std::get_if<Leaf>(&page)) {
      for (const auto& r : leaf->records) bytes += recordByteLength(r);
    } else {
      for (const auto& c : std::get<Branch>(page).children) bytes += c.recordBytes;
    }
    current = Ref{*root, bytes};
  }
  auto next = update(current, 0, std::move(updates));
  return next ? std::optional<Digest>(next->digest) : std::nullopt;
}

namespace {
bool sameRecord(const Record& a, const Record& b) {
  return compareVersions(a.version, b.version) == 0 && a.target == b.target;
}
}  // namespace

void Tree::diff(const std::optional<Digest>& from, const std::optional<Digest>& to,
                const std::function<void(const RecordChange&)>& visit) {
  diffPages(from, to, visit);
}

void Tree::diffPages(const std::optional<Digest>& from, const std::optional<Digest>& to,
                     const std::function<void(const RecordChange&)>& visit) {
  if (from == to) return;
  if (!from || !to) {
    std::vector<Record> records;
    collect(from ? *from : *to, records);
    for (auto& r : records) {
      if (from) visit({r.key, r, std::nullopt});
      else visit({r.key, std::nullopt, r});
    }
    return;
  }
  auto left = readPage(*from);
  auto right = readPage(*to);
  const auto* lb = std::get_if<Branch>(&left);
  const auto* rb = std::get_if<Branch>(&right);
  if (lb && rb) {
    for (int nibble = 0; nibble < 16; nibble++) {
      std::optional<Digest> a, b;
      for (const auto& c : lb->children) if (c.nibble == nibble) a = c.digest;
      for (const auto& c : rb->children) if (c.nibble == nibble) b = c.digest;
      if (a || b) diffPages(a, b, visit);
    }
    return;
  }
  // One side is a leaf: compare the records under this prefix by key.
  std::map<std::string, Record> before, after;
  std::vector<Record> records;
  collect(*from, records);
  for (auto& r : records) before.emplace(r.key, r);
  records.clear();
  collect(*to, records);
  for (auto& r : records) after.emplace(r.key, r);
  std::vector<std::string> keys;
  for (const auto& [k, _] : before) keys.push_back(k);
  for (const auto& [k, _] : after) if (!before.count(k)) keys.push_back(k);
  std::sort(keys.begin(), keys.end(), [](const std::string& a, const std::string& b) { return compareKeys(a, b) < 0; });
  for (const auto& key : keys) {
    auto a = before.find(key);
    auto b = after.find(key);
    if (a != before.end() && b != after.end() && sameRecord(a->second, b->second)) continue;
    visit({key, a == before.end() ? std::nullopt : std::optional<Record>(a->second),
           b == after.end() ? std::nullopt : std::optional<Record>(b->second)});
  }
}

void Tree::forEach(const std::optional<Digest>& root, const std::function<void(const Record&)>& visit) {
  if (!root) return;
  auto page = readPage(*root);
  if (const auto* leaf = std::get_if<Leaf>(&page)) {
    for (const auto& r : leaf->records) visit(r);
    return;
  }
  for (const auto& c : std::get<Branch>(page).children) forEach(c.digest, visit);
}

}  // namespace brightchain::v2
