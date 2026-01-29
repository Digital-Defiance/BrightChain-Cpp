#include "brightchain/disk_block_store.hpp"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace brightchain {

DiskBlockStore::DiskBlockStore(const std::string& storePath, BlockSize blockSize)
    : storePath_(storePath), blockSize_(blockSize) {
    
    if (storePath.empty()) {
        throw std::invalid_argument("Store path is required");
    }

    if (blockSize == BlockSize::Unknown) {
        throw std::invalid_argument("Block size is required");
    }

    // Ensure store path exists
    std::filesystem::create_directories(storePath_);
}

std::filesystem::path DiskBlockStore::blockDir(const Checksum& checksum) const {
    std::string hex = checksum.toHex();
    if (hex.length() < 2) {
        throw std::invalid_argument("Checksum too short");
    }

    std::string blockSizeStr = blockSizeToString(blockSize_);
    return std::filesystem::path(storePath_) / blockSizeStr / 
           std::string(1, hex[0]) / std::string(1, hex[1]);
}

std::filesystem::path DiskBlockStore::blockPath(const Checksum& checksum) const {
    return blockDir(checksum) / checksum.toHex();
}

std::filesystem::path DiskBlockStore::metadataPath(const Checksum& checksum) const {
    return std::filesystem::path(blockPath(checksum).string() + ".m.json");
}

void DiskBlockStore::ensureBlockPath(const Checksum& checksum) const {
    std::filesystem::create_directories(blockDir(checksum));
}

Checksum DiskBlockStore::put(const std::vector<uint8_t>& data) {
    Checksum checksum = Checksum::fromData(data);
    BlockMetadata metadata(blockSize_, data.size());
    return put(data, metadata);
}

Checksum DiskBlockStore::put(const std::vector<uint8_t>& data, const BlockMetadata& metadata) {
    Checksum checksum = Checksum::fromData(data);
    ensureBlockPath(checksum);

    std::filesystem::path path = blockPath(checksum);
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to create block file: " + path.string());
    }

    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (!file) {
        throw std::runtime_error("Failed to write block data: " + path.string());
    }

    putMetadata(checksum, metadata);
    return checksum;
}

std::vector<uint8_t> DiskBlockStore::get(const Checksum& checksum) const {
    std::filesystem::path path = blockPath(checksum);
    
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Block not found: " + checksum.toHex());
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open block file: " + path.string());
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Failed to read block data: " + path.string());
    }

    return data;
}

bool DiskBlockStore::has(const Checksum& checksum) const {
    return std::filesystem::exists(blockPath(checksum));
}

bool DiskBlockStore::remove(const Checksum& checksum) {
    std::filesystem::path path = blockPath(checksum);
    std::filesystem::path metaPath = metadataPath(checksum);

    bool removed = false;
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
        removed = true;
    }
    if (std::filesystem::exists(metaPath)) {
        std::filesystem::remove(metaPath);
    }

    return removed;
}

void DiskBlockStore::putMetadata(const Checksum& checksum, const BlockMetadata& metadata) {
    ensureBlockPath(checksum);
    std::filesystem::path path = metadataPath(checksum);
    
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to create metadata file: " + path.string());
    }
    
    nlohmann::json j = metadata.to_json();
    file << j.dump(2);
}

std::optional<BlockMetadata> DiskBlockStore::getMetadata(const Checksum& checksum) const {
    std::filesystem::path path = metadataPath(checksum);
    
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }
    
    nlohmann::json j;
    file >> j;
    return BlockMetadata::from_json(j);
}

bool DiskBlockStore::hasMetadata(const Checksum& checksum) const {
    return std::filesystem::exists(metadataPath(checksum));
}

} // namespace brightchain
