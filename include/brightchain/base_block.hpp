#pragma once

#include <brightchain/block_size.hpp>
#include <brightchain/checksum.hpp>
#include <cstdint>
#include <vector>

namespace brightchain {

enum class BlockType : uint8_t {
    RawData = 0x01,
    ConstituentBlockList = 0x02,
    SuperCBL = 0x03,
    ExtendedConstituentBlockList = 0x04,
    MessageCBL = 0x05
};

enum class BlockDataType : uint8_t {
    RawData = 0x01,
    EphemeralStructuredData = 0x02
};

/**
 * Base class for all blocks in the BrightChain system.
 * Provides core block functionality and defines the interface.
 */
class BaseBlock {
public:
    virtual ~BaseBlock() = default;

    BlockSize blockSize() const { return blockSize_; }
    BlockType blockType() const { return blockType_; }
    BlockDataType blockDataType() const { return blockDataType_; }
    const Checksum& idChecksum() const { return checksum_; }
    bool canRead() const { return canRead_; }
    bool canPersist() const { return canPersist_; }

    virtual void validateSync() const = 0;
    virtual const std::vector<uint8_t>& data() const = 0;
    virtual std::vector<uint8_t> layerHeaderData() const = 0;
    virtual std::vector<uint8_t> layerPayload() const = 0;
    virtual size_t layerOverheadSize() const = 0;

protected:
    BaseBlock(BlockType type, BlockDataType dataType, BlockSize size,
              const Checksum& checksum, bool canRead = true, bool canPersist = true)
        : blockType_(type), blockDataType_(dataType), blockSize_(size),
          checksum_(checksum), canRead_(canRead), canPersist_(canPersist) {}

    BlockType blockType_;
    BlockDataType blockDataType_;
    BlockSize blockSize_;
    Checksum checksum_;
    bool canRead_;
    bool canPersist_;
};

} // namespace brightchain
