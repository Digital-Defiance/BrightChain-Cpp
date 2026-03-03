# BrightChain Database Design Specification

Version 1.0

## 1. Introduction

This document is the formal public specification for the BrightChain document database on-disk format, serialization conventions, and cross-platform protocols. Any implementation — in C++, TypeScript, or any other language — that conforms to this specification will be able to read and write databases created by any other conforming implementation.

BrightChain-db is a MongoDB-like document database backed by a content-addressable block store. Documents are JSON objects stored as immutable blocks identified by their SHA3-512 checksum. Collections organize documents into named groups, and a head registry tracks the latest metadata block for each collection.

## 2. Block Storage

### 2.1 Directory Layout

Blocks are stored on the filesystem under a root `storePath` directory. The path to a block file is:

```
storePath / blockSizeName / checksumHex[0] / checksumHex[1] / checksumHex
```

Where:

- `storePath` — the root directory for the block store, chosen at construction time.
- `blockSizeName` — the human-readable name of the block size (see Section 2.3).
- `checksumHex[0]` — the first character of the lowercase hex-encoded checksum.
- `checksumHex[1]` — the second character of the lowercase hex-encoded checksum.
- `checksumHex` — the full 128-character lowercase hex-encoded SHA3-512 checksum.

Example for a block with checksum `a1b2c3...` stored at Medium block size:

```
storePath/Medium/a/1/a1b2c3...
```

### 2.2 Checksum Algorithm

All content addressing uses SHA3-512 (Keccak-based, FIPS 202).

- Output: 64 bytes (512 bits).
- Encoding: 128-character lowercase hexadecimal string.
- Input: the raw byte content of the block (no framing, no length prefix).

The checksum is computed over the exact bytes written to disk. For documents and collection metadata, this is the compact UTF-8 JSON serialization (see Section 3).

### 2.3 Block Sizes

The `BlockSize` enumeration defines the standard block sizes. Each value is the size in bytes.

| Name    | Value       | Bytes   | Exponent (2^x) |
|---------|-------------|---------|-----------------|
| Unknown | 0           | 0       | —               |
| Message | 512         | 512 B   | 9               |
| Tiny    | 1024        | 1 KB    | 10              |
| Small   | 4096        | 4 KB    | 12              |
| Medium  | 1048576     | 1 MB    | 20              |
| Large   | 67108864    | 64 MB   | 26              |
| Huge    | 268435456   | 256 MB  | 28              |

The directory name for each block size is its human-readable name: `"Unknown"`, `"Message"`, `"Tiny"`, `"Small"`, `"Medium"`, `"Large"`, `"Huge"`.

`Unknown` (0) is reserved and must not be used for storage.

### 2.4 Block Metadata Sidecar

Each block has an optional JSON metadata sidecar file stored adjacent to the block data file. The sidecar path is:

```
storePath / blockSizeName / checksumHex[0] / checksumHex[1] / checksumHex.m.json
```

The sidecar is a JSON object with the following fields:

| Field                    | Type    | Description                                      |
|--------------------------|---------|--------------------------------------------------|
| `size`                   | integer | Block size in bytes (the `BlockSize` enum value). |
| `created_at`             | integer | Unix timestamp (seconds since epoch) of creation. |
| `length_without_padding` | integer | Actual data length in bytes before any padding.   |

Example:

```json
{
  "size": 1048576,
  "created_at": 1709337600,
  "length_without_padding": 247
}
```

## 3. Document Serialization

### 3.1 Document Format

A document is a JSON object. Every stored document must contain a string `_id` field that serves as its unique identifier within a collection.

### 3.2 DocumentId

A `DocumentId` is a 32-character lowercase hexadecimal string derived from a UUID v4 with dashes removed. Implementations must generate DocumentIds in this format when a document is inserted without an `_id` field.

Example: `"550e8400e29b41d4a716446655440000"`

### 3.3 Serialization

Documents are serialized to bytes using compact JSON encoding:

- Encoding: UTF-8, no byte-order mark (BOM).
- Format: compact (no extra whitespace or newlines). Equivalent to `JSON.stringify(doc)` in JavaScript or `nlohmann::json::dump(-1)` in C++.
- The serialized bytes are the exact input to the SHA3-512 checksum computation.

### 3.4 Content-Addressable Block ID

The block ID for a document (or any block) is computed as:

