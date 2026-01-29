#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace brightchain {

/**
 * Block size exponents (2^x) for calculating block sizes.
 */
constexpr std::array<uint8_t, 6> BLOCK_SIZE_EXPONENTS = {9, 10, 12, 20, 26, 28};

/**
 * Block size enumeration defining standard block sizes.
 * Each size is optimized for specific use cases.
 */
enum class BlockSize : uint32_t {
    Unknown = 0,
    Message = 512,        // 2^9  - Small messages, metadata
    Tiny = 1024,          // 2^10 - Small files
    Small = 4096,         // 2^12 - System page aligned
    Medium = 1048576,     // 2^20 - 1MB, balanced performance
    Large = 67108864,     // 2^26 - 64MB, large files
    Huge = 268435456      // 2^28 - 256MB, maximum throughput
};

/**
 * List of valid block sizes for validation.
 */
constexpr std::array<BlockSize, 6> VALID_BLOCK_SIZES = {
    BlockSize::Message,
    BlockSize::Tiny,
    BlockSize::Small,
    BlockSize::Medium,
    BlockSize::Large,
    BlockSize::Huge
};

/**
 * Validate if a length matches a valid block size.
 * @param length The length to validate
 * @param allowNonStandard Whether to allow non-standard sizes (for testing)
 * @return True if valid
 */
bool validateBlockSize(uint32_t length, bool allowNonStandard = false);

/**
 * Convert a byte length to its BlockSize enum value.
 * @param length The length in bytes
 * @param allowNonStandard Whether to allow non-standard sizes
 * @return The corresponding BlockSize enum value
 * @throws std::invalid_argument if length doesn't match a valid block size
 */
BlockSize lengthToBlockSize(uint32_t length, bool allowNonStandard = true);

/**
 * Convert a byte length to the closest BlockSize enum value.
 * @param length The length in bytes
 * @return The closest BlockSize enum value
 * @throws std::invalid_argument if length is negative
 */
BlockSize lengthToClosestBlockSize(uint32_t length);

/**
 * Convert a BlockSize enum value to its string representation.
 * @param blockSize The BlockSize enum value
 * @return Human-readable name
 */
std::string blockSizeToString(BlockSize blockSize);

/**
 * Get the byte length of a BlockSize.
 * @param blockSize The BlockSize enum value
 * @return Length in bytes
 */
constexpr uint32_t blockSizeToLength(BlockSize blockSize) {
    return static_cast<uint32_t>(blockSize);
}

} // namespace brightchain
