#!/usr/bin/env node
/**
 * Generate cross-platform test vectors for the BrightChain document database.
 *
 * Produces test_vectors_db.json with:
 *   - Document serialization + SHA3-512 block IDs
 *   - Expected directory paths for each BlockSize
 *   - CollectionMeta serialization with multiple document mappings
 *   - HeadRegistry JSON format (legacy and current)
 *
 * Requirements: 8.1, 8.4, 8.5, 8.6, 8.7
 */

import { sha3_512 } from '@noble/hashes/sha3';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// ── BlockSize enum matching C++ and TypeScript implementations ──

const BLOCK_SIZES: Record<string, number> = {
  Message: 512,
  Tiny: 1024,
  Small: 4096,
  Medium: 1048576,
  Large: 67108864,
  Huge: 268435456,
};

// ── Helpers ──

function computeSha3_512Hex(data: Uint8Array): string {
  const hash = sha3_512(data);
  return Buffer.from(hash).toString('hex');
}

function serializeDocument(doc: Record<string, unknown>): Buffer {
  return Buffer.from(JSON.stringify(doc), 'utf8');
}

/**
 * Build the expected directory path for a block:
 *   storePath / blockSizeName / checksumHex[0] / checksumHex[1] / checksumHex
 */
function expectedBlockPath(
  storePath: string,
  blockSizeName: string,
  checksumHex: string,
): string {
  return [
    storePath,
    blockSizeName,
    checksumHex[0],
    checksumHex[1],
    checksumHex,
  ].join('/');
}

// ── Vector generation ──

function generateDocumentVectors() {
  const documents = [
    { _id: 'abc123', name: 'Alice', age: 30 },
    { _id: 'def456', name: 'Bob', age: 25, tags: ['admin', 'user'] },
    {
      _id: '00000000000000000000000000000001',
      nested: { x: 1, y: 2 },
      flag: true,
    },
    { _id: 'empty_fields' },
    {
      _id: 'unicode_doc',
      text: '日本語テスト',
      emoji: '🚀',
    },
    {
      _id: 'numeric_types',
      integer: 42,
      float_val: 3.14,
      negative: -100,
      zero: 0,
    },
  ];

  return documents.map((doc) => {
    const serialized = serializeDocument(doc);
    const blockId = computeSha3_512Hex(serialized);
    return {
      description: `Document with _id="${doc._id}"`,
      document: doc,
      serializedJson: serialized.toString('utf8'),
      serializedHex: serialized.toString('hex'),
      blockId,
    };
  });
}

function generateDirectoryPathVectors() {
  // Use a known checksum to verify path construction for each block size
  const knownDoc = { _id: 'path_test', value: 42 };
  const serialized = serializeDocument(knownDoc);
  const checksumHex = computeSha3_512Hex(serialized);
  const storePath = '/tmp/store';

  return Object.entries(BLOCK_SIZES).map(([name, size]) => ({
    description: `Directory path for BlockSize::${name} (${size})`,
    blockSizeName: name,
    blockSizeValue: size,
    checksumHex,
    storePath,
    expectedPath: expectedBlockPath(storePath, name, checksumHex),
    expectedMetadataPath:
      expectedBlockPath(storePath, name, checksumHex) + '.m.json',
  }));
}

function generateCollectionMetaVectors() {
  // Single mapping
  const singleMeta = {
    mappings: {
      abc123:
        'a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4',
    },
    indexes: [],
  };

  // Multiple mappings
  const multiMeta = {
    mappings: {
      doc001:
        'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
      doc002:
        'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',
      doc003:
        'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
    },
    indexes: [
      { name: 'email_unique', spec: { email: 1 }, unique: true, sparse: false },
    ],
  };

  // Empty collection
  const emptyMeta = {
    mappings: {},
    indexes: [],
  };

  return [
    {
      description: 'CollectionMeta with single document mapping',
      meta: singleMeta,
      serializedJson: JSON.stringify(singleMeta),
      blockId: computeSha3_512Hex(
        Buffer.from(JSON.stringify(singleMeta), 'utf8'),
      ),
    },
    {
      description: 'CollectionMeta with multiple document mappings and index',
      meta: multiMeta,
      serializedJson: JSON.stringify(multiMeta),
      blockId: computeSha3_512Hex(
        Buffer.from(JSON.stringify(multiMeta), 'utf8'),
      ),
    },
    {
      description: 'CollectionMeta for empty collection',
      meta: emptyMeta,
      serializedJson: JSON.stringify(emptyMeta),
      blockId: computeSha3_512Hex(
        Buffer.from(JSON.stringify(emptyMeta), 'utf8'),
      ),
    },
  ];
}

