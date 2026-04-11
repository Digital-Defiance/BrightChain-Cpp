# Implementation Plan: C++ BrightChain Document Database

## Overview

Incremental implementation of the BrightChain document database library in C++20, building on the existing `DiskBlockStore`, `Checksum`, `BlockMetadata`, and `BlockSize` classes. Each task adds a layer of functionality, starting with error types and document helpers, then HeadRegistry, QueryEngine, UpdateEngine, Collection, and finally the top-level BrightChainDb API. Test vectors and cross-platform integration tests are included for TypeScript compatibility verification.

## Tasks

- [x] 1. Create error types and document helpers
  - [x] 1.1 Create `include/brightchain/db_errors.hpp` with `ErrorCode` enum, `DbError` base class, and derived error classes (`DocumentNotFoundError`, `ValidationError`, `DuplicateKeyError`, `IndexError`, `RegistryError`)
    - Each error class stores an `ErrorCode` and message, inherits from `std::runtime_error`
    - _Requirements: 4.6, 2.5_

  - [x] 1.2 Create `include/brightchain/document.hpp` with `Document` type alias (`nlohmann::json`), `DocumentId` alias, and helper functions: `generateDocumentId()`, `ensureDocumentId()`, `serializeDocument()`, `deserializeDocument()`, `computeBlockId()`
    - `generateDocumentId` produces 32-char lowercase hex UUID v4 without dashes
    - `serializeDocument` uses compact UTF-8 JSON with no BOM
    - `computeBlockId` applies SHA3-512 via existing `Checksum` class
    - _Requirements: 3.4, 6.1, 6.2, 6.7_

  - [x] 1.3 Create `src/document.cpp` implementing the document helper functions
    - Wire `computeBlockId` to the existing `Checksum` SHA3-512 implementation
    - _Requirements: 6.1, 6.2_

  - [x] 1.4 Write unit tests for document helpers in `tests/document_test.cpp`
    - Test `generateDocumentId` produces 32-char hex strings
    - Test `ensureDocumentId` adds `_id` when missing and preserves existing `_id`
    - Test serialize/deserialize round-trip
    - Test `computeBlockId` produces deterministic checksums
    - _Requirements: 3.4, 7.1_

- [ ] 2. Implement HeadRegistry persistence
  - [x] 2.1 Create `include/brightchain/head_registry.hpp` with `HeadEntry` struct and `HeadRegistry` class
    - Public API: `load()`, `getHead()`, `setHead()`, `removeHead()`, `clear()`, `keys()`
    - Private: `persist()`, `acquireLock()`, `releaseLock()`, `nowIso8601()`
    - _Requirements: 2.1, 2.8_

  - [x] 2.2 Create `src/head_registry.cpp` implementing HeadRegistry
    - `load()`: parse both legacy format (plain string values) and current format (objects with `blockId` and `timestamp`); missing file → empty registry; invalid JSON → empty registry + warning
    - `persist()`: write to temp file then `std::filesystem::rename()` for atomicity
    - File locking via `.lock` file with `O_CREAT | O_EXCL` semantics and RAII guard
    - `setHead()` and `removeHead()` call `persist()` after mutation
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8_

  - [x] 2.3 Write unit tests for HeadRegistry in `tests/head_registry_test.cpp`
    - Test load from missing file starts empty
    - Test load from invalid JSON starts empty
    - Test load legacy format (plain string values)
    - Test load current format (object with blockId/timestamp)
    - Test setHead persists to disk and is reloadable
    - Test removeHead persists removal
    - Test clear removes all entries
    - Test atomic write (temp file + rename)
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7_

  - [x] 2.4 Write property test for HeadRegistry round-trip
    - **Property 1: HeadRegistry persist/load round-trip**
    - For all valid registry states, persisting to disk and loading produces an equivalent state
    - **Validates: Requirements 7.3**

