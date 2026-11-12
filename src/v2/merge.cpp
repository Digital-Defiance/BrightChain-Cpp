#include "brightchain/v2/merge.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "brightchain/v2/index_key.hpp"
#include "brightchain/v2/index_tree.hpp"
#include "brightchain/v2/tree.hpp"

namespace brightchain::v2 {

namespace {

enum class TreeKind { Catalog, Primary };

/// Copies a block graph from one store to another, children before parents.
class ClosureCopier {
 public:
  ClosureCopier(const DiskStore& local, DiskStore::Writer& writer, const BlockSource& remote)
      : local_(local), writer_(writer), remote_(remote) {}

  /// A block already present locally is complete: blocks are always written after their children.
  bool missing(const Digest& d) const { return !local_.hasBlock(d); }

  void ensureDatabase(const Digest& digest) {
    if (!missing(digest)) return;
    auto bytes = remote_.readBlock(digest);
    auto meta = decodeMetadata(bytes, digest);
    const auto* root = std::get_if<DatabaseRoot>(&meta);
    if (!root) throw MergeError("Not a database root");
    if (root->catalog) ensureTree(TreeKind::Catalog, *root->catalog);
    writer_.writeBlock(bytes);
  }

  void ensureTree(TreeKind kind, const Digest& digest) {
    if (!missing(digest)) return;
    auto bytes = remote_.readBlock(digest);
    auto page = decodeMetadata(bytes, digest);
    if (const auto* branch = std::get_if<Branch>(&page)) {
      for (const auto& c : branch->children) ensureTree(kind, c.digest);
    } else if (const auto* leaf = std::get_if<Leaf>(&page);
               leaf && leaf->kind == (kind == TreeKind::Catalog ? LeafKind::Catalog : LeafKind::Primary)) {
      for (const auto& r : leaf->records) {
        if (!r.target) continue;
        if (kind == TreeKind::Catalog) ensureCollection(*r.target); else ensureDocument(*r.target);
      }
    } else {
      throw MergeError("Unexpected tree page");
    }
    writer_.writeBlock(bytes);
  }

  void ensureCollection(const Digest& digest) {
    if (!missing(digest)) return;
    auto bytes = remote_.readBlock(digest);
    auto meta = decodeMetadata(bytes, digest);
    const auto* c = std::get_if<CollectionRoot>(&meta);
    if (!c) throw MergeError("Not a collection root");
    if (c->primary) ensureTree(TreeKind::Primary, *c->primary);
    if (c->indexes) ensureIndexes(*c->indexes);
    writer_.writeBlock(bytes);
  }

  void ensureDocument(const Digest& digest) {
    if (!missing(digest)) return;
    auto bytes = remote_.readBlock(digest);
    auto doc = decodeDocument(bytes, std::nullopt, digest);
    if (doc.contains("_files") && doc.at("_files").is_object())
      for (const auto& entry : doc.at("_files")) ensureBlob(entry.at("digest").get<std::string>());
    writer_.writeBlock(bytes);
  }

 private:
  void ensureIndexes(const Digest& digest) {
    if (!missing(digest)) return;
    auto bytes = remote_.readBlock(digest);
    auto meta = decodeMetadata(bytes, digest);
    const auto* set = std::get_if<IndexSet>(&meta);
    if (!set) throw MergeError("Not an index set");
    for (const auto& d : set->definitions) if (d.root) ensureIndexTree(*d.root);
    writer_.writeBlock(bytes);
  }

  void ensureIndexTree(const Digest& digest) {
    if (!missing(digest)) return;
    auto bytes = remote_.readBlock(digest);
    auto page = decodeMetadata(bytes, digest);
    if (const auto* branch = std::get_if<IndexBranch>(&page)) {
      for (const auto& c : branch->children) ensureIndexTree(c.digest);
    } else if (!std::holds_alternative<IndexLeaf>(page)) {
      throw MergeError("Expected an index page");
    }
    writer_.writeBlock(bytes);
  }

