#pragma once

#include <cstdint>
#include <string>

namespace brightchain {

/**
 * Site-specific constants
 */
struct SiteConstants {
    static constexpr const char* NAME = "BrightChain";
    static constexpr const char* VERSION = "1.0.0";
    static constexpr const char* DESCRIPTION = "BrightChain";
    static constexpr const char* EMAIL_FROM = "noreply@brightchain.org";
    static constexpr const char* DEFAULT_DOMAIN = "localhost:3000";
    static constexpr uint32_t CSP_NONCE_SIZE = 32;
};

/**
 * Block header constants
 */
struct BlockHeaderConstants {
    static constexpr uint8_t MAGIC_PREFIX = 0xBC;
    static constexpr uint8_t VERSION = 0x01;
};

/**
 * Structured block type identifiers
 */
enum class StructuredBlockType : uint8_t {
    CBL = 0x02,         // Constituent Block List
    SuperCBL = 0x03,    // Hierarchical CBL
    ExtendedCBL = 0x04, // CBL with file name and MIME type
    MessageCBL = 0x05   // CBL for messaging
};

/**
 * CBL (Constituent Block List) constants
 */
struct CBLConstants {
    static constexpr uint32_t BASE_OVERHEAD = 170;
    static constexpr uint32_t MAX_FILE_NAME_LENGTH = 255;
    static constexpr uint32_t MAX_MIME_TYPE_LENGTH = 127;
    static constexpr uint64_t MAX_INPUT_FILE_SIZE = 9007199254740991ULL; // 2^53 - 1
};

/**
 * FEC (Forward Error Correction) constants
 */
struct FECConstants {
    static constexpr uint32_t MAX_SHARD_SIZE = 1048576; // BlockSize::Medium
    static constexpr uint32_t MIN_REDUNDANCY = 2;
    static constexpr uint32_t MAX_REDUNDANCY = 5;
    static constexpr double REDUNDANCY_FACTOR = 1.5;
};

/**
 * Tuple operation constants
 */
struct TupleConstants {
    static constexpr uint32_t MIN_RANDOM_BLOCKS = 2;
    static constexpr uint32_t MAX_RANDOM_BLOCKS = 5;
    static constexpr uint32_t RANDOM_BLOCKS_PER_TUPLE = 2;
    static constexpr uint32_t SIZE = 3;
    static constexpr uint32_t MIN_SIZE = 2;
    static constexpr uint32_t MAX_SIZE = 10;
};

/**
 * Sealing operation constants (for Quorum)
 */
struct SealingConstants {
    static constexpr uint32_t MIN_SHARES = 2;
    static constexpr uint32_t MAX_SHARES = 1048575;
    static constexpr uint32_t DEFAULT_THRESHOLD = 3;
};

/**
 * OFFS cache percentage
 */
constexpr double OFFS_CACHE_PERCENTAGE = 0.7; // 70% from cache, 30% new random

} // namespace brightchain