- [x] 3. Implement StoreLock for cross-platform mutual exclusion
  - [x] 3.1 Create `include/brightchain/store_lock.hpp` with `StoreLock` class and `StoreLock::Guard` RAII wrapper
    - `acquire()`: create `.brightchain-db.lock` with `O_CREAT | O_EXCL`, retry loop (250 retries × 20ms = 5s timeout), stale lock force-removal after timeout
    - `release()`: remove lock file, no-op if not held
    - `Guard`: RAII scoped lock — acquires on construction, releases on destruction
    - Lock file path: `storePath/.brightchain-db.lock`
    - _Requirements: 12.1, 12.3, 12.4, 12.5, 12.6, 12.7_

  - [x] 3.2 Create `src/store_lock.cpp` implementing StoreLock
    - Use POSIX `open()` with `O_CREAT | O_EXCL | O_WRONLY` for cross-platform lock acquisition
    - `std::this_thread::sleep_for()` for retry delay
    - `std::filesystem::remove()` for release and stale lock cleanup
    - Throw `RegistryError` on timeout
    - _Requirements: 12.1, 12.3, 12.4, 12.5, 12.6, 12.7_

  - [x] 3.3 Write unit tests for StoreLock in `tests/store_lock_test.cpp`
    - Test acquire/release cycle
    - Test Guard RAII releases on scope exit
    - Test Guard RAII releases on exception
    - Test double-acquire from same process detects contention
    - Test timeout behavior when lock is held
    - Test stale lock recovery after timeout
    - _Requirements: 12.1, 12.3, 12.4, 12.5, 12.6, 12.7_

- [x] 4. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Implement QueryEngine
  - [x] 5.1 Create `include/brightchain/query_engine.hpp` and `src/query_engine.cpp`
    - Static `matchesFilter(doc, filter)` method with linear scan evaluation
    - Exact-match filters on document fields
    - Comparison operators: `$eq`, `$ne`, `$gt`, `$gte`, `$lt`, `$lte`
    - Set operators: `$in`, `$nin`
    - Existence operator: `$exists`
    - Logical operators: `$and`, `$or`
    - Value comparison order: null < bool < number < string < array < object
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6_

  - [x] 5.2 Write unit tests for QueryEngine in `tests/query_engine_test.cpp`
    - Test exact-match filters
    - Test each comparison operator (`$eq`, `$ne`, `$gt`, `$gte`, `$lt`, `$lte`)
    - Test set operators (`$in`, `$nin`)
    - Test `$exists` operator
    - Test logical operators (`$and`, `$or`)
    - Test cross-type comparison behavior
    - Test nested field matching
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6_

- [x] 6. Implement UpdateEngine
  - [x] 6.1 Create `include/brightchain/update_engine.hpp` and `src/update_engine.cpp`
    - Static `applyUpdate(doc, update)` returns modified copy (copy-on-write)
    - Operators: `$set`, `$unset`, `$inc`, `$push`, `$pull`, `$addToSet`, `$min`, `$max`, `$rename`, `$currentDate`, `$mul`, `$pop`
    - _Requirements: 4.4_

  - [x] 6.2 Write unit tests for UpdateEngine in `tests/update_engine_test.cpp`
    - Test each update operator individually
    - Test combined update with multiple operators
    - Test that original document is not mutated (copy-on-write)
    - _Requirements: 4.4_

- [-] 7. Implement Collection with CRUD operations
  - [x] 7.1 Create `include/brightchain/collection.hpp` with Collection class, result structs (`InsertOneResult`, `InsertManyResult`, `UpdateResult`, `DeleteResult`), `FindOptions`, and `IndexSpec`
    - Constructor takes name, DiskBlockStore ref, dbName, HeadRegistry ref, StoreLock ref, BlockSize
    - _Requirements: 3.1, 3.2, 3.3_

  - [x] 7.2 Create `src/collection.cpp` implementing Collection
    - `ensureLoaded()` / `loadFromStore()`: read head block from HeadRegistry, retrieve CollectionMeta block from DiskBlockStore, restore docIndex from `mappings`
    - `persistMeta()`: serialize CollectionMeta as JSON with `mappings` and `indexes`, compute SHA3-512, store block, update HeadRegistry
    - All write operations (`insertOne`, `insertMany`, `updateOne`, `updateMany`, `deleteOne`, `deleteMany`, `drop`) acquire `StoreLock::Guard` before reading CollectionMeta and hold it through `persistMeta()` and HeadRegistry update
    - Read-only operations (`findOne`, `find`) do NOT acquire the store lock
    - `insertOne()` / `insertMany()`: generate `_id` if missing, serialize doc, store block, update docIndex, persist meta; reject duplicate `_id` with `DuplicateKeyError`
    - `findOne()` / `find()`: load documents lazily, evaluate filter via QueryEngine, apply `skip`/`limit` from FindOptions
    - `updateOne()` / `updateMany()`: find matching docs, apply UpdateEngine, store new blocks, update docIndex, persist meta
    - `deleteOne()` / `deleteMany()`: find matching docs, remove from docIndex (not from block store), persist meta
    - `drop()`: remove head pointer from HeadRegistry, clear docIndex and docCache
    - In-memory document cache for fast repeated reads
    - Index metadata management (`createIndex`, `dropIndex`, `listIndexes`) stored in CollectionMeta but not used for query optimization
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7, 5.7, 5.8, 6.5, 6.6, 12.2, 12.10_

  - [x] 7.3 Write unit tests for Collection CRUD in `tests/collection_test.cpp`
    - Test insertOne with and without `_id`
    - Test insertMany
    - Test duplicate `_id` rejection
    - Test findOne and find with filters
    - Test find with skip/limit options
    - Test updateOne and updateMany
    - Test deleteOne and deleteMany (verify block not deleted, only mapping removed)
    - Test collection drop clears index and cache
    - Test load from store restores documents across Collection instances
    - _Requirements: 3.1, 3.2, 3.3, 3.5, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7, 5.7, 5.8_

  - [x] 7.4 Write property test for document insert/retrieve round-trip
    - **Property 2: Document insert/retrieve round-trip**
    - For all valid documents, inserting into a Collection and retrieving by DocumentId produces an equivalent document
    - **Validates: Requirements 7.1**

  - [x] 7.5 Write property test for CollectionMeta serialization round-trip
    - **Property 3: CollectionMeta serialize/deserialize round-trip**
    - For all valid CollectionMeta objects, serializing to JSON and deserializing produces an equivalent object
    - **Validates: Requirements 7.2**

