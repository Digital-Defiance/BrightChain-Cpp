# Requirements Document

## Introduction

This document specifies the requirements for a C++ implementation of the BrightChain document database (brightchain-db), compatible with the existing TypeScript implementation. The C++ library provides a MongoDB-like document database backed by the existing C++ DiskBlockStore, enabling cross-platform data access: a database created by the TypeScript implementation can be read and written by the C++ implementation, and vice versa. A formal Database Design Specification document is also required to publicly describe the on-disk format, serialization, and protocol for any future implementation.

## Glossary

- **BrightChainDb**: The top-level database object that manages collections, sessions, and the head registry.
- **Collection**: A named group of documents within a database, analogous to a MongoDB collection.
- **Document**: A JSON-serializable record stored as a content-addressable block. Each document has a unique `_id` field.
- **DocumentId**: An opaque string identifier for a document (32-character hex UUID without dashes by default).
- **Block**: An immutable, content-addressable unit of storage identified by its SHA3-512 checksum.
- **DiskBlockStore**: The existing C++ class that stores and retrieves blocks on disk using a directory structure of `storePath/blockSize/char1/char2/checksum`.
- **BlockMetadata**: A JSON sidecar file (`checksum.m.json`) containing block size, creation timestamp, and length without padding.
- **Checksum**: A SHA3-512 hash (64 bytes / 128 hex characters) used as the content-addressable identifier for blocks.
- **HeadRegistry**: A persistent mapping from `dbName:collectionName` keys to block IDs, tracking the latest collection metadata block for each collection.
- **CollectionMeta**: A JSON structure persisted as a block, containing `mappings` (documentId → blockChecksum) and `indexes` (index metadata array).
- **BlockSize**: An enumeration of standard block sizes (Message=512, Tiny=1024, Small=4096, Medium=1048576, Large=67108864, Huge=268435456).
- **DesignSpec**: The formal public Database Design Specification document describing the on-disk format.
- **TestVector**: A pre-computed data file containing known inputs and expected outputs for cross-platform verification.
- **CopyOnWrite**: The immutability model where blocks are never modified or deleted; only the logical mapping from DocumentId to block checksum changes.
- **StoreLock**: A cross-platform file-based lock (`storePath/.brightchain-db.lock`) that prevents concurrent processes from performing conflicting read-modify-write cycles on collection metadata. Both C++ and TypeScript implementations must respect this lock.

## Requirements

### Requirement 1: Database Design Specification Document

**User Story:** As a developer implementing a BrightChain-compatible database in any language, I want a formal public specification document describing the on-disk format, so that I can build an interoperable implementation without reading source code.

#### Acceptance Criteria

1. THE DesignSpec SHALL describe the block storage directory layout as `storePath/blockSizeName/checksumHex[0]/checksumHex[1]/checksumHex`.
2. THE DesignSpec SHALL describe the BlockMetadata JSON sidecar format including the fields `size` (integer block size in bytes), `created_at` (Unix timestamp as integer), and `length_without_padding` (integer byte count).
3. THE DesignSpec SHALL describe the metadata file naming convention as `checksumHex.m.json` adjacent to the block data file.
4. THE DesignSpec SHALL describe the SHA3-512 checksum algorithm used for content addressing, specifying 64-byte output and lowercase hexadecimal encoding.
5. THE DesignSpec SHALL describe the BlockSize enumeration values: Unknown=0, Message=512, Tiny=1024, Small=4096, Medium=1048576, Large=67108864, Huge=268435456.
6. THE DesignSpec SHALL describe the BlockSize-to-directory-name mapping: "Unknown", "Message", "Tiny", "Small", "Medium", "Large", "Huge".
7. THE DesignSpec SHALL describe the Document serialization format as UTF-8 encoded JSON with an `_id` string field.
8. THE DesignSpec SHALL describe the CollectionMeta block format as a JSON object with `mappings` (object mapping DocumentId strings to block checksum hex strings) and `indexes` (array of index descriptor objects with `name`, `spec`, `unique`, and `sparse` fields).
9. THE DesignSpec SHALL describe the HeadRegistry persistence format as a JSON file (`head-registry.json`) mapping `dbName:collectionName` keys to objects containing `blockId` (string) and optionally `timestamp` (ISO 8601 string).
10. THE DesignSpec SHALL describe the content-addressable block ID calculation for documents and collection metadata as SHA3-512 of the raw UTF-8 JSON bytes.
11. THE DesignSpec SHALL describe the CopyOnWrite semantics where blocks are immutable and document updates create new blocks with updated mappings.
12. THE DesignSpec SHALL describe the cross-platform store-level locking protocol, including the lock file path (`storePath/.brightchain-db.lock`), acquisition semantics (`O_CREAT | O_EXCL`), retry behavior, stale lock recovery, and the requirement that the lock covers the full read-modify-write cycle for collection mutations.
13. THE DesignSpec SHALL be placed in the `docs/` directory of the repository.

