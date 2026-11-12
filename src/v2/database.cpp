#include "brightchain/v2/database.hpp"

#include <openssl/rand.h>

namespace brightchain::v2 {

namespace {

/// Read-only adapter over the disk store.
struct ReadBlocks : BlockStore {
  explicit ReadBlocks(const DiskStore& s) : store(s) {}
  Bytes get(const Digest& d) override { return store.readBlock(d); }
  void put(const Digest&, const Bytes&) override { throw StorageError("Cannot write a BrightDB read snapshot"); }
  const DiskStore& store;
};

struct WriteBlocks : BlockStore {
  WriteBlocks(const DiskStore& s, DiskStore::Writer& w) : store(s), writer(w) {}
  Bytes get(const Digest& d) override { return store.readBlock(d); }
  void put(const Digest& d, const Bytes& payload) override {
    if (writer.writeBlock(payload) != d) throw StorageError("BrightDB block writer returned a different digest");
  }
  const DiskStore& store;
  DiskStore::Writer& writer;
};

DatabaseRoot readRootFrom(const DiskStore& store) {
  auto meta = decodeMetadata(store.readBlock(store.readHead()));
  const auto* root = std::get_if<DatabaseRoot>(&meta);
  if (!root) throw StorageError("BrightDB HEAD does not reference a database root");
  return *root;
}

std::optional<CollectionRoot> readCollection(const DiskStore& store, Tree& catalog, const DatabaseRoot& root,
                                             const std::string& name) {
  auto record = catalog.get(root.catalog, name);
  if (!record) return std::nullopt;
  if (record->version.counter > root.clock) throw StorageError("BrightDB catalog version exceeds the database clock");
  if (!record->target) return std::nullopt;
  auto meta = decodeMetadata(store.readBlock(*record->target));
  const auto* c = std::get_if<CollectionRoot>(&meta);
  if (!c || compareVersions(c->generation, record->version) != 0)
    throw StorageError("BrightDB collection generation does not match its catalog entry");
  return *c;
}

void checkPrimaryVersion(const Record& r, const Version& generation, std::uint64_t clock) {
  if (r.version.counter > clock || compareVersions(r.version, generation) <= 0)
    throw StorageError("BrightDB document version is outside its collection generation");
}

std::string randomId() {
  unsigned char r[16];
  RAND_bytes(r, sizeof r);
  return toHex(Bytes(r, r + sizeof r));
}

struct Pending {
  bool live = false;
  bool created = false;
  CollectionRoot root;
  std::map<std::string, Record> changes;
  std::optional<Version> dropVersion;
};

/// Stages catalog and primary changes against one pinned database root.
class WriteBatch {
 public:
  WriteBatch(const DiskStore& store, DiskStore::Writer& writer, DatabaseRoot root)
      : store_(store), writer_(writer), root_(std::move(root)), blocks_(store, writer),
        catalog_(blocks_, LeafKind::Catalog), primary_(blocks_, LeafKind::Primary), clock_(root_.clock) {}

  void createCollection(const std::string& name) {
    if (collection(name).live) throw std::runtime_error("BrightDB collection already exists: " + name);
    addCollection(name);
  }
  void dropCollection(const std::string& name) {
    auto& p = collection(name);
    if (p.live) {
      Pending dropped;
      dropped.dropVersion = nextVersion();
      pending_[name] = dropped;
    }
  }
  std::string insert(const std::string& name, const nlohmann::json& doc) {
    auto encoded = encodeDocumentForWrite(doc, randomId());
    auto& existing = collection(name);
    auto& coll = existing.live ? existing : addCollection(name);
    auto record = document(coll, encoded.key);
    if (record && record->target) throw DuplicateKeyError(encoded.key);
    stage(coll, encoded.key, digestPayload(encoded.payload), encoded.payload);
    return encoded.key;
  }
  ReplaceResult replace(const std::string& name, const std::string& id, const nlohmann::json& doc) {
    encodeKey(id);
    auto& coll = collection(name);
    std::optional<Record> record;
    if (coll.live) record = document(coll, id);
    if (!coll.live || !record || !record->target) return {};
    auto encoded = encodeDocumentForWrite(doc, id, id);
    auto target = digestPayload(encoded.payload);
    bool changed = target != *record->target;
    if (changed) stage(coll, id, target, encoded.payload);
    return {1, changed ? 1 : 0};
  }
  int remove(const std::string& name, const std::string& id) {
    encodeKey(id);
    auto& coll = collection(name);
    if (!coll.live) return 0;
    auto record = document(coll, id);
    if (!record || !record->target) return 0;
    coll.changes[id] = Record{id, nextVersion(), std::nullopt};
    return 1;
  }