- [x] 8. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 9. Implement BrightChainDb top-level API
  - [x] 9.1 Create `include/brightchain/brightchain_db.hpp` with `DbOptions` struct and `BrightChainDb` class
    - Constructor takes DiskBlockStore ref and DbOptions (name defaults to "brightchain", configurable dataDir and blockSize)
    - BrightChainDb owns both HeadRegistry and StoreLock, passes StoreLock ref to Collections
    - _Requirements: 10.1_

  - [x] 9.2 Create `src/brightchain_db.cpp` implementing BrightChainDb
    - `connect()`: load HeadRegistry from disk, set connected flag
    - `disconnect()`: clear connected flag
    - `collection(name)`: return existing or create new Collection backed by same store, HeadRegistry, and StoreLock
    - `listCollections()`: return names of all created/loaded collections
    - `dropCollection(name)`: drop the named collection, remove from internal map
    - `dropDatabase()`: drop all collections, clear HeadRegistry
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5, 10.6_

  - [x] 9.3 Write unit tests for BrightChainDb in `tests/brightchain_db_test.cpp`
    - Test connect loads HeadRegistry
    - Test collection() creates and returns collections
    - Test listCollections returns correct names
    - Test dropCollection removes collection
    - Test dropDatabase clears everything
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5, 10.6_

- [x] 10. CMake build integration
  - [x] 10.1 Add new source files to `src/CMakeLists.txt`
    - Add `document.cpp`, `head_registry.cpp`, `store_lock.cpp`, `query_engine.cpp`, `update_engine.cpp`, `collection.cpp`, `brightchain_db.cpp` to the `brightchain` library target
    - _Requirements: 11.1, 11.3_

  - [x] 10.2 Add new test files to `tests/CMakeLists.txt`
    - Add `document_test.cpp`, `head_registry_test.cpp`, `store_lock_test.cpp`, `query_engine_test.cpp`, `update_engine_test.cpp`, `collection_test.cpp`, `brightchain_db_test.cpp` to the `brightchain_tests` target
    - _Requirements: 11.4_

  - [x] 10.3 Verify build compiles without warnings
    - Ensure all new headers are in `include/brightchain/` following existing convention
    - Ensure only existing dependencies are used (OpenSSL, nlohmann_json, C++20 stdlib)
    - _Requirements: 11.2, 11.3, 11.5_

- [x] 11. Checkpoint - Ensure full build and all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 12. Database Design Specification document
  - [x] 12.1 Create `docs/DATABASE_DESIGN_SPEC.md` describing the on-disk format
    - Block storage directory layout: `storePath/blockSizeName/checksumHex[0]/checksumHex[1]/checksumHex`
    - BlockMetadata JSON sidecar format: `checksumHex.m.json` with `size`, `created_at`, `length_without_padding`
    - SHA3-512 checksum algorithm: 64-byte output, lowercase hex encoding
    - BlockSize enumeration values and directory name mapping
    - Document serialization: UTF-8 JSON with `_id` string field
    - CollectionMeta block format: `mappings` and `indexes` fields
    - HeadRegistry persistence format: `head-registry.json` with `dbName:collectionName` keys
    - Content-addressable block ID calculation (SHA3-512 of raw UTF-8 JSON bytes)
    - Copy-on-write semantics description
    - Cross-platform store-level locking protocol: lock file path, acquisition semantics, retry behavior, stale lock recovery, scope of lock (full read-modify-write cycle)
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 1.10, 1.11, 1.12, 1.13_

