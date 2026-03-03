#pragma once

#include <filesystem>

namespace brightchain::db {

/**
 * Cross-platform file-based lock that serializes write operations across
 * C++ and TypeScript processes. Uses the same lock file and protocol as
 * the TypeScript implementation for interoperability.
 *
 * Lock file path: storePath/.brightchain-db.lock
 * Acquisition: O_CREAT | O_EXCL with retry loop (250 × 20ms = 5s timeout)
 * Stale recovery: force-remove after timeout, retry once
 */
class StoreLock {
public:
    explicit StoreLock(const std::filesystem::path& storePath);

    /**
     * Acquire the lock. Blocks with retry until acquired or timeout.
     * @throws RegistryError on timeout after stale lock recovery attempt.
     */
    void acquire();

    /**
     * Release the lock. Safe to call if not held (no-op).
     */
    void release();

    /// Whether this instance currently holds the lock.
    bool isHeld() const { return held_; }

    /**
     * RAII guard for scoped locking. Acquires on construction, releases
     * on destruction. Ensures release on all exit paths including exceptions.
     */
    class Guard {
    public:
        explicit Guard(StoreLock& lock);
        ~Guard();

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&& other) noexcept;
        Guard& operator=(Guard&&) = delete;

    private:
        StoreLock& lock_;
        bool held_;
    };

private:
    std::filesystem::path lockPath_;  // storePath / ".brightchain-db.lock"
    int maxRetries_ = 250;            // 250 * 20ms = 5s default timeout
    int retryDelayMs_ = 20;
    bool held_ = false;
};

} // namespace brightchain::db
