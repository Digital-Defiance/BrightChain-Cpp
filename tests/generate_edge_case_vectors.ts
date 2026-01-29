#!/usr/bin/env node
import * as secrets from '@digitaldefiance/secrets';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

interface ShamirVector {
  bits: number;
  secret: string;
  shares: string[];
  threshold: number;
  description: string;
}

interface EciesVector {
  mode: string;
  plaintext: number[];
  description: string;
}

function generateShamirVectors(): ShamirVector[] {
  const vectors: ShamirVector[] = [];

  // Test all bit lengths 3-20
  const bitLengths = [3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20];

  for (const bits of bitLengths) {
    secrets.init(bits);

    // Secret type 1: Random hex
    const randomSecret = Math.random().toString(16).slice(2).padEnd(16, '0').slice(0, 16);
    const randomShares = secrets.share(randomSecret, 5, 3);
    vectors.push({
      bits,
      secret: randomSecret,
      shares: randomShares,
      threshold: 3,
      description: `Random ${bits}-bit secret`,
    });

    // Secret type 2: All zeros
    const zeroSecret = '00'.repeat(8);
    const zeroShares = secrets.share(zeroSecret, 5, 3);
    vectors.push({
      bits,
      secret: zeroSecret,
      shares: zeroShares,
      threshold: 3,
      description: `All zeros ${bits}-bit`,
    });

    // Secret type 3: All ones (F's in hex)
    const maxBits = bits;
    const maxValue = (1 << maxBits) - 1;
    const onesSecret = maxValue.toString(16).padStart(16, 'f').slice(-16);
    const onesShares = secrets.share(onesSecret, 5, 3);
    vectors.push({
      bits,
      secret: onesSecret,
      shares: onesShares,
      threshold: 3,
      description: `All ones ${bits}-bit`,
    });

    // Secret type 4: Sequential pattern
    const seqSecret = '0123456789abcdef';
    const seqShares = secrets.share(seqSecret, 5, 3);
    vectors.push({
      bits,
      secret: seqSecret,
      shares: seqShares,
      threshold: 3,
      description: `Sequential ${bits}-bit`,
    });
  }

  return vectors;
}

function generateEciesVectors(): EciesVector[] {
  const vectors: EciesVector[] = [];

  // Various plaintext sizes to test
  const sizes = [
    { size: 0, desc: 'empty' },
    { size: 1, desc: '1-byte' },
    { size: 2, desc: '2-byte' },
    { size: 4, desc: '4-byte' },
    { size: 8, desc: '8-byte' },
    { size: 16, desc: '16-byte' },
    { size: 32, desc: '32-byte' },
    { size: 64, desc: '64-byte' },
    { size: 128, desc: '128-byte' },
    { size: 256, desc: '256-byte' },
    { size: 512, desc: '512-byte' },
    { size: 1024, desc: '1KB' },
  ];

  for (const mode of ['basic', 'withLength']) {
    for (const { size, desc } of sizes) {
      const plaintext = size === 0 
        ? [] 
        : Array.from({ length: size }, (_, i) => (i * 37) % 256);
      
      vectors.push({
        mode,
        plaintext,
        description: `${mode} mode ${desc} plaintext`,
      });
    }
  }

  return vectors;
}

async function main() {
  const shamirVectors = generateShamirVectors();
  const eciesVectors = generateEciesVectors();

  const output = {
    shamir: shamirVectors,
    ecies: eciesVectors,
  };

  const outputPath = path.join(__dirname, 'test_vectors_edge_cases.json');
  fs.writeFileSync(outputPath, JSON.stringify(output, null, 2));
  console.log(`✓ Generated ${shamirVectors.length} Shamir edge case vectors (bits 3-20)`);
  console.log(`✓ Generated ${eciesVectors.length} ECIES edge case vectors`);
  console.log(`✓ Saved to ${outputPath}`);
}

main().catch(console.error);