  void ensureBlob(const Digest& digest) {
    if (!missing(digest)) return;
    auto bytes = remote_.readBlock(digest);
    auto meta = decodeMetadata(bytes, digest);
    const auto* blob = std::get_if<Blob>(&meta);
    if (!blob) throw MergeError("Not an attachment manifest");
    for (const auto& entry : blob->entries) {
      if (blob->depth > 0) ensureBlob(entry);
      else if (missing(entry)) writer_.writeBlock(remote_.readBlock(entry));
    }
    writer_.writeBlock(bytes);
  }

  const DiskStore& local_;
  DiskStore::Writer& writer_;
  const BlockSource& remote_;
};

/// Whether the remote record beats the local one: greater version, then tombstone, then greater digest.
bool remoteWins(const Record& local, const Record& remote) {
  int order = compareVersions(remote.version, local.version);
  if (order != 0) return order > 0;
  if (!local.target) return false;
  if (!remote.target) return true;
  return *remote.target > *local.target;
}

DatabaseRoot readDatabaseRoot(const BlockSource& store, const Digest& digest) {
  auto meta = decodeMetadata(store.readBlock(digest), digest);
  const auto* root = std::get_if<DatabaseRoot>(&meta);
  if (!root) throw MergeError("Not a database root");
  return *root;
}

/// Reads pages from whichever store has them and writes only to the local store.
struct UnionBlocks : BlockStore {
  UnionBlocks(const DiskStore& l, DiskStore::Writer& w, const BlockSource& r) : local(l), writer(w), remote(r) {}
  Bytes get(const Digest& d) override { return local.hasBlock(d) ? local.readBlock(d) : remote.readBlock(d); }
  void put(const Digest& d, const Bytes& b) override {
    if (writer.writeBlock(b) != d) throw MergeError("Block writer returned a different digest");
  }
  const DiskStore& local;
  DiskStore::Writer& writer;
  const BlockSource& remote;
};

CollectionRoot readCollectionRoot(BlockStore& blocks, const Digest& digest) {
  auto meta = decodeMetadata(blocks.get(digest), digest);
  const auto* c = std::get_if<CollectionRoot>(&meta);
  if (!c) throw MergeError("Not a collection root");
  return *c;
}

std::vector<IndexDefinition> readDefinitions(BlockStore& blocks, const CollectionRoot& c) {
  if (!c.indexes) return {};
  auto meta = decodeMetadata(blocks.get(*c.indexes), *c.indexes);
  const auto* set = std::get_if<IndexSet>(&meta);
  if (!set) throw MergeError("Not an index set");
  return set->definitions;
}

std::string joinedFields(const std::vector<std::string>& fields) {
  std::string out;
  for (std::size_t i = 0; i < fields.size(); i++) {
    if (i) out.push_back('\0');
    out += fields[i];
  }
  return out;
}

}  // namespace

std::optional<std::pair<Digest, std::size_t>> mergeReplica(const DiskStore& local, DiskStore::Writer& writer,
                                                           const BlockSource& remote, const Digest& remoteHead) {
  auto localHead = local.readHead();
  if (localHead == remoteHead) return std::nullopt;
  auto localRoot = readDatabaseRoot(local, localHead);
  auto remoteRoot = readDatabaseRoot(remote, remoteHead);
  if (localRoot.databaseId != remoteRoot.databaseId) throw MergeError("The replicas belong to different databases");

  UnionBlocks blocks(local, writer, remote);
  Tree catalog(blocks, LeafKind::Catalog);
  Tree primary(blocks, LeafKind::Primary);
  IndexTree indexTree(blocks);
  ClosureCopier copier(local, writer, remote);

  std::size_t adopted = 0;
  std::vector<Record> catalogUpdates;

  auto readDoc = [&](const std::optional<Digest>& digest, const std::string& id) -> std::optional<nlohmann::json> {
    if (!digest) return std::nullopt;
    return decodeDocument(blocks.get(*digest), id);
  };

  auto mergeCollection = [&](const std::string& name, const Record& localRecord, const Record& remoteRecord) {
    auto lc = readCollectionRoot(blocks, *localRecord.target);
    auto rc = readCollectionRoot(blocks, *remoteRecord.target);
    struct Taken { std::optional<Record> before; Record after; };
    std::vector<Taken> taken;
    primary.diff(lc.primary, rc.primary, [&](const RecordChange& change) {
      if (!change.after) return;
      if (change.before && !remoteWins(*change.before, *change.after)) return;
      if (change.after->target) copier.ensureDocument(*change.after->target);
      taken.push_back({change.before, *change.after});
    });
    std::vector<Record> updates;
    for (const auto& t : taken) updates.push_back(t.after);
    auto mergedPrimary = primary.setMany(lc.primary, updates);

    struct Pair { std::optional<IndexDefinition> local, remote; };
    std::map<std::string, Pair> definitions;
    for (auto& d : readDefinitions(blocks, lc)) definitions[d.name].local = d;
    for (auto& d : readDefinitions(blocks, rc)) definitions[d.name].remote = d;

    std::vector<IndexDefinition> merged;
    // std::map orders names bytewise, matching the reference's sorted names.
    for (auto& [indexName, pair] : definitions) {
      bool keepLocal = pair.local && (!pair.remote || joinedFields(pair.local->fields) >= joinedFields(pair.remote->fields));
      if (keepLocal) {
        std::vector<Bytes> adds, removes;
        for (const auto& t : taken) {
          const auto& id = t.after.key;
          auto oldDoc = readDoc(t.before ? t.before->target : std::nullopt, id);
          auto newDoc = readDoc(t.after.target, id);
          std::vector<Bytes> oldKeys, newKeys;
          if (oldDoc) oldKeys = extractIndexKeys(*oldDoc, pair.local->fields, id);
          if (newDoc) newKeys = extractIndexKeys(*newDoc, pair.local->fields, id);
          std::set<Bytes> oldSet(oldKeys.begin(), oldKeys.end()), newSet(newKeys.begin(), newKeys.end());
          for (auto& k : oldKeys) if (!newSet.count(k)) removes.push_back(k);
          for (auto& k : newKeys) if (!oldSet.count(k)) adds.push_back(k);
        }
        IndexDefinition next = *pair.local;
        next.root = indexTree.apply(pair.local->root, adds, removes);
        merged.push_back(std::move(next));
      } else if (pair.remote) {
        // The index exists only on the other side or loses a name conflict: rebuild it.
        std::vector<Bytes> keys;
        primary.forEach(mergedPrimary, [&](const Record& r) {
          if (!r.target) return;
          for (auto& k : extractIndexKeys(*readDoc(r.target, r.key), pair.remote->fields, r.key)) keys.push_back(std::move(k));
        });
        merged.push_back({indexName, pair.remote->fields, indexTree.apply(std::nullopt, keys, {})});
      }
    }
    std::optional<Digest> indexes;
    if (!merged.empty()) indexes = writer.writeBlock(encodeMetadata(IndexSet{merged}));
    auto target = writer.writeBlock(encodeMetadata(CollectionRoot{lc.generation, mergedPrimary, indexes}));
    if (target != localRecord.target) catalogUpdates.push_back({name, localRecord.version, target});
    adopted += taken.size();
  };

  catalog.diff(localRoot.catalog, remoteRoot.catalog, [&](const RecordChange& change) {
    if (!change.after) return;  // only local: keep
    const auto& l = change.before;
    const auto& r = *change.after;
    if (l && compareVersions(r.version, l->version) == 0 && l->target && r.target) {
      mergeCollection(change.key, *l, r);  // same generation: merge the contents
      return;
    }
    if (l && !remoteWins(*l, r)) return;
    if (r.target) copier.ensureCollection(*r.target);
    catalogUpdates.push_back(r);
    adopted++;
  });

  auto clock = std::max(localRoot.clock, remoteRoot.clock);
  if (catalogUpdates.empty() && clock == localRoot.clock) return std::nullopt;
  auto mergedCatalog = catalog.setMany(localRoot.catalog, catalogUpdates);
  auto head = writer.writeBlock(encodeMetadata(DatabaseRoot{localRoot.databaseId, clock, mergedCatalog}));
  if (head == localHead) return std::nullopt;
  return std::make_pair(head, adopted);
}

void copyDatabase(const DiskStore& local, DiskStore::Writer& writer, const BlockSource& remote, const Digest& head) {
  ClosureCopier(local, writer, remote).ensureDatabase(head);
}

}  // namespace brightchain::v2
