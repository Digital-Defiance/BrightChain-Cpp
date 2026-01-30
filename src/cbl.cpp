#include <brightchain/cbl.hpp>
#include <cstring>
#include <stdexcept>

namespace brightchain {

// Simple CRC8 calculation
static uint8_t calculateCRC8(const uint8_t* data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

std::vector<uint8_t> CBLHeader::serialize() const {
    std::vector<uint8_t> result(SIZE);
    size_t offset = 0;

    // Structured prefix
    result[offset++] = magic;
    result[offset++] = type;
    result[offset++] = version;
    result[offset++] = 0; // CRC8 placeholder
    
    // Creator ID
    std::memcpy(&result[offset], creatorId.data(), 16);
    offset += 16;
    
    // Date created (big-endian)
    for (int i = 7; i >= 0; --i) {
        result[offset++] = (dateCreated >> (i * 8)) & 0xFF;
    }
    
    // Address count (big-endian)
    for (int i = 3; i >= 0; --i) {
        result[offset++] = (addressCount >> (i * 8)) & 0xFF;
    }
    
    // Tuple size
    result[offset++] = tupleSize;
    
    // Original data length (big-endian)
    for (int i = 7; i >= 0; --i) {
        result[offset++] = (originalDataLength >> (i * 8)) & 0xFF;
    }
    
    // Original checksum
    std::memcpy(&result[offset], originalDataChecksum.data(), 64);
    offset += 64;
    
    // Is extended flag
    result[offset++] = isExtended;
    
    // Signature
    std::memcpy(&result[offset], signature.data(), 64);
    
    // Calculate and set CRC8 (over bytes 4 to offset-64, excluding signature)
    result[3] = calculateCRC8(&result[4], offset - 4 - 64);
    
    return result;
}

CBLHeader CBLHeader::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < SIZE) {
        throw std::invalid_argument("Insufficient data for CBL header");
    }

    CBLHeader header;
    size_t offset = 0;

    // Structured prefix
    header.magic = data[offset++];
    header.type = data[offset++];
    header.version = data[offset++];
    header.crc8 = data[offset++];
    
    // Creator ID
    std::memcpy(header.creatorId.data(), &data[offset], 16);
    offset += 16;
    
    // Date created (big-endian)
    header.dateCreated = 0;
    for (int i = 0; i < 8; ++i) {
        header.dateCreated = (header.dateCreated << 8) | data[offset++];
    }
    
    // Address count (big-endian)
    header.addressCount = 0;
    for (int i = 0; i < 4; ++i) {
        header.addressCount = (header.addressCount << 8) | data[offset++];
    }
    
    // Tuple size
    header.tupleSize = data[offset++];
    
    // Original data length (big-endian)
    header.originalDataLength = 0;
    for (int i = 0; i < 8; ++i) {
        header.originalDataLength = (header.originalDataLength << 8) | data[offset++];
    }
    
    // Original checksum
    std::memcpy(header.originalDataChecksum.data(), &data[offset], 64);
    offset += 64;
    
    // Is extended flag
    header.isExtended = data[offset++];
    
    // Signature
    std::memcpy(header.signature.data(), &data[offset], 64);
    
    return header;
}

ConstituentBlockListBlock::ConstituentBlockListBlock(
    BlockSize blockSize, const std::vector<uint8_t>& data, const Checksum& checksum)
    : BaseBlock(BlockType::ConstituentBlockList, BlockDataType::EphemeralStructuredData,
                blockSize, checksum),
      data_(data) {
    header_ = CBLHeader::deserialize(data);
    
    if (header_.magic != BlockHeaderConstants::MAGIC_PREFIX) {
        throw std::invalid_argument("Invalid magic prefix");
    }
}

void ConstituentBlockListBlock::validateSync() const {
    auto computed = Checksum::fromData(data_);
    if (computed != checksum_) {
        throw std::runtime_error("Checksum mismatch");
    }
}

std::vector<uint8_t> ConstituentBlockListBlock::layerHeaderData() const {
    return header_.serialize();
}

std::vector<uint8_t> ConstituentBlockListBlock::layerPayload() const {
    if (data_.size() <= CBLHeader::SIZE) {
        return {};
    }
    return std::vector<uint8_t>(data_.begin() + CBLHeader::SIZE, data_.end());
}

std::vector<Checksum> ConstituentBlockListBlock::addresses() const {
    std::vector<Checksum> result;
    const size_t addressSize = Checksum::HASH_SIZE;
    const size_t dataStart = CBLHeader::SIZE;
    
    for (uint32_t i = 0; i < header_.addressCount; ++i) {
        size_t offset = dataStart + (i * addressSize);
        if (offset + addressSize > data_.size()) {
            break;
        }
        
        Checksum::HashArray hash;
        std::memcpy(hash.data(), &data_[offset], addressSize);
        result.push_back(Checksum::fromHash(hash));
    }
    
    return result;
}

bool ConstituentBlockListBlock::validateSignature(const std::vector<uint8_t>& publicKey) const {
    // Signature validation requires EC key operations
    // For now, return true as placeholder - full implementation needs EC library integration
    // TODO: Implement using OpenSSL EC_KEY_verify or similar
    return true;
}

} // namespace brightchain
