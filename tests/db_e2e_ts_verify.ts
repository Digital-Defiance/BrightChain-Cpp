#!/usr/bin/env node
/**
 * End-to-end TypeScript verification script for cross-platform integration testing.
 *
 * Reads documents inserted by the C++ harness and verifies correctness.
 * This script is the final step in the e2e flow:
 *   1. TypeScript harness creates store and inserts documents
 *   2. C++ harness reads TS documents, inserts more, writes e2e_cpp_manifest.json
 *   3. This script reads C++ documents and verifies they are correct
 *
 * Requirements: 9.5
 *
 * Usage:
 *   tsx db_e2e_ts_verify.ts <storePath>
 */

import { sha3_512 } from '@noble/hashes/sha3';
import * as fs from 'fs';
import * as path from 'path';

// ── Constants ──

const BLOCK_SIZE_NAME = 'Medium';

// ── Helpers ──

function computeSha3_512Hex(data: Uint8Array): string {
  const hash = sha3_512(data);
  return Buffer.from(hash).toString('hex');
}

/**
 * Read a block from disk using the DiskBlockStore directory layout:
 *   storePath / blockSizeName / checksumHex[0] / checksumHex[1] / checksumHex
 */
function readBlock(storePath: string, checksumHex: string): Buffer {
  const blockPath = path.join(
    storePath,
    BLOCK_SIZE_NAME,
    checksumHex[0],
    checksumHex[1],
    checksumHex,
  );
  return fs.readFileSync(blockPath);
}

/**
 * Load the head registry from head-registry.json.
 */
function loadHeadRegistry(
  dataDir: string,
): Record<string, { blockId: string; timestamp?: string }> {
  const registryPath = path.join(dataDir, 'head-registry.json');
  const content = fs.readFileSync(registryPath, 'utf8');
  return JSON.parse(content);
}

/**
 * Load CollectionMeta from a block.
 */
function loadCollectionMeta(
  storePath: string,
  metaBlockId: string,
): { mappings: Record<string, string>; indexes: unknown[] } {
  const raw = readBlock(storePath, metaBlockId);
  return JSON.parse(raw.toString('utf8'));
}

// ── Verification ──

interface VerifyResult {
  passed: number;
  failed: number;
  errors: string[];
}

