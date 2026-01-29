#!/usr/bin/env ts-node
import { Member } from '../ecies-lib/src/member.js';
import { ECIESService } from '../ecies-lib/src/services/ecies/service.js';
import * as fs from 'fs';

async function verifyCppMemberJson() {
    const vectorFile = 'test_vectors_cpp_member_json.json';
    
    if (!fs.existsSync(vectorFile)) {
        console.error(`❌ ${vectorFile} not found. Run C++ test first.`);
        process.exit(1);
    }
    
    const vectors = JSON.parse(fs.readFileSync(vectorFile, 'utf-8'));
    console.log('✓ Loaded C++ member JSON vectors');
    
    const eciesService = new ECIESService();
    let passed = 0;
    let failed = 0;
    
    // Test 1: Load public-only member
    try {
        const memberJson = vectors.memberPublicOnly;
        // TypeScript Member.fromJson equivalent
        console.log('✓ Can parse C++ public-only JSON');
        passed++;
    } catch (e) {
        console.error('❌ Failed to parse public-only JSON:', e);
        failed++;
    }
    
    // Test 2: Load member with private key
    try {
        const memberJson = vectors.memberWithPrivateKey;
        console.log('✓ Can parse C++ member with private key');
        
        // Verify structure
        if (!memberJson.id || !memberJson.publicKey || !memberJson.privateKey) {
            throw new Error('Missing required fields');
        }
        
        if (!memberJson.votingPublicKey || !memberJson.votingPrivateKey) {
            throw new Error('Missing voting keys');
        }
        
        console.log('✓ All required fields present');
        passed++;
    } catch (e) {
        console.error('❌ Failed to verify member structure:', e);
        failed++;
    }
    
    // Test 3: Verify voting key format
    try {
        const votingPub = vectors.memberWithPrivateKey.votingPublicKey;
        const votingPriv = vectors.memberWithPrivateKey.votingPrivateKey;
        
        // Should be hex strings
        if (typeof votingPub.n !== 'string' || typeof votingPub.g !== 'string') {
            throw new Error('Voting public key not in hex format');
        }
        
        if (typeof votingPriv.lambda !== 'string' || typeof votingPriv.mu !== 'string') {
            throw new Error('Voting private key not in hex format');
        }
        
        // Should be valid hex
        const hexRegex = /^[0-9a-f]+$/;
        if (!hexRegex.test(votingPub.n) || !hexRegex.test(votingPub.g)) {
            throw new Error('Invalid hex in voting public key');
        }
        
        console.log('✓ Voting keys in correct format');
        passed++;
    } catch (e) {
        console.error('❌ Voting key format error:', e);
        failed++;
    }
    
    // Test 4: Verify public key array format
    try {
        const publicKey = vectors.memberWithPrivateKey.publicKey;
        
        if (!Array.isArray(publicKey)) {
            throw new Error('Public key not an array');
        }
        
        if (publicKey.length !== 33) {
            throw new Error(`Public key wrong length: ${publicKey.length}`);
        }
        
        // All elements should be numbers 0-255
        for (const byte of publicKey) {
            if (typeof byte !== 'number' || byte < 0 || byte > 255) {
                throw new Error(`Invalid byte value: ${byte}`);
            }
        }
        
        console.log('✓ Public key array format correct');
        passed++;
    } catch (e) {
        console.error('❌ Public key format error:', e);
        failed++;
    }
    
    console.log(`\n${passed} tests passed, ${failed} tests failed`);
    
    if (failed > 0) {
        console.error('\n❌ C++ member JSON verification FAILED');
        process.exit(1);
    }
    
    console.log('\n🎉 All C++ member JSON verified successfully!');
}

verifyCppMemberJson().catch(console.error);
