#pragma once

#include "brightchain/block_size.hpp"
#include "brightchain/block_metadata.hpp"
#include "brightchain/checksum.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace brightchain {

/**
 * DiskBlockStore provides base functionality for storing blocks on disk.
 * Directory structure: storePath/blockSize/char1/char2/checksum
 * Metadata files: storePath/blockSize/char1/char2/checksum.m.json
 */
class DiskBlockStore {
public:
    /**
     * Constructor.
     * @param storePath Root directory for block storage
     * @param blockSize Block size for this store
     */
    DiskBlockStore(const std::string& storePath, BlockSize blockSize);

    /**
     * Store a block.
     * @param data Block data
     * @return Checksum of the stored block
     */
    Checksum put(const std::vector<uint8_t>& data);

    /**
     * Store a block with metadata.
     * @param data Block data
     * @param metadata Block metadata
     * @return Checksum of the stored block
     */
    Checksum put(const std::vector<uint8_t>& data, const BlockMetadata& metadata);

    /**
     * Retrieve a block.
     * @param checksum Block checksum
     * @return Block data
     * @throws std::runtime_error if block not found
     */
    std::vector<uint8_t> get(const Checksum& checksum) const;

    /**
     * Check if a block exists.
     * @param checksum Block checksum
     * @return True if block exists
     */
    bool has(const Checksum& checksum) const;

    /**
     * Delete a block.
     * @param checksum Block checksum
     * @return True if block was deleted
     */
    bool remove(const Checksum& checksum);

    /**
     * Store metadata for a block.
     * @param checksum Block checksum
     * @param metadata Block metadata
     */
    void putMetadata(const Checksum& checksum, const BlockMetadata& metadata);

    /**
     * Retrieve metadata for a block.
     * @param checksum Block checksum
     * @return Block metadata if exists
     */
    std::optional<BlockMetadata> getMetadata(const Checksum& checksum) const;

    /**
     * Check if metadata exists for a block.
     * @param checksum Block checksum
     * @return True if metadata exists
     */
    bool hasMetadata(const Checksum& checksum) const;

    /**
     * Get block size.
     */
    BlockSize blockSize() const { return blockSize_; }

    /**
     * Get store path.
     */
    const std::string& storePath() const { return storePath_; }

protected:
    /**
     * Get directory path for a block.
     */
    std::filesystem::path blockDir(const Checksum& checksum) const;

    /**
     * Get file path for a block.
     */
    std::filesystem::path blockPath(const Checksum& checksum) const;

    /**
     * Get metadata file path for a block.
     */
    std::filesystem::path metadataPath(const Checksum& checksum) const;

    /**
     * Ensure directory structure exists for a block.
     */
    void ensureBlockPath(const Checksum& checksum) const;

private:
    std::string storePath_;
    BlockSize blockSize_;
};

} // namespace brightchain
