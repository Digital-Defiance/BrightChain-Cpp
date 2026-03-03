#include "brightchain/brightchain_db.hpp"

namespace brightchain::db {

BrightChainDb::BrightChainDb(DiskBlockStore& store, const DbOptions& options)
    : store_(store)
    , options_(options)
    , headRegistry_(options.dataDir)
    , storeLock_(store.storePath())
{
}

void BrightChainDb::connect() {
    headRegistry_.load();
    connected_ = true;
}

void BrightChainDb::disconnect() {
    connected_ = false;
}

Collection& BrightChainDb::collection(const std::string& name) {
    auto it = collections_.find(name);
    if (it != collections_.end()) {
        return *it->second;
    }
    auto col = std::make_unique<Collection>(
        name, store_, options_.name, headRegistry_, storeLock_, options_.blockSize);
    auto& ref = *col;
    collections_.emplace(name, std::move(col));
    return ref;
}

std::vector<std::string> BrightChainDb::listCollections() const {
    std::vector<std::string> names;
    names.reserve(collections_.size());
    for (const auto& [name, _] : collections_) {
        names.push_back(name);
    }
    return names;
}

void BrightChainDb::dropCollection(const std::string& name) {
    auto it = collections_.find(name);
    if (it != collections_.end()) {
        it->second->drop();
        collections_.erase(it);
    }
}

void BrightChainDb::dropDatabase() {
    for (auto& [_, col] : collections_) {
        col->drop();
    }
    collections_.clear();
    headRegistry_.clear();
}

const std::string& BrightChainDb::name() const {
    return options_.name;
}

} // namespace brightchain::db
