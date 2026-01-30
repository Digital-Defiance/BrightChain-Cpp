import { Member } from '../ecies-lib/src/member.js';
import { ECIESService } from '../ecies-lib/src/services/ecies/service.js';
import { deriveVotingKeysFromECDH } from '../ecies-lib/src/services/voting.service.js';
import { SecureString } from '../ecies-lib/src/secure-string.js';
import { MemberType } from '../ecies-lib/src/enumerations/member-type.js';
import { EmailString } from '../ecies-lib/src/email-string.js';
import * as fs from 'fs';

async function generatePaillierVectors() {
    const eciesService = new ECIESService();
    
    // Test vector 1: Known mnemonic
    const mnemonic = new SecureString('abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about');
    
    const member = Member.fromMnemonic(
        mnemonic,
        eciesService,
        undefined,
        'TestUser',
        new EmailString('test@example.com')
    );
    
    // Derive voting keys
    await member.deriveVotingKeys();
    
    const vectors = {
        mnemonic: mnemonic.value,
        ecdhPrivateKey: Buffer.from(member.privateKey!.value).toString('hex'),
        ecdhPublicKey: Buffer.from(member.publicKey).toString('hex'),
        votingPublicKey: {
            n: member.votingPublicKey!.n.toString(16),
            g: member.votingPublicKey!.g.toString(16)
        },
        votingPrivateKey: {
            lambda: member.votingPrivateKey!.lambda.toString(16),
            mu: member.votingPrivateKey!.mu.toString(16)
        },
        testVotes: [] as any[]
    };
    
    // Generate test votes
    for (let i = 0; i < 5; i++) {
        const plaintext = BigInt(i);
        const ciphertext = member.votingPublicKey!.encrypt(plaintext);
        vectors.testVotes.push({
            plaintext: i,
            ciphertext: ciphertext.toString(16)
        });
    }
    
    // Test homomorphic addition
    const ct1 = member.votingPublicKey!.encrypt(1n);
    const ct2 = member.votingPublicKey!.encrypt(2n);
    const ct3 = member.votingPublicKey!.encrypt(3n);
    const sum = member.votingPublicKey!.addition(ct1, ct2, ct3);
    
    (vectors as any).homomorphicSum = {
        ciphertexts: [ct1.toString(16), ct2.toString(16), ct3.toString(16)],
        sum: sum.toString(16),
        expectedPlaintext: 6
    };
    
    // Write to file
    fs.writeFileSync(
        'test_vectors_paillier.json',
        JSON.stringify(vectors, null, 2)
    );
    
    console.log('Generated Paillier test vectors');
    console.log('Voting Public Key n:', vectors.votingPublicKey.n.substring(0, 64) + '...');
}

generatePaillierVectors().catch(console.error);