```
blockId = SHA3-512( serializedBytes )
```

Where `serializedBytes` is the exact UTF-8 JSON byte sequence produced by compact serialization. The resulting checksum is encoded as a 128-character lowercase hex string.

For cross-platform compatibility, implementations must produce identical byte sequences for the same logical JSON document. This means key ordering in serialization must be deterministic and consistent across implementations.

## 4. Collection Metadata

### 4.1 CollectionMeta Block

Each collection's state is persisted as a `CollectionMeta` block in the block store. This block is a JSON object with two fields:

| Field      | Type   | Description                                                    |
|------------|--------|----------------------------------------------------------------|
| `mappings` | object | Maps `DocumentId` strings to block checksum hex strings.       |
| `indexes`  | array  | Array of index descriptor objects (metadata only).             |

Example:

```json
{
  "mappings": {
    "550e8400e29b41d4a716446655440000": "a1b2c3d4e5f6...",
    "660f9511f30c52e5b827557766551111": "b2c3d4e5f6a7..."
  },
  "indexes": [
    {
      "name": "email_unique",
      "spec": {"email": 1},
      "unique": true,
      "sparse": false
    }
  ]
}
```

### 4.2 Index Descriptor

Each entry in the `indexes` array is a JSON object with:

| Field    | Type    | Description                                          |
|----------|---------|------------------------------------------------------|
| `name`   | string  | Unique name for the index.                           |
| `spec`   | object  | Field specification (e.g., `{"field": 1}`).          |
| `unique` | boolean | Whether the index enforces uniqueness.               |
| `sparse` | boolean | Whether the index skips documents missing the field. |

Index metadata is stored for future use but is not currently used for query optimization. Queries use linear scan.

### 4.3 CollectionMeta Persistence

The CollectionMeta block is stored in the block store like any other block:

1. Serialize the CollectionMeta JSON to compact UTF-8 bytes.
2. Compute the SHA3-512 checksum of those bytes.
3. Store the bytes in the block store at the computed checksum path.
4. Update the HeadRegistry (Section 5) with the new checksum.

## 5. Head Registry

### 5.1 Purpose

The head registry is a persistent mapping that tracks the latest `CollectionMeta` block ID for each collection. It enables the database to locate collection state across process restarts.

### 5.2 File Format

The head registry is stored as a JSON file named `head-registry.json` in a configurable data directory.

Keys are formatted as `dbName:collectionName` (e.g., `"brightchain:users"`).

#### Current Format

Values are JSON objects with `blockId` and optionally `timestamp`:

```json
{
  "brightchain:users": {
    "blockId": "a1b2c3d4e5f6...",
    "timestamp": "2025-03-01T12:00:00Z"
  },
  "brightchain:orders": {
    "blockId": "b2c3d4e5f6a7...",
    "timestamp": "2025-03-01T12:05:00Z"
  }
}
```

#### Legacy Format

Older implementations may store plain string values (block ID only, no timestamp). Conforming implementations must be able to read this format:

```json
{
  "brightchain:users": "a1b2c3d4e5f6...",
  "brightchain:orders": "b2c3d4e5f6a7..."
}
```

When reading legacy entries, the timestamp should be treated as absent.

### 5.3 Atomic Writes

The head registry must be written atomically to prevent corruption on crash:

1. Write the updated JSON to a temporary file in the same directory.
2. Rename (move) the temporary file to `head-registry.json`.

On POSIX systems, `rename()` is atomic within the same filesystem. Implementations must use this pattern or an equivalent atomic write mechanism.

### 5.4 Registry-Level Locking

The head registry uses its own file lock (`head-registry.json.lock`) to prevent concurrent write corruption from multiple threads or processes writing to the registry file simultaneously. This lock is separate from the store-level lock (Section 6) and protects only the registry file itself.

- Lock file: `dataDir/head-registry.json.lock`
- Acquisition: `O_CREAT | O_EXCL` (or platform equivalent).
- Release: remove the lock file after the write completes.

### 5.5 Error Handling

- If `head-registry.json` does not exist on load, the registry starts empty without error.
- If `head-registry.json` contains invalid JSON on load, the registry starts empty and a warning should be logged.

## 6. Store-Level Locking Protocol

### 6.1 Purpose

The store-level lock serializes write operations (insert, update, delete, drop) across all processes — including C++ and TypeScript — that access the same block store. This prevents lost updates when concurrent processes perform read-modify-write cycles on collection metadata.

