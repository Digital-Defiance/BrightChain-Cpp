#pragma once

#include <brightchain/block_size.hpp>
#include <brightchain/collection.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/head_registry.hpp>
#include <brightchain/store_lock.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace brightchain::db {

/**
 * Options for constructing a BrightChainDb instance.
 */
struct DbOptions {
    std::string name = "brightchain";
    std::filesystem::path dataDir = ".";
    BlockSize blockSize = BlockSize::Medium;
};

/**
 * Top-level database object that manages collections, the HeadRegistry,
 * and the StoreLock. Provides a clean API entry point analogous to a
 * MongoDB database handle.
 *
 * BrightChainDb owns both the HeadRegistry and StoreLock and passes
 * references to them when creating Collection instances.
 */
class BrightChainDb {
public:
    /**
     * Construct a database backed by the given DiskBlockStore.
     * @param store  Block store for persisting documents and metadata.
     * @param options Database options (name, dataDir, blockSize).
     */
    BrightChainDb(DiskBlockStore& store, const DbOptions& options = {});

    /// Load the HeadRegistry from disk and mark the database as connected.
    void connect();

    /// Mark the database as disconnected.
    void disconnect();

    /**
     * Get or create a named collection.
     * @param name Collection name.
     * @return Reference to the Collection instance.
     */
    Collection& collection(const std::string& name);

    /// Return the names of all collections that have been created or loaded.
    std::vector<std::string> listCollections() const;

    /// Drop the named collection and remove it from the internal map.
    void dropCollection(const std::string& name);

    /// Drop all collections and clear the HeadRegistry.
    void dropDatabase();

    /// Get the database name.
    const std::string& name() const;

private:
    DiskBlockStore& store_;
    DbOptions options_;
    HeadRegistry headRegistry_;
    StoreLock storeLock_;
    std::unordered_map<std::string, std::unique_ptr<Collection>> collections_;
    bool connected_ = false;
};

} // namespace brightchain::db
