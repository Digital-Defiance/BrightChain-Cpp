#!/usr/bin/env ts-node
import { Member } from '../ecies-lib/src/member.js';
import { ECIESService } from '../ecies-lib/src/services/ecies/service.js';
import { SecureString } from '../ecies-lib/src/secure-string.js';
import { MemberType } from '../ecies-lib/src/enumerations/member-type.js';
import { EmailString } from '../ecies-lib/src/email-string.js';
import * as fs from 'fs';

async function verifyCppVotingVectors() {
    const vectorFile = 'test_vectors_cpp_voting.json';
    
    if (!fs.existsSync(vectorFile)) {
        console.error(`❌ ${vectorFile} not found. Run C++ test first.`);
        process.exit(1);
    }
    
    const vectors = JSON.parse(fs.readFileSync(vectorFile, 'utf-8'));
    console.log('✓ Loaded C++ voting vectors');
    
    const eciesService = new ECIESService();
    const mnemonic = new SecureString(vectors.mnemonic);
    
    const member = Member.fromMnemonic(
        mnemonic,
        eciesService,
        undefined,
        'TestUser',
        new EmailString('test@example.com')
    );
    
    await member.deriveVotingKeys();
    console.log('✓ Derived voting keys from mnemonic');
    
    // Verify public key matches
    const cppN = BigInt('0x' + vectors.votingPublicKey.n);
    const cppG = BigInt('0x' + vectors.votingPublicKey.g);
    
    if (member.votingPublicKey!.n !== cppN) {
        console.error('❌ Public key N does not match!');
        process.exit(1);
    }
    console.log('✓ Public key N matches');
    
    if (member.votingPublicKey!.g !== cppG) {
        console.error('❌ Public key G does not match!');
        process.exit(1);
    }
    console.log('✓ Public key G matches');
    
    // Verify can decrypt C++ votes
    let passed = 0;
    let failed = 0;
    
    for (const vote of vectors.encryptedVotes) {
        const ciphertext = BigInt('0x' + vote.ciphertext);
        const expectedPlaintext = BigInt(vote.plaintext);
        
        try {
            const decrypted = member.votingPrivateKey!.decrypt(ciphertext);
            
            if (decrypted === expectedPlaintext) {
                passed++;
            } else {
                console.error(`❌ Vote ${vote.plaintext}: expected ${expectedPlaintext}, got ${decrypted}`);
                failed++;
            }
        } catch (e) {
            console.error(`❌ Failed to decrypt vote ${vote.plaintext}:`, e);
            failed++;
        }
    }
    
    console.log(`\n✓ Decrypted ${passed}/${vectors.encryptedVotes.length} C++ votes correctly`);
    
    if (failed > 0) {
        console.error(`❌ ${failed} votes failed verification`);
        process.exit(1);
    }
    
    console.log('\n🎉 All C++ voting vectors verified successfully!');
}

verifyCppVotingVectors().catch(console.error);
