#include "brightchain/head_registry.hpp"
#include "brightchain/db_errors.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <thread>

namespace brightchain::db {

HeadRegistry::HeadRegistry(const std::filesystem::path& dataDir)
    : dataDir_(dataDir)
    , registryPath_(dataDir / "head-registry.json")
    , lockPath_(dataDir / "head-registry.json.lock")
{}

void HeadRegistry::load() {
    heads_.clear();

    if (!std::filesystem::exists(registryPath_)) {
        return;
    }

    std::ifstream ifs(registryPath_);
    if (!ifs.is_open()) {
        return;
    }

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const nlohmann::json::parse_error&) {
        std::cerr << "HeadRegistry: invalid JSON in " << registryPath_
                  << ", starting empty\n";
        return;
    }

    if (!j.is_object()) {
        return;
    }

    for (auto& [key, value] : j.items()) {
        if (value.is_string()) {
            // Legacy format: plain string blockId
            heads_[key] = HeadEntry{value.get<std::string>(), ""};
        } else if (value.is_object() && value.contains("blockId")) {
            HeadEntry entry;
            entry.blockId = value["blockId"].get<std::string>();
            entry.timestamp = value.value("timestamp", "");
            heads_[key] = std::move(entry);
        }
    }
}


std::optional<HeadEntry> HeadRegistry::getHead(const std::string& key) const {
    auto it = heads_.find(key);
    if (it == heads_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void HeadRegistry::setHead(const std::string& key, const std::string& blockId) {
    acquireLock();
    try {
        heads_[key] = HeadEntry{blockId, nowIso8601()};
        persist();
    } catch (...) {
        releaseLock();
        throw;
    }
    releaseLock();
}

void HeadRegistry::removeHead(const std::string& key) {
    acquireLock();
    try {
        heads_.erase(key);
        persist();
    } catch (...) {
        releaseLock();
        throw;
    }
    releaseLock();
}

void HeadRegistry::clear() {
    acquireLock();
    try {
        heads_.clear();
        persist();
    } catch (...) {
        releaseLock();
        throw;
    }
    releaseLock();
}

std::vector<std::string> HeadRegistry::keys() const {
    std::vector<std::string> result;
    result.reserve(heads_.size());
    for (const auto& [key, _] : heads_) {
        result.push_back(key);
    }
    return result;
}

void HeadRegistry::persist() {
    std::filesystem::create_directories(dataDir_);

    nlohmann::json j = nlohmann::json::object();
    for (const auto& [key, entry] : heads_) {
        j[key] = {{"blockId", entry.blockId}, {"timestamp", entry.timestamp}};
    }

    // Write to temp file, then atomic rename
    auto tmpPath = registryPath_;
    tmpPath += ".tmp";

    std::ofstream ofs(tmpPath);
    if (!ofs.is_open()) {
        throw RegistryError("Failed to open temp file for head registry: " + tmpPath.string());
    }
    ofs << j.dump(2);
    ofs.close();

    if (ofs.fail()) {
        throw RegistryError("Failed to write head registry temp file");
    }

    std::filesystem::rename(tmpPath, registryPath_);
}

void HeadRegistry::acquireLock() {
    constexpr int maxRetries = 250;
    constexpr int delayMs = 20;

    for (int i = 0; i < maxRetries; ++i) {
        int fd = ::open(lockPath_.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd >= 0) {
            ::close(fd);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    // Stale lock recovery: force remove and try once more
    std::filesystem::remove(lockPath_);
    int fd = ::open(lockPath_.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd >= 0) {
        ::close(fd);
        return;
    }

    throw RegistryError("Failed to acquire head registry lock after timeout");
}

void HeadRegistry::releaseLock() {
    std::filesystem::remove(lockPath_);
}

std::string HeadRegistry::nowIso8601() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time_t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

} // namespace brightchain::db
