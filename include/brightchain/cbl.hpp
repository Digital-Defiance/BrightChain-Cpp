#pragma once

#include <brightchain/base_block.hpp>
#include <brightchain/checksum.hpp>
#include <brightchain/constants.hpp>
#include <cstdint>
#include <ctime>
#include <vector>

namespace brightchain {

/**
 * CBL Header Structure (matches TypeScript exactly):
 * [Magic(1)][Type(1)][Version(1)][CRC8(1)]
 * [CreatorId(16)][DateCreated(8)][AddressCount(4)][TupleSize(1)]
 * [OriginalDataLength(8)][OriginalChecksum(64)][IsExtended(1)][Signature(64)]
 * Total: 170 bytes
 */
struct CBLHeader {
    uint8_t magic = BlockHeaderConstants::MAGIC_PREFIX;
    uint8_t type = static_cast<uint8_t>(StructuredBlockType::CBL);
    uint8_t version = BlockHeaderConstants::VERSION;
    uint8_t crc8 = 0;
    std::array<uint8_t, 16> creatorId;
    uint64_t dateCreated;
    uint32_t addressCount;
    uint8_t tupleSize;
    uint64_t originalDataLength;
    Checksum::HashArray originalDataChecksum;
    uint8_t isExtended = 0;
    std::array<uint8_t, 64> signature;

    static constexpr size_t SIZE = 170;
    
    std::vector<uint8_t> serialize() const;
    static CBLHeader deserialize(const std::vector<uint8_t>& data);
};

/**
 * Constituent Block List - stores references to related blocks.
 * Structure: [Header][Block References][Padding]
 */
class ConstituentBlockListBlock : public BaseBlock {
public:
    ConstituentBlockListBlock(BlockSize blockSize, const std::vector<uint8_t>& data,
                              const Checksum& checksum);

    void validateSync() const override;
    const std::vector<uint8_t>& data() const override { return data_; }
    std::vector<uint8_t> layerHeaderData() const override;
    std::vector<uint8_t> layerPayload() const override;
    size_t layerOverheadSize() const override { return CBLHeader::SIZE; }

    const CBLHeader& header() const { return header_; }
    std::vector<Checksum> addresses() const;
    uint32_t addressCount() const { return header_.addressCount; }
    uint32_t tupleSize() const { return header_.tupleSize; }
    uint64_t originalDataLength() const { return header_.originalDataLength; }
    
    /**
     * Validate signature using creator's public key
     */
    bool validateSignature(const std::vector<uint8_t>& publicKey) const;

protected:
    std::vector<uint8_t> data_;
    CBLHeader header_;
};

} // namespace brightchain
