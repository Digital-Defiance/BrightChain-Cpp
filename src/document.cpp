#include "brightchain/document.hpp"
#include <random>
#include <sstream>
#include <iomanip>

namespace brightchain::db {

DocumentId generateDocumentId() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;

    // Generate 16 random bytes
    uint8_t bytes[16];
    uint64_t r1 = dist(gen);
    uint64_t r2 = dist(gen);
    std::memcpy(bytes, &r1, 8);
    std::memcpy(bytes + 8, &r2, 8);

    // Set UUID v4 version bits: byte 6 high nibble = 0x4
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    // Set UUID variant bits: byte 8 high bits = 0b10xx
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    // Format as 32-char lowercase hex (no dashes)
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return oss.str();
}

DocumentId ensureDocumentId(Document& doc) {
    if (!doc.contains("_id")) {
        doc["_id"] = generateDocumentId();
    }
    return doc["_id"].get<std::string>();
}

std::vector<uint8_t> serializeDocument(const Document& doc) {
    std::string s = doc.dump(-1);
    return std::vector<uint8_t>(s.begin(), s.end());
}

Document deserializeDocument(const std::vector<uint8_t>& data) {
    return nlohmann::json::parse(data.begin(), data.end());
}

Checksum computeBlockId(const std::vector<uint8_t>& data) {
    return Checksum::fromData(data);
}

} // namespace brightchain::db
