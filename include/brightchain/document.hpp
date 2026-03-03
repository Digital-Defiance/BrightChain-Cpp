#pragma once

#include <brightchain/checksum.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace brightchain::db {

/// Document is a JSON object with a string `_id` field.
using Document = nlohmann::json;

/// DocumentId is a 32-character lowercase hex string (UUID v4 without dashes).
using DocumentId = std::string;

/**
 * Generate a 32-char lowercase hex UUID v4 (no dashes).
 */
DocumentId generateDocumentId();

/**
 * Ensure the document has an `_id` field. If missing, generates one.
 * @param doc The document to check/modify.
 * @return The document's `_id` value.
 */
DocumentId ensureDocumentId(Document& doc);

/**
 * Serialize a document to compact UTF-8 JSON bytes (no BOM).
 * @param doc The document to serialize.
 * @return UTF-8 encoded JSON byte vector.
 */
std::vector<uint8_t> serializeDocument(const Document& doc);

/**
 * Deserialize UTF-8 JSON bytes to a document.
 * @param data The raw JSON bytes.
 * @return Parsed document.
 */
Document deserializeDocument(const std::vector<uint8_t>& data);

/**
 * Compute the content-addressable block ID (SHA3-512) for raw data bytes.
 * @param data The serialized data bytes.
 * @return SHA3-512 checksum.
 */
Checksum computeBlockId(const std::vector<uint8_t>& data);

} // namespace brightchain::db