### Requirement 2: Head Registry Persistence

**User Story:** As a developer using the C++ database library, I want the head registry to persist across process restarts, so that collections and their documents survive application shutdown.

#### Acceptance Criteria

1. THE HeadRegistry SHALL store head pointers in a JSON file named `head-registry.json` in a configurable data directory.
2. WHEN the HeadRegistry sets a head pointer, THE HeadRegistry SHALL write the updated registry to disk atomically using a temporary file and rename.
3. WHEN the HeadRegistry loads from disk, THE HeadRegistry SHALL parse both the legacy format (plain blockId string values) and the current format (objects with `blockId` and `timestamp` fields).
4. IF the registry file does not exist on load, THEN THE HeadRegistry SHALL start with an empty registry without error.
5. IF the registry file contains invalid JSON on load, THEN THE HeadRegistry SHALL start with an empty registry and log a warning.
6. THE HeadRegistry SHALL use file-level locking via a `.lock` file to prevent concurrent write corruption.
7. WHEN the HeadRegistry removes a head pointer, THE HeadRegistry SHALL persist the updated registry to disk.
8. THE HeadRegistry SHALL store timestamps as ISO 8601 strings alongside each head pointer.

### Requirement 3: Collection Management

**User Story:** As a developer using the C++ database library, I want to create and manage document collections, so that I can organize documents into logical groups.

#### Acceptance Criteria

1. WHEN a Collection is created with a name, block store, database name, and head registry, THE Collection SHALL initialize with an empty document index and document cache.
2. WHEN a Collection loads from the store, THE Collection SHALL read the head block ID from the HeadRegistry, retrieve the CollectionMeta block, and restore the document index from the `mappings` field.
3. WHEN a Collection persists its metadata, THE Collection SHALL serialize the CollectionMeta as JSON, compute its SHA3-512 block ID, store the block in the DiskBlockStore, and update the HeadRegistry with the new block ID.
4. THE Collection SHALL generate DocumentId values as 32-character lowercase hexadecimal strings (UUID v4 without dashes) by default.
5. WHEN a Collection is dropped, THE Collection SHALL remove its head pointer from the HeadRegistry and clear its in-memory index and cache.

### Requirement 4: Document CRUD Operations

**User Story:** As a developer using the C++ database library, I want to insert, find, update, and delete JSON documents, so that I can use the database for application data storage.

#### Acceptance Criteria

1. WHEN a document is inserted, THE Collection SHALL serialize the document as UTF-8 JSON, compute its SHA3-512 block ID, store the block in the DiskBlockStore, add the DocumentId-to-blockId mapping to the index, and persist the updated CollectionMeta.
2. WHEN a document is inserted without an `_id` field, THE Collection SHALL generate a DocumentId and add the `_id` field to the document before serialization.
3. WHEN a document is retrieved by DocumentId, THE Collection SHALL look up the block checksum in the document index, retrieve the block from the DiskBlockStore, and deserialize the UTF-8 JSON.
4. WHEN a document is updated, THE Collection SHALL serialize the updated document as a new block, update the DocumentId-to-blockId mapping to point to the new block, and persist the updated CollectionMeta.
5. WHEN a document is deleted, THE Collection SHALL remove the DocumentId-to-blockId mapping from the index and persist the updated CollectionMeta, without deleting the underlying block (CopyOnWrite semantics).
6. WHEN a document with a given `_id` already exists and an insert is attempted with the same `_id`, THE Collection SHALL return an error indicating a duplicate key.
7. THE Collection SHALL cache deserialized documents in memory for fast repeated reads.

### Requirement 5: Document Query Operations

