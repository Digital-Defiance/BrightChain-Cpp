#include "brightchain/v2/database.hpp"

#include <openssl/rand.h>

#include "brightchain/v2/index_tree.hpp"

#include <algorithm>
#include <set>

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
  /// Index definitions as of this batch; nullopt until first needed.
  std::optional<std::vector<IndexDefinition>> definitions;
  /// Net entry changes per index (hex key -> key), relative to the index root in the base state.
  std::map<std::string, std::map<std::string, Bytes>> indexAdds, indexRemoves;
  bool indexesChanged = false;
};

std::vector<IndexDefinition> readIndexDefinitions(const DiskStore& store, const CollectionRoot& collection) {
  if (!collection.indexes) return {};
  auto meta = decodeMetadata(store.readBlock(*collection.indexes), *collection.indexes);
  const auto* set = std::get_if<IndexSet>(&meta);
  if (!set) throw StorageError("BrightDB collection indexes do not reference an index set");
  return set->definitions;
}

/// Stages catalog and primary changes against one pinned database root.
class WriteBatch {
 public:
  WriteBatch(const DiskStore& store, DiskStore::Writer& writer, DatabaseRoot root)
      : store_(store), writer_(writer), root_(std::move(root)), blocks_(store, writer),
        catalog_(blocks_, LeafKind::Catalog), primary_(blocks_, LeafKind::Primary), indexTree_(blocks_),
        clock_(root_.clock) {}

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
    validateFileReferences(store_, doc);
    auto& existing = collection(name);
    auto& coll = existing.live ? existing : addCollection(name);
    auto record = document(coll, encoded.key);
    if (record && record->target) throw DuplicateKeyError(encoded.key);
    maintainIndexes(coll, encoded.key, std::nullopt, &encoded.payload);
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
    validateFileReferences(store_, doc);
    auto target = digestPayload(encoded.payload);
    bool changed = target != *record->target;
    if (changed) {
      maintainIndexes(coll, id, record->target, &encoded.payload);
      stage(coll, id, target, encoded.payload);
    }
    return {1, changed ? 1 : 0};
  }
  int remove(const std::string& name, const std::string& id) {
    encodeKey(id);
    auto& coll = collection(name);
    if (!coll.live) return 0;
    auto record = document(coll, id);
    if (!record || !record->target) return 0;
    maintainIndexes(coll, id, record->target, nullptr);
    coll.changes[id] = Record{id, nextVersion(), std::nullopt};
    return 1;
  }

  void createIndex(const std::string& name, const std::string& indexName, const std::vector<std::string>& fields) {
    encodeKey(indexName);
    auto& existing = collection(name);
    // Like inserting, creating an index creates a missing collection.
    auto& coll = existing.live ? existing : addCollection(name);
    auto& defs = definitions(coll);
    for (const auto& d : defs) if (d.name == indexName) throw std::runtime_error("BrightDB index already exists: " + indexName);
    // Validates names and field paths before any scan.
    encodeMetadata(IndexSet{{IndexDefinition{indexName, fields, std::nullopt}}});

    std::map<std::string, Bytes> entries;
    auto addDocument = [&](const std::string& id, const nlohmann::json& doc) {
      for (auto& key : extractIndexKeys(doc, fields, id)) entries[toHex(key)] = key;
    };
    std::set<std::string> seen;
    primary_.forEach(coll.root.primary, [&](const Record& r) {
      seen.insert(r.key);
      auto it = coll.changes.find(r.key);
      const Record& effective = it != coll.changes.end() ? it->second : r;
      if (effective.target) addDocument(r.key, decodeDocument(documentBytes(*effective.target), r.key));
    });
    for (const auto& [id, r] : coll.changes)
      if (!seen.count(id) && r.target) addDocument(id, decodeDocument(documentBytes(*r.target), id));
    defs.push_back(IndexDefinition{indexName, fields, std::nullopt});
    coll.indexAdds[indexName] = std::move(entries);
    coll.indexRemoves[indexName] = {};
    coll.indexesChanged = true;
  }

  void dropIndex(const std::string& name, const std::string& indexName) {
    auto& coll = collection(name);
    if (!coll.live) throw std::runtime_error("BrightDB collection does not exist: " + name);
    auto& defs = definitions(coll);
    auto it = std::find_if(defs.begin(), defs.end(), [&](const IndexDefinition& d) { return d.name == indexName; });
    if (it == defs.end()) throw std::runtime_error("BrightDB index does not exist: " + indexName);
    defs.erase(it);
    coll.indexAdds.erase(indexName);
    coll.indexRemoves.erase(indexName);
    coll.indexesChanged = true;
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
      if (!p.created && p.changes.empty() && !p.indexesChanged) continue;
      std::vector<Record> records;
      for (auto& [_, r] : p.changes) records.push_back(r);
      auto primary = primary_.setMany(p.root.primary, records);
      auto indexes = p.root.indexes;
      if (p.indexesChanged) {
        std::vector<IndexDefinition> updatedDefinitions;
        for (const auto& d : p.definitions.value_or(std::vector<IndexDefinition>{})) {
          std::vector<Bytes> adds, removes;
          if (auto it = p.indexAdds.find(d.name); it != p.indexAdds.end()) for (auto& [_, k] : it->second) adds.push_back(k);
          if (auto it = p.indexRemoves.find(d.name); it != p.indexRemoves.end()) for (auto& [_, k] : it->second) removes.push_back(k);
          IndexDefinition next = d;
          next.root = indexTree_.apply(d.root, adds, removes);
          updatedDefinitions.push_back(std::move(next));
        }
        indexes = updatedDefinitions.empty() ? std::nullopt
                                             : std::optional<Digest>(writer_.writeBlock(encodeMetadata(IndexSet{updatedDefinitions})));
      }
      CollectionRoot updated{p.root.generation, primary, indexes};
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
    p.root = CollectionRoot{nextVersion(), std::nullopt, std::nullopt};
    p.definitions = std::vector<IndexDefinition>{};
    pending_[name] = std::move(p);
    return pending_[name];
  }
  Bytes documentBytes(const Digest& digest) {
    if (auto it = documents_.find(digest); it != documents_.end()) return it->second;
    return store_.readBlock(digest);
  }
  std::vector<IndexDefinition>& definitions(Pending& coll) {
    if (!coll.definitions) coll.definitions = readIndexDefinitions(store_, coll.root);
    return *coll.definitions;
  }
  void noteEntry(Pending& coll, const std::string& index, const Bytes& key, bool add) {
    auto hex = toHex(key);
    auto& adds = coll.indexAdds[index];
    auto& removes = coll.indexRemoves[index];
    if (add) {
      // Re-adding an entry removed earlier in this batch cancels the removal.
      if (!removes.erase(hex)) adds[hex] = key;
    } else if (!adds.erase(hex)) {
      removes[hex] = key;
    }
    coll.indexesChanged = true;
  }
  /// Move index entries from a document's old content to its new content.
  void maintainIndexes(Pending& coll, const std::string& id, const std::optional<Digest>& oldTarget, const Bytes* newPayload) {
    auto& defs = definitions(coll);
    if (defs.empty()) return;
    std::optional<nlohmann::json> oldDoc, newDoc;
    if (oldTarget) oldDoc = decodeDocument(documentBytes(*oldTarget), id);
    if (newPayload) newDoc = decodeDocument(*newPayload, id);
    for (const auto& def : defs) {
      std::vector<Bytes> before, after;
      if (oldDoc) before = extractIndexKeys(*oldDoc, def.fields, id);
      if (newDoc) after = extractIndexKeys(*newDoc, def.fields, id);
      std::set<Bytes> beforeSet(before.begin(), before.end()), afterSet(after.begin(), after.end());
      for (const auto& k : before) if (!afterSet.count(k)) noteEntry(coll, def.name, k, false);
      for (const auto& k : after) if (!beforeSet.count(k)) noteEntry(coll, def.name, k, true);
    }
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
  IndexTree indexTree_;
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

// ---- snapshots ---------------------------------------------------------------

std::vector<std::string> Snapshot::listCollections() const {
  ReadBlocks reader(store_);
  Tree catalog(reader, LeafKind::Catalog);
  std::vector<std::string> names;
  catalog.forEach(block_.catalog, [&](const Record& r) {
    if (r.version.counter > block_.clock) throw StorageError("BrightDB catalog version exceeds the database clock");
    if (r.target) names.push_back(r.key);
  });
  return names;
}

std::optional<CollectionRoot> Snapshot::collectionRoot(const std::string& collection) const {
  ReadBlocks reader(store_);
  Tree catalog(reader, LeafKind::Catalog);
  return readCollection(store_, catalog, block_, collection);
}

std::optional<nlohmann::json> Snapshot::document(const Record& record, const CollectionRoot& collection) const {
  checkPrimaryVersion(record, collection.generation, block_.clock);
  if (!record.target) return std::nullopt;
  return decodeDocument(store_.readBlock(*record.target), record.key, record.target);
}

std::optional<nlohmann::json> Snapshot::findById(const std::string& collection, const std::string& id) const {
  encodeKey(id);
  auto coll = collectionRoot(collection);
  if (!coll) return std::nullopt;
  ReadBlocks reader(store_);
  Tree primary(reader, LeafKind::Primary);
  auto record = primary.get(coll->primary, id);
  if (!record) return std::nullopt;
  return document(*record, *coll);
}

std::vector<nlohmann::json> Snapshot::findAll(const std::string& collection) const {
  std::vector<nlohmann::json> out;
  auto coll = collectionRoot(collection);
  if (!coll) return out;
  ReadBlocks reader(store_);
  Tree primary(reader, LeafKind::Primary);
  primary.forEach(coll->primary, [&](const Record& r) {
    if (auto doc = document(r, *coll)) out.push_back(std::move(*doc));
  });
  return out;
}

Database Database::fork(const std::string& path) const {
  auto head = store_.readHead();
  auto root = readRootFrom(store_);
  auto store = DiskStore::create(path, encodeMetadata(DatabaseRoot{root.databaseId, 0, std::nullopt}));
  store.withWriteLock([&](DiskStore::Writer& writer) {
    copyDatabase(store, writer, store_, head);
    writer.publishHead(head);
  });
  return Database(std::move(store));
}

MergeResult Database::merge(const Database& other, const std::optional<Digest>& root) {
  auto remoteHead = root ? *root : other.store_.readHead();
  MergeResult result;
  store_.withWriteLock([&](DiskStore::Writer& writer) {
    result.previous = store_.readHead();
    result.root = result.previous;
    if (auto merged = mergeReplica(store_, writer, other.store_, remoteHead)) {
      writer.publishHead(merged->first);
      result.root = merged->first;
      result.adopted = merged->second;
    }
  });
  notifier_->bump();
  return result;
}

Digest Database::root() const { return store_.readHead(); }

Snapshot Database::snapshot() const { return at(store_.readHead()); }

Snapshot Database::at(const Digest& root) const {
  auto meta = decodeMetadata(store_.readBlock(root), root);
  const auto* block = std::get_if<DatabaseRoot>(&meta);
  if (!block) throw StorageError("Digest does not identify a BrightDB database root");
  if (block->databaseId != readRootFrom(store_).databaseId)
    throw StorageError("Root belongs to a different BrightDB database");
  return Snapshot(store_, root, *block);
}

std::vector<Change> Database::diff(const Digest& from, const Digest& to) const {
  auto before = at(from);
  auto after = at(to);
  std::set<std::string> names;
  for (auto& n : before.listCollections()) names.insert(n);
  for (auto& n : after.listCollections()) names.insert(n);
  std::vector<std::string> ordered(names.begin(), names.end());
  std::sort(ordered.begin(), ordered.end(), [](const std::string& a, const std::string& b) { return compareKeys(a, b) < 0; });

  ReadBlocks reader(store_);
  Tree primary(reader, LeafKind::Primary);
  std::vector<Change> out;
  for (const auto& name : ordered) {
    auto a = before.collectionRoot(name);
    auto b = after.collectionRoot(name);
    auto emit = [&](const RecordChange& change) {
      std::optional<nlohmann::json> oldDoc, newDoc;
      if (change.before && change.before->target && a) oldDoc = before.document(*change.before, *a);
      if (change.after && change.after->target && b) newDoc = after.document(*change.after, *b);
      Change::Type type;
      if (!oldDoc) {
        if (!newDoc) return;
        type = Change::Type::Added;
      } else {
        type = newDoc ? Change::Type::Modified : Change::Type::Deleted;
      }
      out.push_back({name, change.key, type, oldDoc, newDoc});
    };
    if (a && b && compareVersions(a->generation, b->generation) == 0) {
      primary.diff(a->primary, b->primary, emit);
    } else {
      if (a) primary.forEach(a->primary, [&](const Record& r) { emit({r.key, r, std::nullopt}); });
      if (b) primary.forEach(b->primary, [&](const Record& r) { emit({r.key, std::nullopt, r}); });
    }
  }
  return out;
}


void Database::createIndex(const std::string& collection, const std::string& name, const std::vector<std::string>& fields) {
  encodeKey(collection);
  store_.withWriteLock([&](DiskStore::Writer& writer) {
    WriteBatch batch(store_, writer, readRootFrom(store_));
    batch.createIndex(collection, name, fields);
    if (auto head = batch.persist()) writer.publishHead(*head);
  });
  notifier_->bump();
}

void Database::dropIndex(const std::string& collection, const std::string& name) {
  encodeKey(collection);
  store_.withWriteLock([&](DiskStore::Writer& writer) {
    WriteBatch batch(store_, writer, readRootFrom(store_));
    batch.dropIndex(collection, name);
    if (auto head = batch.persist()) writer.publishHead(*head);
  });
  notifier_->bump();
}

AttachmentRef Database::putAttachment(const Bytes& bytes, const std::optional<std::string>& type) {
  AttachmentRef ref;
  store_.withWriteLock([&](DiskStore::Writer& writer) { ref = writeAttachment(writer, bytes, type); });
  return ref;
}

Bytes Database::getAttachment(const Digest& digest) const { return readAttachment(store_, digest); }

void Database::streamAttachment(const Digest& digest, const std::function<void(const Bytes&)>& visit) const {
  brightchain::v2::streamAttachment(store_, digest, visit);
}

std::vector<IndexInfo> Snapshot::listIndexes(const std::string& collection) const {
  auto coll = collectionRoot(collection);
  if (!coll) return {};
  std::vector<IndexInfo> out;
  for (const auto& d : readIndexDefinitions(store_, *coll)) out.push_back({d.name, d.fields});
  return out;
}

std::vector<nlohmann::json> Snapshot::findByIndex(const std::string& collection, const std::string& index,
                                                  const IndexQuery& query) const {
  auto coll = collectionRoot(collection);
  if (!coll) throw StorageError("BrightDB collection does not exist: " + collection);
  std::optional<IndexDefinition> definition;
  for (auto& d : readIndexDefinitions(store_, *coll)) if (d.name == index) definition = d;
  if (!definition) throw StorageError("BrightDB index does not exist: " + index);
  auto range = indexRange(definition->fields.size(), query);
  ReadBlocks reader(store_);
  Tree primary(reader, LeafKind::Primary);
  IndexTree tree(reader);
  auto limit = query.limit.value_or(SIZE_MAX);
  std::set<std::string> seen;
  std::vector<nlohmann::json> documents;
  tree.range(definition->root, range.lo, range.hi, query.descending, [&](const Bytes& entry) {
    if (documents.size() >= limit) return false;
    auto id = decodeIndexId(entry, definition->fields.size());
    if (!seen.insert(id).second) return true;
    auto record = primary.get(coll->primary, id);
    auto doc = record ? document(*record, *coll) : std::nullopt;
    if (!doc) throw StorageError("BrightDB index " + index + " references a missing document");
    documents.push_back(std::move(*doc));
    return true;
  });
  return documents;
}

std::vector<std::string> Database::listCollections() { return snapshot().listCollections(); }

std::optional<nlohmann::json> Database::findById(const std::string& collection, const std::string& id) {
  return snapshot().findById(collection, id);
}

std::vector<nlohmann::json> Database::findAll(const std::string& collection) {
  return snapshot().findAll(collection);
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
        case WriteOp::Type::CreateIndex: batch.createIndex(op.collection, op.name, op.fields); break;
        case WriteOp::Type::DropIndex: batch.dropIndex(op.collection, op.name); break;
      }
    }
    if (auto head = batch.persist()) writer.publishHead(*head);
  });
  notifier_->bump();
}