function generateHeadRegistryVectors() {
  // Legacy format: plain string values
  const legacyRegistry: Record<string, string> = {
    'mydb:users':
      'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
    'mydb:orders':
      'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',
  };

  // Current format: objects with blockId and timestamp
  const currentRegistry: Record<
    string,
    { blockId: string; timestamp: string }
  > = {
    'mydb:users': {
      blockId:
        'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
      timestamp: '2026-01-15T10:30:00.000Z',
    },
    'mydb:orders': {
      blockId:
        'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',
      timestamp: '2026-01-15T11:00:00.000Z',
    },
  };

  // Empty registry
  const emptyRegistry = {};

  return [
    {
      description: 'HeadRegistry legacy format (plain string values)',
      format: 'legacy',
      json: legacyRegistry,
      serializedJson: JSON.stringify(legacyRegistry),
      entries: Object.entries(legacyRegistry).map(([key, blockId]) => ({
        key,
        blockId,
      })),
    },
    {
      description:
        'HeadRegistry current format (objects with blockId and timestamp)',
      format: 'current',
      json: currentRegistry,
      serializedJson: JSON.stringify(currentRegistry),
      entries: Object.entries(currentRegistry).map(([key, entry]) => ({
        key,
        blockId: entry.blockId,
        timestamp: entry.timestamp,
      })),
    },
    {
      description: 'HeadRegistry empty',
      format: 'current',
      json: emptyRegistry,
      serializedJson: JSON.stringify(emptyRegistry),
      entries: [],
    },
  ];
}

function generateBlockSizeVectors() {
  // For each block size, serialize a document and compute its block ID
  const doc = { _id: 'blocksize_test', data: 'hello world' };
  const serialized = serializeDocument(doc);
  const blockId = computeSha3_512Hex(serialized);

  return Object.entries(BLOCK_SIZES).map(([name, size]) => ({
    blockSizeName: name,
    blockSizeValue: size,
    document: doc,
    serializedJson: serialized.toString('utf8'),
    blockId,
    expectedDir: `${name}/${blockId[0]}/${blockId[1]}`,
  }));
}

// ── Main ──

function main() {
  const vectors = {
    description:
      'Cross-platform test vectors for BrightChain document database',
    timestamp: new Date().toISOString(),
    documentVectors: generateDocumentVectors(),
    directoryPathVectors: generateDirectoryPathVectors(),
    collectionMetaVectors: generateCollectionMetaVectors(),
    headRegistryVectors: generateHeadRegistryVectors(),
    blockSizeVectors: generateBlockSizeVectors(),
  };

  const outputPath = path.join(__dirname, 'test_vectors_db.json');
  fs.writeFileSync(outputPath, JSON.stringify(vectors, null, 2));
  console.log(`Generated DB test vectors at ${outputPath}`);
  console.log(
    `  ${vectors.documentVectors.length} document vectors`,
  );
  console.log(
    `  ${vectors.directoryPathVectors.length} directory path vectors`,
  );
  console.log(
    `  ${vectors.collectionMetaVectors.length} collection meta vectors`,
  );
  console.log(
    `  ${vectors.headRegistryVectors.length} head registry vectors`,
  );
  console.log(
    `  ${vectors.blockSizeVectors.length} block size vectors`,
  );
}

main();