- [x] 13. TypeScript store-level locking update
  - [x] 13.1 Create `BrightChain/brightchain-db/src/lib/storeLock.ts` implementing a `StoreLock` class
    - Same protocol as C++: lock file at `storePath/.brightchain-db.lock`
    - `acquire()`: `fs.open(lockPath, 'wx')` with retry loop (250 retries × 20ms = 5s timeout)
    - `release()`: `fs.unlink(lockPath)`, no-op if not held
    - Stale lock force-removal after timeout exhaustion
    - _Requirements: 12.1, 12.3, 12.4, 12.5, 12.6, 12.7, 12.9_

  - [x] 13.2 Update `BrightChain/brightchain-db/src/lib/collection.ts` to acquire StoreLock around write operations
    - Wrap `insertOne`, `insertMany`, `updateOne`, `updateMany`, `deleteOne`, `deleteMany`, `replaceOne`, `drop` in StoreLock acquire/release (try/finally)
    - Read-only operations (`findOne`, `find`) do NOT acquire the lock
    - StoreLock instance passed via constructor or BrightChainDb
    - _Requirements: 12.2, 12.9, 12.10_

  - [x] 13.3 Update `BrightChain/brightchain-db/src/lib/database.ts` to create and pass StoreLock to Collections
    - BrightChainDb creates StoreLock using the store's base path
    - Pass StoreLock to each Collection on creation
    - _Requirements: 12.9_

  - [x] 13.4 Write tests for TypeScript StoreLock in `BrightChain/brightchain-db/src/__tests__/storeLock.spec.ts`
    - Test acquire/release cycle
    - Test contention behavior (two locks on same path)
    - Test stale lock recovery
    - _Requirements: 12.1, 12.3, 12.7_

- [x] 14. Cross-platform test vectors
  - [x] 14.1 Create `tests/generate_db_test_vectors.ts` TypeScript script to generate test vectors
    - Generate JSON file with known document JSON, expected SHA3-512 block IDs, expected directory paths, expected CollectionMeta JSON
    - Include test cases for each BlockSize value
    - Include test cases for CollectionMeta with multiple document mappings
    - Include test cases for HeadRegistry JSON format (both legacy and current)
    - _Requirements: 8.1, 8.4, 8.5, 8.6, 8.7_

  - [x] 14.2 Generate `tests/test_vectors_db.json` by running the TypeScript generator
    - _Requirements: 8.1_

  - [x] 14.3 Create `tests/db_cross_compat_test.cpp` C++ test that validates against test vectors
    - Verify C++ produces identical block IDs for same document JSON bytes
    - Verify C++ produces identical directory paths for same checksums and block sizes
    - _Requirements: 8.2, 8.3_

- [-] 15. End-to-end cross-platform integration test
  - [x] 15.1 Create `tests/db_e2e_ts_harness.ts` TypeScript harness
    - Create DiskBlockStore at a known temp path, instantiate BrightChainDb, create a collection, insert documents, persist head registry
    - _Requirements: 9.1_

  - [x] 15.2 Create `tests/db_e2e_cpp_test.cpp` C++ test harness
    - Open same DiskBlockStore path, load HeadRegistry, open same collection, read documents inserted by TypeScript
    - Verify retrieved documents are byte-for-byte identical to originals
    - Insert additional documents from C++ side and persist
    - Use Medium BlockSize (1048576 bytes)
    - _Requirements: 9.2, 9.3, 9.4, 9.6_

  - [x] 15.3 Create `tests/db_e2e_ts_verify.ts` TypeScript verification script
    - Read documents inserted by C++ harness and verify correctness
    - _Requirements: 9.5_

- [x] 16. Final checkpoint - Ensure full build and all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from Requirements 7.x
- The design uses C++ throughout; all code examples use C++20 with nlohmann::json
- Existing `DiskBlockStore`, `Checksum`, `BlockMetadata`, and `BlockSize` classes are reused as-is
