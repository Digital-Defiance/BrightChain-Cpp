#!/usr/bin/env ts-node
import { Member } from '../ecies-lib/src/member.js';
import { ECIESService } from '../ecies-lib/src/services/ecies/service.js';
import { SecureString } from '../ecies-lib/src/secure-string.js';
import { MemberType } from '../ecies-lib/src/enumerations/member-type.js';
import { EmailString } from '../ecies-lib/src/email-string.js';
import * as fs from 'fs';

async function generateMemberJsonVectors() {
    const eciesService = new ECIESService();
    
    // Test 1: Member from known mnemonic with voting keys
    const mnemonic = new SecureString('abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about');
    const member = Member.fromMnemonic(
        mnemonic,
        eciesService,
        undefined,
        'Test User',
        new EmailString('test@example.com')
    );
    
    await member.deriveVotingKeys();
    
    const vectors: any = {
        source: 'typescript',
        memberPublicOnly: member.toPublicJson(),
        memberWithVotingKeys: member.toJson()
    };
    
    // Test 2: Generated member
    const member2 = Member.newMember(
        eciesService,
        MemberType.System,
        'System User',
        new EmailString('system@example.com')
    ).member;
    
    await member2.deriveVotingKeys();
    vectors.memberGenerated = member2.toJson();
    
    fs.writeFileSync('test_vectors_member_json.json', JSON.stringify(vectors, null, 2));
    console.log('✓ Generated TypeScript member JSON vectors');
    console.log(`  Member ID: ${member.id.toString('hex')}`);
    console.log(`  Has voting keys: ${member.votingPublicKey !== undefined}`);
}

generateMemberJsonVectors().catch(console.error);
