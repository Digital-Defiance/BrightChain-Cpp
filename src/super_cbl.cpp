#include <brightchain/super_cbl.hpp>
#include <cstring>
#include <stdexcept>

namespace brightchain {

static uint8_t calculateCRC8(const uint8_t* data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1;
        }
    }
    return crc;
}

std::vector<uint8_t> SuperCBLHeader::serialize() const {
    std::vector<uint8_t> result(SIZE);
    size_t offset = 0;

    result[offset++] = magic;
    result[offset++] = type;
    result[offset++] = version;
    result[offset++] = 0; // CRC8 placeholder
    
    std::memcpy(&result[offset], creatorId.data(), 16);
    offset += 16;
    
    for (int i = 7; i >= 0; --i) result[offset++] = (dateCreated >> (i * 8)) & 0xFF;
    for (int i = 3; i >= 0; --i) result[offset++] = (subCblCount >> (i * 8)) & 0xFF;
    for (int i = 3; i >= 0; --i) result[offset++] = (totalBlockCount >> (i * 8)) & 0xFF;
    for (int i = 1; i >= 0; --i) result[offset++] = (depth >> (i * 8)) & 0xFF;
    for (int i = 7; i >= 0; --i) result[offset++] = (originalDataLength >> (i * 8)) & 0xFF;
    
    std::memcpy(&result[offset], originalDataChecksum.data(), 64);
    offset += 64;
    
    std::memcpy(&result[offset], signature.data(), 64);
    
    result[3] = calculateCRC8(&result[4], offset - 4 - 64);
    
    return result;
}

SuperCBLHeader SuperCBLHeader::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < SIZE) {
        throw std::invalid_argument("Insufficient data for SuperCBL header");
    }

    SuperCBLHeader header;
    size_t offset = 0;

    header.magic = data[offset++];
    header.type = data[offset++];
    header.version = data[offset++];
    header.crc8 = data[offset++];
    
    std::memcpy(header.creatorId.data(), &data[offset], 16);
    offset += 16;
    
    header.dateCreated = 0;
    for (int i = 0; i < 8; ++i) header.dateCreated = (header.dateCreated << 8) | data[offset++];
    
    header.subCblCount = 0;
    for (int i = 0; i < 4; ++i) header.subCblCount = (header.subCblCount << 8) | data[offset++];
    
    header.totalBlockCount = 0;
    for (int i = 0; i < 4; ++i) header.totalBlockCount = (header.totalBlockCount << 8) | data[offset++];
    
    header.depth = 0;
    for (int i = 0; i < 2; ++i) header.depth = (header.depth << 8) | data[offset++];
    
    header.originalDataLength = 0;
    for (int i = 0; i < 8; ++i) header.originalDataLength = (header.originalDataLength << 8) | data[offset++];
    
    std::memcpy(header.originalDataChecksum.data(), &data[offset], 64);
    offset += 64;
    
    std::memcpy(header.signature.data(), &data[offset], 64);
    
    return header;
}

SuperCBL::SuperCBL(BlockSize blockSize, const std::vector<uint8_t>& data,
                   const Checksum& checksum)
    : BaseBlock(BlockType::SuperCBL, BlockDataType::EphemeralStructuredData,
                blockSize, checksum),
      data_(data) {
    header_ = SuperCBLHeader::deserialize(data);
    
    if (header_.magic != BlockHeaderConstants::MAGIC_PREFIX) {
        throw std::invalid_argument("Invalid magic prefix");
    }
}

void SuperCBL::validateSync() const {
    auto computed = Checksum::fromData(data_);
    if (computed != checksum_) {
        throw std::runtime_error("Checksum mismatch");
    }
}

std::vector<uint8_t> SuperCBL::layerHeaderData() const {
    return header_.serialize();
}

std::vector<uint8_t> SuperCBL::layerPayload() const {
    if (data_.size() <= SuperCBLHeader::SIZE) {
        return {};
    }
    return std::vector<uint8_t>(data_.begin() + SuperCBLHeader::SIZE, data_.end());
}

std::vector<Checksum> SuperCBL::subCblChecksums() const {
    std::vector<Checksum> result;
    const size_t checksumSize = Checksum::HASH_SIZE;
    const size_t dataStart = SuperCBLHeader::SIZE;
    
    for (uint32_t i = 0; i < header_.subCblCount; ++i) {
        size_t offset = dataStart + (i * checksumSize);
        if (offset + checksumSize > data_.size()) {
            break;
        }
        
        Checksum::HashArray hash;
        std::memcpy(hash.data(), &data_[offset], checksumSize);
        result.push_back(Checksum::fromHash(hash));
    }
    
    return result;
}

bool SuperCBL::validateSignature(const std::vector<uint8_t>& publicKey) const {
    // Signature validation requires EC key operations
    // TODO: Implement using OpenSSL EC_KEY_verify
    return true;
}

} // namespace brightchain