std::string Database::insertOne(const std::string& collection, const nlohmann::json& document) {
  std::string id;
  encodeKey(collection);
  store_.withWriteLock([&](DiskStore::Writer& writer) {
    WriteBatch batch(store_, writer, readRootFrom(store_));
    id = batch.insert(collection, document);
    if (auto head = batch.persist()) writer.publishHead(*head);
  });
  notifier_->bump();
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
  notifier_->bump();
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
  notifier_->bump();
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
  notifier_->bump();
  return count;
}

}  // namespace brightchain::v2

namespace brightchain::v2 {

std::vector<Change> ChangeEvent::changes() const { return db->diff(previous, root); }

struct Subscription::State {
  std::mutex mutex;
  bool stopped = false;
  std::shared_ptr<CommitNotifier> notifier;
};

Subscription::~Subscription() { stop(); }

void Subscription::stop() {
  if (state_) {
    { std::lock_guard<std::mutex> lock(state_->mutex); state_->stopped = true; }
    // Take the notifier's mutex so a waiting thread cannot miss the wakeup.
    { std::lock_guard<std::mutex> lock(state_->notifier->mutex); }
    state_->notifier->cv.notify_all();
  }
  if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

std::unique_ptr<Subscription> Database::subscribe(std::function<void(const ChangeEvent&)> listener,
                                                  std::chrono::milliseconds interval,
                                                  std::function<void(const std::exception&)> onError) const {
  auto sub = std::unique_ptr<Subscription>(new Subscription());
  sub->state_ = std::make_shared<Subscription::State>();
  sub->state_->notifier = notifier_;
  auto state = sub->state_;
  auto notifier = notifier_;
  Database db = *this;
  Digest last = db.root();
  sub->thread_ = std::thread([state, notifier, db, last, interval, listener = std::move(listener),
                              onError = std::move(onError)]() mutable {
    std::uint64_t seen = 0;
    while (true) {
      try {
        auto root = db.root();
        if (root != last) {
          ChangeEvent event{last, root, &db};
          last = root;
          listener(event);
        }
      } catch (const std::exception& e) {
        if (onError) onError(e);
      }
      // Wake on a local commit, on stop, or after the polling interval.
      std::unique_lock<std::mutex> lock(notifier->mutex);
      notifier->cv.wait_for(lock, interval, [&] {
        std::lock_guard<std::mutex> stateLock(state->mutex);
        return state->stopped || notifier->commits != seen;
      });
      seen = notifier->commits;
      std::lock_guard<std::mutex> stateLock(state->mutex);
      if (state->stopped) return;
    }
  });
  return sub;
}

}  // namespace brightchain::v2
