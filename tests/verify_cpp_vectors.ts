#!/usr/bin/env node
import { 
  ECIESService,
  EciesEncryptionTypeEnum
} from '@digitaldefiance/ecies-lib';
import secrets from '@digitaldefiance/secrets';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

async function verifyEciesVectors() {
  const vectorsPath = path.join(__dirname, 'test_vectors_cpp_ecies.json');
  if (!fs.existsSync(vectorsPath)) {
    console.log('C++ ECIES vectors not found. Run C++ tests first.');
    return;
  }

  const vectors = JSON.parse(fs.readFileSync(vectorsPath, 'utf-8'));
  let passed = 0;
  let failed = 0;
  
  const eciesService = new ECIESService();

  for (const vector of vectors.ecies) {
    try {
      const privateKey = new Uint8Array(vector.privateKey);
      const encrypted = new Uint8Array(vector.encrypted);
      const expectedPlaintext = new Uint8Array(vector.plaintext);
      
      // Determine encryption type from mode
      const encryptionType = vector.mode === 'basic' 
        ? EciesEncryptionTypeEnum.Basic 
        : EciesEncryptionTypeEnum.WithLength;

      // Parse the encrypted message to get components
      const header = eciesService.parseSingleEncryptedHeader(encryptionType, encrypted);
      
      // Extract ciphertext (after header)
      const ciphertext = encrypted.slice(header.headerSize);
      
      // Build AAD from header components (preamble + version + cipherSuite + type + ephemeralPublicKey)
      // Version is at offset 0, cipherSuite at 1, type at 2, ephemeralPublicKey starts at 3
      const aad = encrypted.slice(0, 3 + header.ephemeralPublicKey.length);

      // Decrypt with components
      const { decrypted } = await eciesService.decryptWithComponents(
        privateKey,
        header.ephemeralPublicKey,
        header.iv,
        header.authTag,
        ciphertext,
        aad
      );

      if (Buffer.compare(Buffer.from(decrypted), Buffer.from(expectedPlaintext)) === 0) {
        console.log(`✓ ECIES ${vector.mode} decryption successful`);
        passed++;
      } else {
        console.log(`✗ ECIES ${vector.mode} decryption mismatch`);
        console.log(`  Expected: ${Buffer.from(expectedPlaintext).toString('hex')}`);
        console.log(`  Got: ${Buffer.from(decrypted).toString('hex')}`);
        failed++;
      }
    } catch (error) {
      console.log(`✗ ECIES ${vector.mode} error: ${error}`);
      failed++;
    }
  }

  console.log(`\nECIES: ${passed} passed, ${failed} failed`);
}

function verifyShamirVectors() {
  const vectorsPath = path.join(__dirname, 'test_vectors_cpp_shamir.json');
  if (!fs.existsSync(vectorsPath)) {
    console.log('C++ Shamir vectors not found. Run C++ tests first.');
    return;
  }

  const vectors = JSON.parse(fs.readFileSync(vectorsPath, 'utf-8'));
  let passed = 0;
  let failed = 0;

  for (const vector of vectors.shamir) {
    try {
      const shares = vector.shares.slice(0, vector.threshold);
      // Don't pass bits - it's encoded in the share string itself
      const recovered = secrets.combine(shares);

      if (recovered === vector.secret) {
        console.log(`✓ Shamir ${vector.bits}-bit combination successful`);
        passed++;
      } else {
        console.log(`✗ Shamir ${vector.bits}-bit combination mismatch`);
        console.log(`  Expected: ${vector.secret}`);
        console.log(`  Got: ${recovered}`);
        failed++;
      }
    } catch (error) {
      console.log(`✗ Shamir ${vector.bits}-bit error: ${error}`);
      failed++;
    }
  }

  console.log(`\nShamir: ${passed} passed, ${failed} failed`);
}

async function main() {
  console.log('Verifying C++ generated test vectors...\n');
  await verifyEciesVectors();
  verifyShamirVectors();
}

main().catch(console.error);
