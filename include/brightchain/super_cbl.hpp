#pragma once

#include <brightchain/base_block.hpp>
#include <brightchain/checksum.hpp>
#include <brightchain/constants.hpp>
#include <vector>

namespace brightchain {

/**
 * SuperCBL Header Structure:
 * [Magic(1)][Type(1)][Version(1)][CRC8(1)]
 * [CreatorId(16)][DateCreated(8)][SubCblCount(4)][TotalBlockCount(4)]
 * [Depth(2)][OriginalDataLength(8)][OriginalChecksum(64)][Signature(64)]
 * Total: 175 bytes
 */
struct SuperCBLHeader {
    uint8_t magic = BlockHeaderConstants::MAGIC_PREFIX;
    uint8_t type = static_cast<uint8_t>(StructuredBlockType::SuperCBL);
    uint8_t version = BlockHeaderConstants::VERSION;
    uint8_t crc8 = 0;
    std::array<uint8_t, 16> creatorId;
    uint64_t dateCreated;
    uint32_t subCblCount;
    uint32_t totalBlockCount;
    uint16_t depth;
    uint64_t originalDataLength;
    Checksum::HashArray originalDataChecksum;
    std::array<uint8_t, 64> signature;

    static constexpr size_t SIZE = 175;
    
    std::vector<uint8_t> serialize() const;
    static SuperCBLHeader deserialize(const std::vector<uint8_t>& data);
};

/**
 * SuperCBL - hierarchical CBL referencing sub-CBLs
 */
class SuperCBL : public BaseBlock {
public:
    SuperCBL(BlockSize blockSize, const std::vector<uint8_t>& data,
             const Checksum& checksum);

    void validateSync() const override;
    const std::vector<uint8_t>& data() const override { return data_; }
    std::vector<uint8_t> layerHeaderData() const override;
    std::vector<uint8_t> layerPayload() const override;
    size_t layerOverheadSize() const override { return SuperCBLHeader::SIZE; }

    /**
     * Validate signature using creator's public key
     */
    bool validateSignature(const std::vector<uint8_t>& publicKey) const;
    std::vector<Checksum> subCblChecksums() const;
    uint32_t subCblCount() const { return header_.subCblCount; }
    uint32_t totalBlockCount() const { return header_.totalBlockCount; }
    uint16_t depth() const { return header_.depth; }
    uint64_t originalDataLength() const { return header_.originalDataLength; }

private:
    std::vector<uint8_t> data_;
    SuperCBLHeader header_;
};

} // namespace brightchain