**User Story:** As a developer using the C++ database library, I want to query documents using filter expressions, so that I can find documents matching specific criteria.

#### Acceptance Criteria

1. WHEN a filter query is provided, THE Collection SHALL evaluate the filter against each document in the collection and return matching documents.
2. THE Collection SHALL support exact-match filters on document fields.
3. THE Collection SHALL support comparison operators: `$eq`, `$ne`, `$gt`, `$gte`, `$lt`, `$lte`.
4. THE Collection SHALL support set operators: `$in`, `$nin`.
5. THE Collection SHALL support the `$exists` operator to test for field presence.
6. THE Collection SHALL support logical operators: `$and`, `$or`.
7. WHEN find options include `limit`, THE Collection SHALL return at most the specified number of matching documents.
8. WHEN find options include `skip`, THE Collection SHALL skip the specified number of matching documents before returning results.

### Requirement 6: Cross-Platform Serialization Compatibility

**User Story:** As a developer, I want the C++ implementation to read databases created by the TypeScript implementation and vice versa, so that data is portable across platforms.

#### Acceptance Criteria

1. THE Collection SHALL serialize documents as JSON using UTF-8 encoding with no byte-order mark.
2. THE Collection SHALL compute block IDs by applying SHA3-512 to the exact serialized JSON byte sequence.
3. THE DiskBlockStore SHALL use the directory structure `storePath/blockSizeName/checksumHex[0]/checksumHex[1]/checksumHex` where `blockSizeName` is one of "Message", "Tiny", "Small", "Medium", "Large", "Huge".
4. THE DiskBlockStore SHALL write metadata sidecar files as `checksumHex.m.json` containing a JSON object with integer `size`, integer `created_at` (Unix timestamp), and integer `length_without_padding`.
5. THE HeadRegistry SHALL use the key format `dbName:collectionName` in the persisted JSON file.
6. THE CollectionMeta block SHALL be a JSON object with a `mappings` field (object of DocumentId to checksum hex strings) and an `indexes` field (array of index descriptor objects).
7. FOR ALL valid Document objects, serializing to JSON then computing the SHA3-512 block ID in C++ SHALL produce the same block ID as the TypeScript implementation for identical JSON byte sequences.

### Requirement 7: JSON Serialization Round-Trip

**User Story:** As a developer, I want JSON serialization to be lossless, so that documents are not corrupted during storage and retrieval.

#### Acceptance Criteria

1. FOR ALL valid Document objects, inserting a document into a Collection and then retrieving the document by its DocumentId SHALL produce a document equivalent to the original (round-trip property).
2. FOR ALL valid CollectionMeta objects, serializing to JSON and then deserializing SHALL produce an equivalent CollectionMeta object (round-trip property).
3. FOR ALL valid HeadRegistry states, persisting to disk and then loading SHALL produce an equivalent registry state (round-trip property).

### Requirement 8: Cross-Platform Test Vectors

**User Story:** As a developer, I want pre-computed test vectors that verify cross-platform compatibility, so that I can confirm the C++ and TypeScript implementations produce identical results.

#### Acceptance Criteria

1. THE test vector generator SHALL produce a JSON file containing test cases with known document JSON, expected SHA3-512 block IDs, expected directory paths, and expected CollectionMeta JSON.
2. WHEN the C++ test suite runs against the test vectors, THE test suite SHALL verify that the C++ implementation produces identical block IDs for the same document JSON bytes.
3. WHEN the C++ test suite runs against the test vectors, THE test suite SHALL verify that the C++ implementation produces identical directory paths for the same checksums and block sizes.
4. THE test vector file SHALL include test cases for each BlockSize value.
5. THE test vector file SHALL include test cases for CollectionMeta serialization with multiple document mappings.
6. THE test vector file SHALL include test cases for HeadRegistry JSON format with both legacy and current formats.
7. THE test vector generator SHALL be a TypeScript script that can be run to regenerate vectors from the TypeScript implementation.

### Requirement 9: End-to-End Cross-Platform Integration Test

**User Story:** As a developer, I want an end-to-end test that creates a database in TypeScript and reads it in C++, so that I can verify real cross-platform interoperability.

#### Acceptance Criteria