  /// Returns the new database root digest, or nullopt when nothing changed.
  std::optional<Digest> persist() {
    for (const auto& [digest, payload] : documents_) {
      (void)digest;
      writer_.writeBlock(payload);
    }
    std::vector<Record> catalogChanges;
    for (auto& [name, p] : pending_) {
      if (!p.live) {
        if (p.dropVersion) catalogChanges.push_back({name, *p.dropVersion, std::nullopt});
        continue;
      }
      if (!p.created && p.changes.empty()) continue;
      std::vector<Record> records;
      for (auto& [_, r] : p.changes) records.push_back(r);
      auto primary = primary_.setMany(p.root.primary, records);
      CollectionRoot updated{p.root.generation, primary};
      auto target = writer_.writeBlock(encodeMetadata(updated));
      catalogChanges.push_back({name, p.root.generation, target});
    }
    if (catalogChanges.empty()) return std::nullopt;
    auto catalog = catalog_.setMany(root_.catalog, catalogChanges);
    DatabaseRoot next{root_.databaseId, clock_, catalog};
    return writer_.writeBlock(encodeMetadata(next));
  }

 private:
  Version nextVersion() {
    if (clock_ == UINT64_MAX) throw StorageError("BrightDB database clock exhausted");
    return Version{++clock_, store_.writerId()};
  }
  Pending& collection(const std::string& name) {
    if (auto it = pending_.find(name); it != pending_.end()) return it->second;
    ReadBlocks reader(store_);
    Tree readCatalog(reader, LeafKind::Catalog);
    auto root = readCollection(store_, readCatalog, root_, name);
    Pending p;
    if (root) { p.live = true; p.root = *root; }
    return pending_.emplace(name, std::move(p)).first->second;
  }
  Pending& addCollection(const std::string& name) {
    Pending p;
    p.live = true;
    p.created = true;
    p.root = CollectionRoot{nextVersion(), std::nullopt};
    pending_[name] = std::move(p);
    return pending_[name];
  }
  std::optional<Record> document(Pending& coll, const std::string& id) {
    if (auto it = coll.changes.find(id); it != coll.changes.end()) return it->second;
    auto existing = primary_.get(coll.root.primary, id);
    if (existing) checkPrimaryVersion(*existing, coll.root.generation, root_.clock);
    return existing;
  }
  void stage(Pending& coll, const std::string& id, const Digest& target, const Bytes& payload) {
    coll.changes[id] = Record{id, nextVersion(), target};
    documents_[target] = payload;
  }

