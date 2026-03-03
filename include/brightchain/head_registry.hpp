#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace brightchain::db {

/**
 * Entry in the head registry mapping a collection key to its latest block ID.
 */
struct HeadEntry {
    std::string blockId;
    std::string timestamp; // ISO 8601
};

/**
 * Persists dbName:collectionName → blockId mappings to head-registry.json.
 *
 * Supports both legacy format (plain string values) and current format
 * (objects with blockId and timestamp). Uses atomic writes (temp + rename)
 * and file locking for crash safety.
 */
class HeadRegistry {
public:
    explicit HeadRegistry(const std::filesystem::path& dataDir);

    /// Load registry from disk. Missing/invalid file → empty registry.
    void load();

    /// Get the head entry for a key, or nullopt if not found.
    std::optional<HeadEntry> getHead(const std::string& key) const;

    /// Set the head block ID for a key. Persists immediately.
    void setHead(const std::string& key, const std::string& blockId);

    /// Remove a key from the registry. Persists immediately.
    void removeHead(const std::string& key);

    /// Clear all entries. Persists immediately.
    void clear();

    /// Return all keys in the registry.
    std::vector<std::string> keys() const;

private:
    void persist();
    void acquireLock();
    void releaseLock();
    std::string nowIso8601() const;

    std::filesystem::path dataDir_;
    std::filesystem::path registryPath_;  // dataDir / "head-registry.json"
    std::filesystem::path lockPath_;      // dataDir / "head-registry.json.lock"
    std::unordered_map<std::string, HeadEntry> heads_;
};

} // namespace brightchain::db