1. THE TypeScript test harness SHALL create a DiskBlockStore at a known path, instantiate a BrightChainDb, create a collection, insert documents, and persist the head registry to disk.
2. THE C++ test harness SHALL open the same DiskBlockStore path, load the HeadRegistry, open the same collection, and read the documents inserted by the TypeScript harness.
3. WHEN the C++ test harness reads documents created by the TypeScript harness, THE retrieved documents SHALL be byte-for-byte identical to the originals.
4. THE C++ test harness SHALL also insert documents into the same collection and persist the updated state.
5. THE TypeScript test harness SHALL then verify that documents inserted by the C++ harness are readable and correct.
6. THE end-to-end test SHALL use the Medium BlockSize (1048576 bytes).

### Requirement 10: BrightChainDb Top-Level API

**User Story:** As a developer using the C++ database library, I want a top-level database object that manages collections and sessions, so that I have a clean API entry point.

#### Acceptance Criteria

1. WHEN a BrightChainDb is constructed with a DiskBlockStore and options, THE BrightChainDb SHALL store the database name (defaulting to "brightchain") and initialize the HeadRegistry.
2. WHEN `connect` is called, THE BrightChainDb SHALL load the HeadRegistry from disk if using a PersistentHeadRegistry.
3. WHEN `collection` is called with a name, THE BrightChainDb SHALL return a Collection instance backed by the same DiskBlockStore and HeadRegistry, creating a new Collection if one does not already exist for that name.
4. WHEN `listCollections` is called, THE BrightChainDb SHALL return the names of all collections that have been created or loaded.
5. WHEN `dropCollection` is called with a name, THE BrightChainDb SHALL drop the named collection and remove it from the internal collection map.
6. WHEN `dropDatabase` is called, THE BrightChainDb SHALL drop all collections and clear the HeadRegistry.

### Requirement 11: C++ Library Build Integration

**User Story:** As a developer, I want the database library to integrate with the existing CMake build system, so that I can build and link it alongside the existing BrightChain C++ code.

#### Acceptance Criteria

1. THE database library source files SHALL be added to the existing `src/CMakeLists.txt` build configuration.
2. THE database library headers SHALL be placed in `include/brightchain/` following the existing header convention.
3. THE database library SHALL depend only on the existing project dependencies: OpenSSL (for SHA3-512) and nlohmann_json (for JSON serialization), plus the C++20 standard library.
4. THE database test files SHALL be added to the existing `tests/CMakeLists.txt` build configuration.
5. THE database library SHALL compile without warnings under the existing project compiler settings.

### Requirement 12: Cross-Platform Store-Level Locking

**User Story:** As a developer running both C++ and TypeScript processes against the same block store, I want a cross-platform file-based locking mechanism that prevents concurrent read-modify-write conflicts on collection metadata, so that no writes are silently lost.

#### Acceptance Criteria

1. THE database layer SHALL use a store-level lock file at `storePath/.brightchain-db.lock` to serialize write operations across processes.
2. WHEN a Collection performs a write operation (insert, update, delete, drop), THE Collection SHALL acquire the store lock before reading the current CollectionMeta, hold it through the mutation, and release it after the HeadRegistry has been updated.
3. THE store lock SHALL use `O_CREAT | O_EXCL` (or platform equivalent) file creation semantics for cross-platform compatibility between C++ and TypeScript processes.
4. IF the store lock cannot be acquired within a configurable timeout (default 5 seconds), THEN the operation SHALL fail with a descriptive error rather than blocking indefinitely.
5. THE store lock SHALL include a retry loop with configurable delay (default 20ms) to handle transient contention.
6. THE store lock SHALL be released in all cases, including error paths, using RAII (C++) or try/finally (TypeScript) patterns.
7. IF a stale lock file is detected (e.g., the owning process crashed), THE lock acquisition SHALL force-remove the stale lock after the timeout expires and retry once.
8. THE store lock protocol SHALL be documented in the Database Design Specification (Requirement 1) so that any future implementation can interoperate.
9. THE TypeScript `PersistentHeadRegistry` and `Collection` SHALL be updated to acquire the store lock around the full read-modify-write cycle for collection mutations, matching the C++ behavior.
10. Read-only operations (find, findOne) SHALL NOT require the store lock, but SHALL reload the CollectionMeta from the HeadRegistry if the in-memory state may be stale.
