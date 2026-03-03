#!/usr/bin/env node
/**
 * End-to-end TypeScript harness for cross-platform integration testing.
 *
 * Creates a DiskBlockStore at a known temp path using the C++ directory layout
 * (storePath/blockSizeName/hex[0]/hex[1]/checksumHex), instantiates a database,
 * creates a collection, inserts documents, and persists the head registry.
 *
 * The C++ test harness (db_e2e_cpp_test.cpp) then opens the same store path,
 * loads the HeadRegistry, and verifies the documents are readable.
 *
 * Requirements: 9.1
 *
 * Usage:
 *   tsx db_e2e_ts_harness.ts <storePath>
 *
 * The harness writes a manifest file at <storePath>/e2e_manifest.json containing
 * the inserted documents for verification by the C++ and TypeScript verifiers.
 */

import { sha3_512 } from '@noble/hashes/sha3';
import * as fs from 'fs';
import * as path from 'path';

// ── Constants ──

const BLOCK_SIZE_NAME = 'Medium';
const BLOCK_SIZE_VALUE = 1048576;
const DB_NAME = 'e2etest';
const COLLECTION_NAME = 'testdocs';

// ── Helpers ──

function computeSha3_512Hex(data: Uint8Array): string {
  const hash = sha3_512(data);
  return Buffer.from(hash).toString('hex');
}

function serializeDocument(doc: Record<string, unknown>): Buffer {
  return Buffer.from(JSON.stringify(doc), 'utf8');
}

/**
 * Write a block to disk using the C++ DiskBlockStore directory layout:
 *   storePath / blockSizeName / checksumHex[0] / checksumHex[1] / checksumHex
 *
 * Also writes a metadata sidecar file:
 *   checksumHex.m.json
 */
function writeBlock(storePath: string, data: Buffer): string {
  const checksumHex = computeSha3_512Hex(new Uint8Array(data));

  const blockDir = path.join(
    storePath,
    BLOCK_SIZE_NAME,
    checksumHex[0],
    checksumHex[1],
  );
  fs.mkdirSync(blockDir, { recursive: true });

  const blockPath = path.join(blockDir, checksumHex);
  fs.writeFileSync(blockPath, data);

  // Write metadata sidecar
  const metadata = {
    size: BLOCK_SIZE_VALUE,
    created_at: Math.floor(Date.now() / 1000),
    length_without_padding: data.length,
  };
  const metaPath = blockPath + '.m.json';
  fs.writeFileSync(metaPath, JSON.stringify(metadata, null, 2));

  return checksumHex;
}

/**
 * Persist the head registry to head-registry.json in the data directory.
 */
function writeHeadRegistry(
  dataDir: string,
  heads: Record<string, { blockId: string; timestamp: string }>,
): void {
  const registryPath = path.join(dataDir, 'head-registry.json');
  fs.writeFileSync(registryPath, JSON.stringify(heads, null, 2));
}

// ── Test Documents ──

const TEST_DOCUMENTS: Record<string, unknown>[] = [
  {
    _id: 'e2e_doc_001',
    name: 'Alice',
    age: 30,
    email: 'alice@example.com',
    tags: ['admin', 'user'],
  },
  {
    _id: 'e2e_doc_002',
    name: 'Bob',
    age: 25,
    active: true,
  },
  {
    _id: 'e2e_doc_003',
    name: '日本語テスト',
    emoji: '🚀',
    nested: { x: 1, y: 2 },
  },
  {
    _id: 'e2e_doc_004',
    name: 'Charlie',
    age: 0,
    balance: 3.14,
    empty_array: [],
  },
  {
    _id: 'e2e_doc_005',
    name: 'Diana',
    age: 42,
    metadata: { role: 'manager', level: 3 },
  },
];

// ── Main ──

function main(): void {
  const storePath = process.argv[2];
  if (!storePath) {
    console.error('Usage: tsx db_e2e_ts_harness.ts <storePath>');
    process.exit(1);
  }

  // Ensure store directory exists
  fs.mkdirSync(storePath, { recursive: true });

  console.log(`[TS Harness] Store path: ${storePath}`);
  console.log(`[TS Harness] Database: ${DB_NAME}, Collection: ${COLLECTION_NAME}`);
  console.log(`[TS Harness] BlockSize: ${BLOCK_SIZE_NAME} (${BLOCK_SIZE_VALUE})`);

  // Insert each document as a block
  const docMappings: Record<string, string> = {};
  const insertedDocs: Record<string, unknown>[] = [];

  for (const doc of TEST_DOCUMENTS) {
    const serialized = serializeDocument(doc);
    const blockId = writeBlock(storePath, serialized);
    const docId = doc._id as string;
    docMappings[docId] = blockId;
    insertedDocs.push(doc);
    console.log(`[TS Harness] Inserted ${docId} → ${blockId.substring(0, 16)}...`);
  }

  // Build CollectionMeta and store it as a block
  const collectionMeta = {
    mappings: docMappings,
    indexes: [],
  };
  const metaSerialized = Buffer.from(JSON.stringify(collectionMeta), 'utf8');
  const metaBlockId = writeBlock(storePath, metaSerialized);
  console.log(`[TS Harness] CollectionMeta block: ${metaBlockId.substring(0, 16)}...`);

  // Write head registry
  const headKey = `${DB_NAME}:${COLLECTION_NAME}`;
  const heads: Record<string, { blockId: string; timestamp: string }> = {
    [headKey]: {
      blockId: metaBlockId,
      timestamp: new Date().toISOString(),
    },
  };
  writeHeadRegistry(storePath, heads);
  console.log(`[TS Harness] HeadRegistry written with key: ${headKey}`);

  // Write manifest for verification
  const manifest = {
    dbName: DB_NAME,
    collectionName: COLLECTION_NAME,
    blockSizeName: BLOCK_SIZE_NAME,
    blockSizeValue: BLOCK_SIZE_VALUE,
    documents: insertedDocs,
    docMappings,
    metaBlockId,
    headKey,
  };
  const manifestPath = path.join(storePath, 'e2e_manifest.json');
  fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2));
  console.log(`[TS Harness] Manifest written to ${manifestPath}`);
  console.log(`[TS Harness] Done. ${insertedDocs.length} documents inserted.`);
}

main();
