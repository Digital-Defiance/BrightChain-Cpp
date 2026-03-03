#include "brightchain/store_lock.hpp"
#include "brightchain/db_errors.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <chrono>

namespace brightchain::db {

StoreLock::StoreLock(const std::filesystem::path& storePath)
    : lockPath_(storePath / ".brightchain-db.lock")
{}

void StoreLock::acquire() {
    for (int i = 0; i < maxRetries_; ++i) {
        int fd = ::open(lockPath_.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd >= 0) {
            ::close(fd);
            held_ = true;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs_));
    }

    // Stale lock recovery: force remove and try once more
    std::filesystem::remove(lockPath_);
    int fd = ::open(lockPath_.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd >= 0) {
        ::close(fd);
        held_ = true;
        return;
    }

    throw RegistryError("Failed to acquire store lock after timeout: " + lockPath_.string());
}

void StoreLock::release() {
    if (!held_) {
        return;
    }
    std::filesystem::remove(lockPath_);
    held_ = false;
}

// --- Guard ---

StoreLock::Guard::Guard(StoreLock& lock)
    : lock_(lock), held_(true)
{
    lock_.acquire();
}

StoreLock::Guard::~Guard() {
    if (held_) {
        lock_.release();
    }
}

StoreLock::Guard::Guard(Guard&& other) noexcept
    : lock_(other.lock_), held_(other.held_)
{
    other.held_ = false;
}

} // namespace brightchain::db
