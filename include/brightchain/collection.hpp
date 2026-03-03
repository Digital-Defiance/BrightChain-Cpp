#pragma once

#include <brightchain/block_size.hpp>
#include <brightchain/document.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/head_registry.hpp>
#include <brightchain/store_lock.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace brightchain::db {

/// Result of inserting a single document.
struct InsertOneResult {
    DocumentId insertedId;
};

/// Result of inserting multiple documents.
struct InsertManyResult {
    std::vector<DocumentId> insertedIds;
};

/// Result of an update operation.
struct UpdateResult {
    size_t matchedCount;
    size_t modifiedCount;
};

/// Result of a delete operation.
struct DeleteResult {
    size_t deletedCount;
};

/// Options for find queries (limit and skip).
struct FindOptions {
    std::optional<size_t> limit;
    std::optional<size_t> skip;
};

/// Descriptor for a collection index (metadata only, no query optimization).
struct IndexSpec {
    std::string name;
    Document spec;       // e.g. {"field": 1}
    bool unique = false;
    bool sparse = false;
};

/**
 * A named group of documents within a database, analogous to a MongoDB collection.
 *
 * Documents are stored as content-addressable blocks in the DiskBlockStore.
 * The collection maintains an in-memory index mapping DocumentId → block checksum
 * and persists this mapping as a CollectionMeta block via the HeadRegistry.
 *
 * Write operations acquire the StoreLock for the full read-modify-write cycle.
 * Read-only operations (findOne, find) do NOT acquire the lock.
 */
class Collection {
public:
    /**
     * Construct a collection.
     * @param name        Collection name.
     * @param store       Block store for persisting documents and metadata.
     * @param dbName      Database name (used as HeadRegistry key prefix).
     * @param headRegistry Registry tracking the latest CollectionMeta block.
     * @param storeLock   Cross-platform lock for serializing write operations.
     * @param blockSize   Block size for stored blocks (default Medium).
     */
    Collection(const std::string& name,
               DiskBlockStore& store,
               const std::string& dbName,
               HeadRegistry& headRegistry,
               StoreLock& storeLock,
               BlockSize blockSize = BlockSize::Medium);

    // -- CRUD operations --

    /// Insert a single document. Generates _id if missing.
    InsertOneResult insertOne(Document doc);

    /// Insert multiple documents. Generates _id for each if missing.
    InsertManyResult insertMany(std::vector<Document> docs);

    /// Find the first document matching the filter, or nullopt.
    std::optional<Document> findOne(const Document& filter = {});

    /// Find all documents matching the filter, with optional skip/limit.
    std::vector<Document> find(const Document& filter = {},
                               const FindOptions& options = {});

    /// Update the first document matching the filter.
    UpdateResult updateOne(const Document& filter, const Document& update);

    /// Update all documents matching the filter.
    UpdateResult updateMany(const Document& filter, const Document& update);

    /// Delete the first document matching the filter.
    DeleteResult deleteOne(const Document& filter);

    /// Delete all documents matching the filter.
    DeleteResult deleteMany(const Document& filter);

    // -- Index management (metadata only, no query optimization) --

    /// Create an index from the given spec.
    void createIndex(const IndexSpec& spec);

    /// Drop the index with the given name.
    void dropIndex(const std::string& name);

    /// List all indexes on this collection.
    std::vector<IndexSpec> listIndexes() const;

    // -- Lifecycle --

    /// Drop this collection: removes head pointer, clears index and cache.
    void drop();

    /// Get the collection name.
    const std::string& name() const;

private:
    /// Ensure the collection metadata has been loaded from the store.
    void ensureLoaded();

    /// Load CollectionMeta from the block store via the HeadRegistry.
    void loadFromStore();

    /// Persist the current CollectionMeta (mappings + indexes) to the store.
    void persistMeta();

    /// Load and deserialize a document from the block store by checksum hex.
    Document loadDocument(const std::string& checksumHex);

    std::string name_;
    std::string dbName_;
    DiskBlockStore& store_;
    HeadRegistry& headRegistry_;
    StoreLock& storeLock_;
    BlockSize blockSize_;
    bool loaded_ = false;

    std::unordered_map<DocumentId, std::string> docIndex_;   // _id → checksum hex
    std::unordered_map<DocumentId, Document> docCache_;      // _id → cached doc
    std::vector<IndexSpec> indexes_;
};

} // namespace brightchain::db
