#include "brightchain/collection.hpp"
#include "brightchain/db_errors.hpp"
#include "brightchain/query_engine.hpp"
#include "brightchain/update_engine.hpp"
#include "brightchain/block_metadata.hpp"

#include <algorithm>

namespace brightchain::db {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Collection::Collection(const std::string& name,
                       DiskBlockStore& store,
                       const std::string& dbName,
                       HeadRegistry& headRegistry,
                       StoreLock& storeLock,
                       BlockSize blockSize)
    : name_(name)
    , dbName_(dbName)
    , store_(store)
    , headRegistry_(headRegistry)
    , storeLock_(storeLock)
    , blockSize_(blockSize)
{
}

// ---------------------------------------------------------------------------
// Lazy loading
// ---------------------------------------------------------------------------
void Collection::ensureLoaded() {
    if (!loaded_) {
        loadFromStore();
        loaded_ = true;
    }
}

void Collection::loadFromStore() {
    std::string key = dbName_ + ":" + name_;
    auto entry = headRegistry_.getHead(key);
    if (!entry) {
        return; // empty collection — no head entry yet
    }

    // Retrieve the CollectionMeta block
    auto metaChecksum = Checksum::fromHex(entry->blockId);
    auto metaBytes = store_.get(metaChecksum);
    auto metaDoc = deserializeDocument(metaBytes);

    // Restore docIndex_ from "mappings"
    docIndex_.clear();
    if (metaDoc.contains("mappings") && metaDoc["mappings"].is_object()) {
        for (auto it = metaDoc["mappings"].begin(); it != metaDoc["mappings"].end(); ++it) {
            docIndex_[it.key()] = it.value().get<std::string>();
        }
    }

    // Restore indexes_ from "indexes"
    indexes_.clear();
    if (metaDoc.contains("indexes") && metaDoc["indexes"].is_array()) {
        for (const auto& idx : metaDoc["indexes"]) {
            IndexSpec spec;
            spec.name = idx.value("name", "");
            spec.spec = idx.value("spec", Document{});
            spec.unique = idx.value("unique", false);
            spec.sparse = idx.value("sparse", false);
            indexes_.push_back(std::move(spec));
        }
    }

    // Clear the document cache — stale entries may reference old checksums
    docCache_.clear();
}

// ---------------------------------------------------------------------------
// Persist metadata
// ---------------------------------------------------------------------------
void Collection::persistMeta() {
    // Build CollectionMeta JSON
    Document meta;

    // mappings: docId → checksum hex
    Document mappings = Document::object();
    for (const auto& [docId, checksumHex] : docIndex_) {
        mappings[docId] = checksumHex;
    }
    meta["mappings"] = std::move(mappings);

    // indexes: array of index descriptors
    auto indexArr = Document::array();
    for (const auto& idx : indexes_) {
        Document idxDoc;
        idxDoc["name"] = idx.name;
        idxDoc["spec"] = idx.spec;
        idxDoc["unique"] = idx.unique;
        idxDoc["sparse"] = idx.sparse;
        indexArr.push_back(std::move(idxDoc));
    }
    meta["indexes"] = std::move(indexArr);

    // Serialize, compute block ID, store
    auto bytes = serializeDocument(meta);
    auto checksum = computeBlockId(bytes);
    store_.put(bytes, BlockMetadata(blockSize_, bytes.size()));

    // Update HeadRegistry
    std::string key = dbName_ + ":" + name_;
    headRegistry_.setHead(key, checksum.toHex());
}

// ---------------------------------------------------------------------------
// Load a single document (with caching)
// ---------------------------------------------------------------------------
Document Collection::loadDocument(const std::string& checksumHex) {
    // Check cache first — find the docId that maps to this checksum
    for (const auto& [docId, cached] : docCache_) {
        auto it = docIndex_.find(docId);
        if (it != docIndex_.end() && it->second == checksumHex) {
            return cached;
        }
    }

    // Not cached — load from store
    auto checksum = Checksum::fromHex(checksumHex);
    auto bytes = store_.get(checksum);
    auto doc = deserializeDocument(bytes);

    // Cache by docId if we can find it
    if (doc.contains("_id") && doc["_id"].is_string()) {
        docCache_[doc["_id"].get<std::string>()] = doc;
    }

    return doc;
}

// ---------------------------------------------------------------------------
// insertOne
// ---------------------------------------------------------------------------
InsertOneResult Collection::insertOne(Document doc) {
    StoreLock::Guard guard(storeLock_);
    loaded_ = false;
    ensureLoaded();

    DocumentId id = ensureDocumentId(doc);

    if (docIndex_.count(id)) {
        throw DuplicateKeyError("Duplicate key: _id=" + id);
    }

    auto bytes = serializeDocument(doc);
    auto checksum = computeBlockId(bytes);
    store_.put(bytes, BlockMetadata(blockSize_, bytes.size()));

    docIndex_[id] = checksum.toHex();
    docCache_[id] = std::move(doc);

    persistMeta();

    return InsertOneResult{id};
}

// ---------------------------------------------------------------------------
// insertMany
// ---------------------------------------------------------------------------
InsertManyResult Collection::insertMany(std::vector<Document> docs) {
    StoreLock::Guard guard(storeLock_);
    loaded_ = false;
    ensureLoaded();

    std::vector<DocumentId> ids;
    ids.reserve(docs.size());

    for (auto& doc : docs) {
        DocumentId id = ensureDocumentId(doc);

        if (docIndex_.count(id)) {
            throw DuplicateKeyError("Duplicate key: _id=" + id);
        }

        auto bytes = serializeDocument(doc);
        auto checksum = computeBlockId(bytes);
        store_.put(bytes, BlockMetadata(blockSize_, bytes.size()));

        docIndex_[id] = checksum.toHex();
        docCache_[id] = std::move(doc);
        ids.push_back(id);
    }

    persistMeta();

    return InsertManyResult{std::move(ids)};
}

// ---------------------------------------------------------------------------
// findOne (no lock)
// ---------------------------------------------------------------------------
std::optional<Document> Collection::findOne(const Document& filter) {
    ensureLoaded();

    for (const auto& [docId, checksumHex] : docIndex_) {
        Document doc = loadDocument(checksumHex);
        if (QueryEngine::matchesFilter(doc, filter)) {
            return doc;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// find (no lock)
// ---------------------------------------------------------------------------
std::vector<Document> Collection::find(const Document& filter,
                                       const FindOptions& options) {
    ensureLoaded();

    std::vector<Document> results;
    size_t skipped = 0;
    size_t skipCount = options.skip.value_or(0);

    for (const auto& [docId, checksumHex] : docIndex_) {
        Document doc = loadDocument(checksumHex);
        if (!QueryEngine::matchesFilter(doc, filter)) {
            continue;
        }

        if (skipped < skipCount) {
            ++skipped;
            continue;
        }

        results.push_back(std::move(doc));

        if (options.limit && results.size() >= *options.limit) {
            break;
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// updateOne
// ---------------------------------------------------------------------------
UpdateResult Collection::updateOne(const Document& filter, const Document& update) {
    StoreLock::Guard guard(storeLock_);
    loaded_ = false;
    ensureLoaded();

    for (const auto& [docId, checksumHex] : docIndex_) {
        Document doc = loadDocument(checksumHex);
        if (!QueryEngine::matchesFilter(doc, filter)) {
            continue;
        }

        // Found a match — apply update
        Document modified = UpdateEngine::applyUpdate(doc, update);

        auto bytes = serializeDocument(modified);
        auto newChecksum = computeBlockId(bytes);
        store_.put(bytes, BlockMetadata(blockSize_, bytes.size()));

        docIndex_[docId] = newChecksum.toHex();
        docCache_[docId] = std::move(modified);

        persistMeta();
        return UpdateResult{1, 1};
    }

    return UpdateResult{0, 0};
}

// ---------------------------------------------------------------------------
// updateMany
// ---------------------------------------------------------------------------
UpdateResult Collection::updateMany(const Document& filter, const Document& update) {
    StoreLock::Guard guard(storeLock_);
    loaded_ = false;
    ensureLoaded();

    size_t matched = 0;
    size_t modified = 0;

    // Collect matching doc IDs first to avoid modifying map during iteration
    std::vector<DocumentId> matchingIds;
    for (const auto& [docId, checksumHex] : docIndex_) {
        Document doc = loadDocument(checksumHex);
        if (QueryEngine::matchesFilter(doc, filter)) {
            matchingIds.push_back(docId);
        }
    }

    for (const auto& docId : matchingIds) {
        Document doc = loadDocument(docIndex_[docId]);
        Document updatedDoc = UpdateEngine::applyUpdate(doc, update);

        auto bytes = serializeDocument(updatedDoc);
        auto newChecksum = computeBlockId(bytes);
        store_.put(bytes, BlockMetadata(blockSize_, bytes.size()));

        docIndex_[docId] = newChecksum.toHex();
        docCache_[docId] = std::move(updatedDoc);

        ++matched;
        ++modified;
    }

    if (matched > 0) {
        persistMeta();
    }

    return UpdateResult{matched, modified};
}

// ---------------------------------------------------------------------------
// deleteOne
// ---------------------------------------------------------------------------
DeleteResult Collection::deleteOne(const Document& filter) {
    StoreLock::Guard guard(storeLock_);
    loaded_ = false;
    ensureLoaded();

    for (auto it = docIndex_.begin(); it != docIndex_.end(); ++it) {
        Document doc = loadDocument(it->second);
        if (QueryEngine::matchesFilter(doc, filter)) {
            docCache_.erase(it->first);
            docIndex_.erase(it);
            persistMeta();
            return DeleteResult{1};
        }
    }

    return DeleteResult{0};
}

// ---------------------------------------------------------------------------
// deleteMany
// ---------------------------------------------------------------------------
DeleteResult Collection::deleteMany(const Document& filter) {
    StoreLock::Guard guard(storeLock_);
    loaded_ = false;
    ensureLoaded();

    // Collect IDs to delete first
    std::vector<DocumentId> toDelete;
    for (const auto& [docId, checksumHex] : docIndex_) {
        Document doc = loadDocument(checksumHex);
        if (QueryEngine::matchesFilter(doc, filter)) {
            toDelete.push_back(docId);
        }
    }

    for (const auto& docId : toDelete) {
        docIndex_.erase(docId);
        docCache_.erase(docId);
    }

    if (!toDelete.empty()) {
        persistMeta();
    }

    return DeleteResult{toDelete.size()};
}

// ---------------------------------------------------------------------------
// drop
// ---------------------------------------------------------------------------
void Collection::drop() {
    StoreLock::Guard guard(storeLock_);

    std::string key = dbName_ + ":" + name_;
    headRegistry_.removeHead(key);

    docIndex_.clear();
    docCache_.clear();
    indexes_.clear();
    loaded_ = false;
}

// ---------------------------------------------------------------------------
// Index management
// ---------------------------------------------------------------------------
void Collection::createIndex(const IndexSpec& spec) {
    StoreLock::Guard guard(storeLock_);
    loaded_ = false;
    ensureLoaded();

    indexes_.push_back(spec);
    persistMeta();
}

void Collection::dropIndex(const std::string& indexName) {
    StoreLock::Guard guard(storeLock_);
    loaded_ = false;
    ensureLoaded();

    indexes_.erase(
        std::remove_if(indexes_.begin(), indexes_.end(),
                        [&indexName](const IndexSpec& idx) {
                            return idx.name == indexName;
                        }),
        indexes_.end());
    persistMeta();
}

std::vector<IndexSpec> Collection::listIndexes() const {
    return indexes_;
}

// ---------------------------------------------------------------------------
// name
// ---------------------------------------------------------------------------
const std::string& Collection::name() const {
    return name_;
}

} // namespace brightchain::db
