// Standalone Paillier test vector generator
// Run with: node --loader ts-node/esm generate_paillier_vectors_simple.ts

import { secp256k1 } from '@noble/curves/secp256k1';
import { generateRandomKeysSync, PublicKey, PrivateKey } from 'paillier-bigint';
import * as fs from 'fs';
import * as crypto from 'crypto';

// HKDF implementation
async function hkdf(secret: Uint8Array, info: string, length: number): Promise<Uint8Array> {
    return new Promise((resolve, reject) => {
        crypto.hkdf('sha512', secret, Buffer.alloc(0), Buffer.from(info), length, (err, derivedKey) => {
            if (err) reject(err);
            else resolve(new Uint8Array(derivedKey));
        });
    });
}

async function main() {
    // Known test private key (from mnemonic "abandon abandon...")
    const privateKeyHex = '0000000000000000000000000000000000000000000000000000000000000001';
    const privateKey = Buffer.from(privateKeyHex, 'hex');
    
    // Get public key
    const publicKey = secp256k1.getPublicKey(privateKey, true);
    
    // Compute ECDH shared secret (with self for deterministic test)
    const sharedSecret = secp256k1.getSharedSecret(privateKey, publicKey, false);
    
    // Derive seed using HKDF
    const seed = await hkdf(sharedSecret, 'PaillierPrimeGen', 64);
    
    // Generate Paillier keys (using simple variant for testing)
    const { publicKey: paillierPub, privateKey: paillierPriv } = generateRandomKeysSync(2048, true);
    
    const vectors = {
        description: 'Paillier cross-platform test vectors',
        ecdhPrivateKey: privateKeyHex,
        ecdhPublicKey: Buffer.from(publicKey).toString('hex'),
        sharedSecret: Buffer.from(sharedSecret).toString('hex'),
        hkdfSeed: Buffer.from(seed).toString('hex'),
        votingPublicKey: {
            n: paillierPub.n.toString(16),
            g: paillierPub.g.toString(16)
        },
        votingPrivateKey: {
            lambda: paillierPriv.lambda.toString(16),
            mu: paillierPriv.mu.toString(16)
        },
        testVotes: [] as any[]
    };
    
    // Generate test votes
    for (let i = 0; i < 5; i++) {
        const plaintext = BigInt(i);
        const ciphertext = paillierPub.encrypt(plaintext);
        const decrypted = paillierPriv.decrypt(ciphertext);
        vectors.testVotes.push({
            plaintext: i,
            ciphertext: ciphertext.toString(16),
            decrypted: Number(decrypted)
        });
    }
    
    // Test homomorphic addition
    const ct1 = paillierPub.encrypt(1n);
    const ct2 = paillierPub.encrypt(2n);
    const ct3 = paillierPub.encrypt(3n);
    const sum = paillierPub.addition(ct1, ct2, ct3);
    const sumDecrypted = paillierPriv.decrypt(sum);
    
    (vectors as any).homomorphicSum = {
        ciphertexts: [ct1.toString(16), ct2.toString(16), ct3.toString(16)],
        sum: sum.toString(16),
        expectedPlaintext: 6,
        decrypted: Number(sumDecrypted)
    };
    
    // Write to file
    fs.writeFileSync(
        'test_vectors_paillier.json',
        JSON.stringify(vectors, null, 2)
    );
    
    console.log('Generated Paillier test vectors');
    console.log('ECDH Public Key:', vectors.ecdhPublicKey);
    console.log('Shared Secret:', vectors.sharedSecret.substring(0, 32) + '...');
    console.log('HKDF Seed:', vectors.hkdfSeed.substring(0, 32) + '...');
    console.log('Paillier n:', vectors.votingPublicKey.n.substring(0, 64) + '...');
    console.log('Test votes:', vectors.testVotes.length);
    console.log('Homomorphic sum test:', vectors.homomorphicSum.decrypted === 6 ? 'PASS' : 'FAIL');
}

main().catch(console.error);
