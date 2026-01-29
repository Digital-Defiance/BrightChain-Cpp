#include "brightchain/block_size.hpp"
#include <stdexcept>

namespace brightchain {

bool validateBlockSize(uint32_t length, bool allowNonStandard) {
    for (const auto& size : VALID_BLOCK_SIZES) {
        if (static_cast<uint32_t>(size) == length) {
            return true;
        }
    }
    return allowNonStandard && length > 0;
}

BlockSize lengthToBlockSize(uint32_t length, bool allowNonStandard) {
    if (!validateBlockSize(length, allowNonStandard)) {
        throw std::invalid_argument("Invalid block size length: " + std::to_string(length));
    }

    for (const auto& size : VALID_BLOCK_SIZES) {
        if (static_cast<uint32_t>(size) == length) {
            return size;
        }
    }

    if (allowNonStandard) {
        return lengthToClosestBlockSize(length);
    }

    throw std::invalid_argument("Invalid block size length: " + std::to_string(length));
}

BlockSize lengthToClosestBlockSize(uint32_t length) {
    if (length == 0) {
        throw std::invalid_argument("Block size length cannot be zero");
    }

    if (length > static_cast<uint32_t>(BlockSize::Huge)) {
        return BlockSize::Huge;
    }

    // Find the smallest block size that can fit the data
    for (const auto& size : VALID_BLOCK_SIZES) {
        if (length <= static_cast<uint32_t>(size)) {
            return size;
        }
    }

    return BlockSize::Huge;
}

std::string blockSizeToString(BlockSize blockSize) {
    switch (blockSize) {
        case BlockSize::Unknown: return "Unknown";
        case BlockSize::Message: return "Message";
        case BlockSize::Tiny: return "Tiny";
        case BlockSize::Small: return "Small";
        case BlockSize::Medium: return "Medium";
        case BlockSize::Large: return "Large";
        case BlockSize::Huge: return "Huge";
        default: return "Unknown";
    }
}

} // namespace brightchain
