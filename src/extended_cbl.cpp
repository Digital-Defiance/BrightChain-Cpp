#include <brightchain/extended_cbl.hpp>
#include <cstring>
#include <regex>
#include <stdexcept>

namespace brightchain {

std::vector<uint8_t> ExtendedCBLMetadata::serialize() const {
    if (fileName.length() > CBLConstants::MAX_FILE_NAME_LENGTH) {
        throw std::invalid_argument("File name too long");
    }
    if (mimeType.length() > CBLConstants::MAX_MIME_TYPE_LENGTH) {
        throw std::invalid_argument("MIME type too long");
    }
    
    std::vector<uint8_t> result;
    
    uint16_t fileNameLen = static_cast<uint16_t>(fileName.length());
    result.push_back((fileNameLen >> 8) & 0xFF);
    result.push_back(fileNameLen & 0xFF);
    result.insert(result.end(), fileName.begin(), fileName.end());
    
    uint8_t mimeTypeLen = static_cast<uint8_t>(mimeType.length());
    result.push_back(mimeTypeLen);
    result.insert(result.end(), mimeType.begin(), mimeType.end());
    
    return result;
}

ExtendedCBLMetadata ExtendedCBLMetadata::deserialize(
    const std::vector<uint8_t>& data, size_t offset) {
    ExtendedCBLMetadata metadata;
    
    if (offset + 1 >= data.size()) {
        throw std::invalid_argument("Invalid offset for metadata");
    }
    
    uint16_t fileNameLen = (data[offset] << 8) | data[offset + 1];
    offset += 2;
    if (offset + fileNameLen > data.size()) {
        throw std::invalid_argument("Invalid file name length");
    }
    metadata.fileName = std::string(data.begin() + offset, 
                                    data.begin() + offset + fileNameLen);
    offset += fileNameLen;
    
    if (offset >= data.size()) {
        throw std::invalid_argument("Missing MIME type");
    }
    
    uint8_t mimeTypeLen = data[offset++];
    if (offset + mimeTypeLen > data.size()) {
        throw std::invalid_argument("Invalid MIME type length");
    }
    metadata.mimeType = std::string(data.begin() + offset,
                                    data.begin() + offset + mimeTypeLen);
    
    return metadata;
}

size_t ExtendedCBLMetadata::size() const {
    return 3 + fileName.length() + mimeType.length();
}

ExtendedCBL::ExtendedCBL(BlockSize blockSize, const std::vector<uint8_t>& data,
                         const Checksum& checksum)
    : ConstituentBlockListBlock(blockSize, data, checksum) {
    
    if (header_.type != static_cast<uint8_t>(StructuredBlockType::ExtendedCBL)) {
        throw std::invalid_argument("Not an ExtendedCBL block");
    }
    
    // In ExtendedCBL, metadata comes at offset 106 (after isExtended flag)
    // BEFORE the signature, not after the full header
    metadata_ = ExtendedCBLMetadata::deserialize(data, 106);
}

void ExtendedCBL::validateSync() const {
    ConstituentBlockListBlock::validateSync();
    
    if (metadata_.fileName.length() > CBLConstants::MAX_FILE_NAME_LENGTH) {
        throw std::runtime_error("File name too long");
    }
    
    if (metadata_.mimeType.length() > CBLConstants::MAX_MIME_TYPE_LENGTH) {
        throw std::runtime_error("MIME type too long");
    }
    
    // Validate MIME type format (basic check)
    std::regex mimePattern(R"(^[a-zA-Z0-9][a-zA-Z0-9!#$&\-\^_+.]*\/[a-zA-Z0-9][a-zA-Z0-9!#$&\-\^_+.]*$)");
    if (!std::regex_match(metadata_.mimeType, mimePattern)) {
        throw std::runtime_error("Invalid MIME type format");
    }
}

std::vector<uint8_t> ExtendedCBL::layerHeaderData() const {
    auto baseHeader = ConstituentBlockListBlock::layerHeaderData();
    auto metadataBytes = metadata_.serialize();
    baseHeader.insert(baseHeader.end(), metadataBytes.begin(), metadataBytes.end());
    return baseHeader;
}

size_t ExtendedCBL::layerOverheadSize() const {
    return CBLHeader::SIZE + metadata_.size();
}

} // namespace brightchain
