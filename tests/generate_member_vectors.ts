#!/usr/bin/env ts-node
import { Member, ECIESService, MemberType, EmailString, EciesEncryptionTypeEnum } from '@digitaldefiance/ecies-lib';
import { createGuidV4Configuration } from '@digitaldefiance/ecies-lib';
import * as fs from 'fs';

async function generateMemberVectors() {
  // Use GuidV4 provider (16-byte UUIDs)
  const config = createGuidV4Configuration();
  const eciesService = new ECIESService(config.idProvider);
  const vectors: any = { members: [] };

  // Test 1: Member from known mnemonic
  const mnemonic1 = 'abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about';
  const member1 = Member.fromMnemonic(
    { value: mnemonic1, dispose: () => {} } as any,
    eciesService,
    undefined,
    'Test User 1',
    new EmailString('test1@example.com')
  );

  const testData = Buffer.from([0xde, 0xad, 0xbe, 0xef]);
  const signature1 = member1.sign(testData);

  vectors.members.push({
    name: 'member1_from_mnemonic',
    mnemonic: mnemonic1,
    publicKey: Array.from(member1.publicKey),
    privateKey: member1.privateKey ? Array.from(member1.privateKey.value) : null,
    id: Array.from(member1.idBytes),
    testData: Array.from(testData),
    signature: Array.from(signature1),
  });

  // Test 2: Another member for cross-verification
  const mnemonic2 = 'legal winner thank year wave sausage worth useful legal winner thank yellow';
  const member2 = Member.fromMnemonic(
    { value: mnemonic2, dispose: () => {} } as any,
    eciesService,
    undefined,
    'Test User 2',
    new EmailString('test2@example.com')
  );

  const signature2 = member2.sign(testData);

  vectors.members.push({
    name: 'member2_from_mnemonic',
    mnemonic: mnemonic2,
    publicKey: Array.from(member2.publicKey),
    privateKey: member2.privateKey ? Array.from(member2.privateKey.value) : null,
    id: Array.from(member2.idBytes),
    testData: Array.from(testData),
    signature: Array.from(signature2),
  });

  // Test 3: ECIES encryption between members
  const message = Buffer.from('Hello from TypeScript!');
  const encrypted = await eciesService.encrypt(
    EciesEncryptionTypeEnum.WithLength,
    member2.publicKey,
    message
  );

  vectors.ecies_cross_member = {
    sender: 'member1',
    recipient: 'member2',
    recipientPublicKey: Array.from(member2.publicKey),
    plaintext: Array.from(message),
    encrypted: Array.from(encrypted),
  };

  // Cleanup
  member1.dispose();
  member2.dispose();

  fs.writeFileSync('member_test_vectors.json', JSON.stringify(vectors, null, 2));
  console.log('Generated member test vectors with GuidV4 provider');
  console.log(`Member 1 ID: ${Buffer.from(vectors.members[0].id).toString('hex')}`);
  console.log(`Member 2 ID: ${Buffer.from(vectors.members[1].id).toString('hex')}`);
}

generateMemberVectors().catch(console.error);
