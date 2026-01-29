#!/usr/bin/env node
import secrets from '@digitaldefiance/secrets';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

function generateShamirTestVectors() {
  const vectors: any = { shamir: [] };

  // Test with 8 bits
  {
    secrets.init(8);  // Set Galois Field to 8 bits
    const secret = 'deadbeef';
    const shares = secrets.share(secret, 5, 3);  // padLength defaults to 128

    vectors.shamir.push({
      bits: 8,
      secret: secret,
      shares: shares,
      threshold: 3,
    });
  }

  // Test with 10 bits  
  {
    secrets.init(10);  // Set Galois Field to 10 bits
    const secret = 'cafebabe';
    const shares = secrets.share(secret, 10, 5);  // padLength defaults to 128

    vectors.shamir.push({
      bits: 10,
      secret: secret,
      shares: shares,
      threshold: 5,
    });
  }

  const outputPath = path.join(__dirname, 'test_vectors_shamir.json');
  fs.writeFileSync(outputPath, JSON.stringify(vectors, null, 2));
  console.log(`Generated Shamir test vectors at ${outputPath}`);
}

generateShamirTestVectors();
