import * as fs from 'fs';
import { createHash } from 'crypto';

interface SHA3TestVector {
  data: string;
  dataHex: string;
  dataDescription: string;
  hash: string;
}

function sha3_512(data: Buffer | string): string {
  const buffer = typeof data === 'string' ? Buffer.from(data) : data;
  // Node.js crypto doesn't have SHA3-512, we'll use crypto-js or similar
  // For now, using openssl via a different method
  // Actually, let's check if we can use Node's hash
  return createHash('sha3-512').update(buffer).digest('hex');
}

function generateSHA3Vectors(): SHA3TestVector[] {
  const vectors: SHA3TestVector[] = [];

  // Empty data
  vectors.push({
    data: '',
    dataHex: '',
    dataDescription: 'empty',
    hash: sha3_512(''),
  });

  // Single byte
  vectors.push({
    data: 'a',
    dataHex: '61',
    dataDescription: 'single byte "a"',
    hash: sha3_512('a'),
  });

  // Simple string
  const simple = 'Hello, World!';
  vectors.push({
    data: simple,
    dataHex: Buffer.from(simple).toString('hex'),
    dataDescription: 'simple string "Hello, World!"',
    hash: sha3_512(simple),
  });

  // Numeric string
  const numeric = '1234567890';
  vectors.push({
    data: numeric,
    dataHex: Buffer.from(numeric).toString('hex'),
    dataDescription: 'numeric string "1234567890"',
    hash: sha3_512(numeric),
  });

  // 32-byte symmetric key (common ECIES scenario)
  const key32 = Buffer.from('0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef', 'hex');
  vectors.push({
    data: key32.toString('base64'),
    dataHex: key32.toString('hex'),
    dataDescription: '32-byte symmetric key',
    hash: sha3_512(key32),
  });

  // 64-byte data (double key)
  const data64 = Buffer.concat([key32, key32]);
  vectors.push({
    data: data64.toString('base64'),
    dataHex: data64.toString('hex'),
    dataDescription: '64-byte block',
    hash: sha3_512(data64),
  });

  // 128-byte block
  const data128 = Buffer.concat([data64, data64]);
  vectors.push({
    data: data128.toString('base64'),
    dataHex: data128.toString('hex'),
    dataDescription: '128-byte block',
    hash: sha3_512(data128),
  });

  // 512-byte block (message block)
  const data512 = Buffer.alloc(512, 0xaa);
  vectors.push({
    data: data512.toString('base64'),
    dataHex: data512.toString('hex'),
    dataDescription: '512-byte block (0xaa repeated)',
    hash: sha3_512(data512),
  });

  // 1KB block
  const data1k = Buffer.alloc(1024, 0xbb);
  vectors.push({
    data: data1k.toString('base64'),
    dataHex: data1k.toString('hex'),
    dataDescription: '1KB block (0xbb repeated)',
    hash: sha3_512(data1k),
  });

  // 1MB block (large payload)
  const data1m = Buffer.alloc(1024 * 1024, 0xcc);
  vectors.push({
    data: data1m.toString('base64'),
    dataHex: data1m.toString('hex'),
    dataDescription: '1MB block (0xcc repeated)',
    hash: sha3_512(data1m),
  });

  // JSON data
  const jsonData = JSON.stringify({
    id: 'test-123',
    data: 'sample data',
    timestamp: 1234567890,
    nested: {
      key1: 'value1',
      key2: [1, 2, 3],
    },
  });
  vectors.push({
    data: jsonData,
    dataHex: Buffer.from(jsonData).toString('hex'),
    dataDescription: 'JSON object',
    hash: sha3_512(jsonData),
  });

  return vectors;
}

// Generate and save vectors
const vectors = generateSHA3Vectors();
const output = {
  description: 'SHA3-512 test vectors for C++ compatibility validation',
  timestamp: new Date().toISOString(),
  vectors: vectors,
};

fs.writeFileSync(
  './test_vectors_sha3.json',
  JSON.stringify(output, null, 2)
);

console.log(`Generated ${vectors.length} SHA3 test vectors`);
console.log('Saved to test_vectors_sha3.json');