  const DiskStore& store_;
  DiskStore::Writer& writer_;
  DatabaseRoot root_;
  WriteBlocks blocks_;
  Tree catalog_;
  Tree primary_;
  std::uint64_t clock_;
  std::map<std::string, Pending> pending_;
  std::map<Digest, Bytes> documents_;
};

}  // namespace

Database::Database(DiskStore store) : store_(std::move(store)) {}

Database Database::open(const std::string& path) {
  DatabaseRoot empty{toHex(fromHex(randomId())), 0, std::nullopt};
  auto store = DiskStore::openOrCreate(path, encodeMetadata(empty));
  readRootFrom(store);
  return Database(std::move(store));
}

DatabaseRoot Database::readRoot() const { return readRootFrom(store_); }

std::vector<std::string> Database::listCollections() {
  auto root = readRoot();
  ReadBlocks reader(store_);
  Tree catalog(reader, LeafKind::Catalog);
  std::vector<std::string> names;
  catalog.forEach(root.catalog, [&](const Record& r) {
    if (r.version.counter > root.clock) throw StorageError("BrightDB catalog version exceeds the database clock");
    if (r.target) names.push_back(r.key);
  });
  return names;
}

std::optional<nlohmann::json> Database::findById(const std::string& collection, const std::string& id) {
  encodeKey(id);
  auto root = readRoot();
  ReadBlocks reader(store_);
  Tree catalog(reader, LeafKind::Catalog), primary(reader, LeafKind::Primary);
  auto coll = readCollection(store_, catalog, root, collection);
  if (!coll) return std::nullopt;
  auto record = primary.get(coll->primary, id);
  if (!record) return std::nullopt;
  checkPrimaryVersion(*record, coll->generation, root.clock);
  if (!record->target) return std::nullopt;
  return decodeDocument(store_.readBlock(*record->target), record->key, record->target);
}

std::vector<nlohmann::json> Database::findAll(const std::string& collection) {
  auto root = readRoot();
  ReadBlocks reader(store_);
  Tree catalog(reader, LeafKind::Catalog), primary(reader, LeafKind::Primary);
  std::vector<nlohmann::json> out;
  auto coll = readCollection(store_, catalog, root, collection);
  if (!coll) return out;
  primary.forEach(coll->primary, [&](const Record& r) {
    checkPrimaryVersion(r, coll->generation, root.clock);
    if (r.target) out.push_back(decodeDocument(store_.readBlock(*r.target), r.key, r.target));
  });
  return out;
}

void Database::writeBatch(const std::vector<WriteOp>& operations) {
  store_.withWriteLock([&](DiskStore::Writer& writer) {
    WriteBatch batch(store_, writer, readRootFrom(store_));
    for (const auto& op : operations) {
      encodeKey(op.collection);
      switch (op.type) {
        case WriteOp::Type::CreateCollection: batch.createCollection(op.collection); break;
        case WriteOp::Type::DropCollection: batch.dropCollection(op.collection); break;
        case WriteOp::Type::Insert: batch.insert(op.collection, op.document); break;
        case WriteOp::Type::Replace: batch.replace(op.collection, op.id, op.document); break;
        case WriteOp::Type::Delete: batch.remove(op.collection, op.id); break;
      }
    }
    if (auto head = batch.persist()) writer.publishHead(*head);
  });
}

std::string Database::insertOne(const std::string& collection, const nlohmann::json& document) {
  std::string id;
  encodeKey(collection);
  store_.withWriteLock([&](DiskStore::Writer& writer) {
    WriteBatch batch(store_, writer, readRootFrom(store_));
    id = batch.insert(collection, document);
    if (auto head = batch.persist()) writer.publishHead(*head);
  });
  return id;
}

std::vector<std::string> Database::insertMany(const std::string& collection, const std::vector<nlohmann::json>& documents) {
  std::vector<std::string> ids;
  encodeKey(collection);
  store_.withWriteLock([&](DiskStore::Writer& writer) {
    WriteBatch batch(store_, writer, readRootFrom(store_));
    for (const auto& d : documents) ids.push_back(batch.insert(collection, d));
    if (auto head = batch.persist()) writer.publishHead(*head);
  });
  return ids;
}

ReplaceResult Database::replaceById(const std::string& collection, const std::string& id, const nlohmann::json& document) {
  ReplaceResult result;
  encodeKey(collection);
  store_.withWriteLock([&](DiskStore::Writer& writer) {
    WriteBatch batch(store_, writer, readRootFrom(store_));
    result = batch.replace(collection, id, document);
    if (auto head = batch.persist()) writer.publishHead(*head);
  });
  return result;
}

int Database::deleteById(const std::string& collection, const std::string& id) {
  int count = 0;
  encodeKey(collection);
  store_.withWriteLock([&](DiskStore::Writer& writer) {
    WriteBatch batch(store_, writer, readRootFrom(store_));
    count = batch.remove(collection, id);
    if (auto head = batch.persist()) writer.publishHead(*head);
  });
  return count;
}

}  // namespace brightchain::v2