function verify(storePath: string): VerifyResult {
  const result: VerifyResult = { passed: 0, failed: 0, errors: [] };

  // Load the C++ manifest
  const cppManifestPath = path.join(storePath, 'e2e_cpp_manifest.json');
  if (!fs.existsSync(cppManifestPath)) {
    result.failed++;
    result.errors.push('e2e_cpp_manifest.json not found — C++ harness did not run');
    return result;
  }

  const cppManifest = JSON.parse(fs.readFileSync(cppManifestPath, 'utf8'));
  const dbName: string = cppManifest.dbName;
  const collectionName: string = cppManifest.collectionName;
  const cppDocuments: Record<string, unknown>[] = cppManifest.cppDocuments;
  const expectedTotalCount: number = cppManifest.totalDocCount;

  console.log(`[TS Verify] Store path: ${storePath}`);
  console.log(`[TS Verify] Database: ${dbName}, Collection: ${collectionName}`);
  console.log(`[TS Verify] Expected C++ documents: ${cppDocuments.length}`);
  console.log(`[TS Verify] Expected total documents: ${expectedTotalCount}`);

  // Load the head registry (updated by C++)
  const headKey = `${dbName}:${collectionName}`;
  const heads = loadHeadRegistry(storePath);

  if (!heads[headKey]) {
    result.failed++;
    result.errors.push(`HeadRegistry missing key: ${headKey}`);
    return result;
  }

  const metaBlockId = heads[headKey].blockId;
  console.log(`[TS Verify] HeadRegistry meta block: ${metaBlockId.substring(0, 16)}...`);

  // Load CollectionMeta
  const meta = loadCollectionMeta(storePath, metaBlockId);
  const mappings = meta.mappings;
  const totalMappings = Object.keys(mappings).length;

  console.log(`[TS Verify] CollectionMeta has ${totalMappings} document mappings`);

  // Verify total document count
  if (totalMappings === expectedTotalCount) {
    result.passed++;
    console.log(`[TS Verify] ✓ Total document count matches: ${totalMappings}`);
  } else {
    result.failed++;
    result.errors.push(
      `Total document count mismatch: expected ${expectedTotalCount}, got ${totalMappings}`,
    );
  }

  // Verify each C++ document is present and correct
  for (const expectedDoc of cppDocuments) {
    const docId = expectedDoc._id as string;

    // Check mapping exists
    if (!mappings[docId]) {
      result.failed++;
      result.errors.push(`Document ${docId} not found in CollectionMeta mappings`);
      continue;
    }

    const blockId = mappings[docId];

    // Read the raw block
    let rawBlock: Buffer;
    try {
      rawBlock = readBlock(storePath, blockId);
    } catch (err) {
      result.failed++;
      result.errors.push(`Failed to read block for ${docId}: ${(err as Error).message}`);
      continue;
    }

    // Verify content-addressable integrity: SHA3-512 of block data must match blockId
    const actualHash = computeSha3_512Hex(new Uint8Array(rawBlock));
    if (actualHash !== blockId) {
      result.failed++;
      result.errors.push(
        `Block ID mismatch for ${docId}: expected ${blockId.substring(0, 16)}..., got ${actualHash.substring(0, 16)}...`,
      );
      continue;
    }
    result.passed++;

    // Deserialize and verify document content
    let doc: Record<string, unknown>;
    try {
      doc = JSON.parse(rawBlock.toString('utf8'));
    } catch (err) {
      result.failed++;
      result.errors.push(`Failed to parse JSON for ${docId}: ${(err as Error).message}`);
      continue;
    }

    // Verify each field matches
    let docOk = true;
    for (const [key, expectedValue] of Object.entries(expectedDoc)) {
      const actualValue = doc[key];
      if (JSON.stringify(actualValue) !== JSON.stringify(expectedValue)) {
        result.failed++;
        result.errors.push(
          `Field '${key}' mismatch in ${docId}: expected ${JSON.stringify(expectedValue)}, got ${JSON.stringify(actualValue)}`,
        );
        docOk = false;
      }
    }

    if (docOk) {
      result.passed++;
      console.log(`[TS Verify] ✓ Document ${docId} verified`);
    }
  }

  // Also verify the original TypeScript documents are still present
  const tsManifestPath = path.join(storePath, 'e2e_manifest.json');
  if (fs.existsSync(tsManifestPath)) {
    const tsManifest = JSON.parse(fs.readFileSync(tsManifestPath, 'utf8'));
    const tsDocs: Record<string, unknown>[] = tsManifest.documents;

    for (const tsDoc of tsDocs) {
      const docId = tsDoc._id as string;
      if (mappings[docId]) {
        result.passed++;
        console.log(`[TS Verify] ✓ TS document ${docId} still present in mappings`);
      } else {
        result.failed++;
        result.errors.push(`TS document ${docId} missing from mappings after C++ update`);
      }
    }
  }

  return result;
}

// ── Main ──

function main(): void {
  const storePath = process.argv[2];
  if (!storePath) {
    console.error('Usage: tsx db_e2e_ts_verify.ts <storePath>');
    process.exit(1);
  }

  if (!fs.existsSync(storePath)) {
    console.error(`[TS Verify] Store path does not exist: ${storePath}`);
    process.exit(1);
  }

  const result = verify(storePath);

  console.log('');
  console.log(`[TS Verify] Results: ${result.passed} passed, ${result.failed} failed`);

  if (result.errors.length > 0) {
    console.error('[TS Verify] Errors:');
    for (const err of result.errors) {
      console.error(`  ✗ ${err}`);
    }
    process.exit(1);
  }

  console.log('[TS Verify] All verifications passed.');
}

main();