Block writes themselves are safe without locking because blocks are content-addressable and immutable: two processes writing the same content produce the same checksum and the same file path, so the write is idempotent.

### 6.2 Lock File

The lock file is located at:

```
storePath / .brightchain-db.lock
```

Where `storePath` is the root directory of the block store. All implementations must use this exact path.

### 6.3 Acquisition

Lock acquisition uses exclusive file creation semantics:

- POSIX: `open(lockPath, O_CREAT | O_EXCL | O_WRONLY, 0644)`
- Node.js: `fs.open(lockPath, 'wx')`

If the file already exists, the lock is held by another process and acquisition fails.

### 6.4 Retry Behavior

On acquisition failure, the implementation retries with the following defaults:

| Parameter   | Default | Description                          |
|-------------|---------|--------------------------------------|
| Max retries | 250     | Maximum number of retry attempts.    |
| Retry delay | 20 ms   | Delay between retry attempts.        |
| Timeout     | 5 s     | Total timeout (retries × delay).     |

If all retries are exhausted without acquiring the lock, the implementation proceeds to stale lock recovery (Section 6.5).

### 6.5 Stale Lock Recovery

If the lock cannot be acquired after exhausting all retries, the lock file is assumed to be stale (left behind by a crashed process). The implementation:

1. Force-removes the lock file.
2. Attempts acquisition once more.
3. If the second attempt also fails, the operation fails with a descriptive error.

### 6.6 Release

Lock release removes the lock file from the filesystem. Release must be safe to call if the lock is not held (no-op). Implementations must guarantee release on all exit paths:

- C++: RAII guard pattern (acquire on construction, release on destruction).
- TypeScript: `try/finally` pattern.

### 6.7 Scope of Lock

The store lock must be held for the entire read-modify-write cycle of a collection mutation:

1. Acquire lock.
2. Reload CollectionMeta from the HeadRegistry and block store (to pick up changes from other processes).
3. Perform the mutation (insert, update, delete).
4. Persist the updated CollectionMeta block.
5. Update the HeadRegistry with the new block ID.
6. Release lock.

Read-only operations (`find`, `findOne`) do not acquire the store lock.

## 7. Copy-on-Write Semantics

Blocks in the store are immutable. They are never modified or deleted during normal operations.

When a document is updated:

1. The updated document is serialized to a new byte sequence.
2. A new block is created with the new content and its SHA3-512 checksum.
3. The collection's `mappings` entry for that `DocumentId` is updated to point to the new block's checksum.
4. A new CollectionMeta block is created and persisted.

When a document is deleted:

1. The `DocumentId` entry is removed from the collection's `mappings`.
2. A new CollectionMeta block is created and persisted.
3. The original document block is not removed from the store.

Old blocks (previous document versions, previous CollectionMeta blocks) remain in the store indefinitely. This provides a natural audit trail and enables future features like versioning or garbage collection.

## 8. BrightChainDb API Structure

The top-level `BrightChainDb` object manages the lifecycle:

- Owns the `HeadRegistry` and `StoreLock` instances.
- Creates `Collection` instances on demand, passing references to the shared `HeadRegistry`, `StoreLock`, and `DiskBlockStore`.
- `connect()` loads the HeadRegistry from disk.
- `disconnect()` marks the database as disconnected.
- `dropDatabase()` drops all collections and clears the HeadRegistry.

The database name defaults to `"brightchain"` and is used as the prefix in HeadRegistry keys (`dbName:collectionName`).

## 9. Cross-Platform Compatibility Requirements

For two implementations to interoperate, they must agree on:

1. The SHA3-512 algorithm and its lowercase hex encoding.
2. The block storage directory layout (Section 2.1).
3. The metadata sidecar format (Section 2.4).
4. The document serialization format — compact UTF-8 JSON producing identical bytes for the same logical document (Section 3.3).
5. The CollectionMeta block format (Section 4.1).
6. The HeadRegistry file format, including legacy compatibility (Section 5.2).
7. The store-level locking protocol (Section 6).
8. The HeadRegistry key format: `dbName:collectionName` (Section 5.2).

Implementations should provide cross-platform test vectors (pre-computed checksums for known documents) and end-to-end integration tests (one implementation writes, another reads) to verify conformance.
