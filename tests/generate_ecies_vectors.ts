#!/usr/bin/env node
import { 
  Member, 
  ECIESService,
  EciesEncryptionTypeEnum,
  EmailString,
  MemberType
} from '@digitaldefiance/ecies-lib';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

async function generateEciesTestVectors() {
  const vectors: any = { ecies: [] };
  
  // Create ECIES service
  const eciesService = new ECIESService();

  // Test Basic mode (type 33)
  {
    const { member, mnemonic } = Member.newMember(
      eciesService,
      MemberType.User,
      'Test User Basic',
      new EmailString('test-basic@example.com')
    );
    
    const plaintext = Buffer.from([0xde, 0xad, 0xbe, 0xef]);
    // Note: encrypt signature is (encryptionType, publicKey, message, preamble?)
    const encrypted = await eciesService.encrypt(EciesEncryptionTypeEnum.Basic, member.publicKey, plaintext);

    vectors.ecies.push({
      mode: 'basic',
      privateKey: Array.from(member.privateKey!.value),
      publicKey: Array.from(member.publicKey),
      plaintext: Array.from(plaintext),
      encrypted: Array.from(encrypted),
    });
    
    mnemonic.dispose();
    member.dispose();
  }

  // Test WithLength mode (type 66)
  {
    const { member, mnemonic } = Member.newMember(
      eciesService,
      MemberType.User,
      'Test User WithLength',
      new EmailString('test-withlength@example.com')
    );
    
    const plaintext = Buffer.from([0x01, 0x02, 0x03, 0x04, 0x05]);
    // Note: encrypt signature is (encryptionType, publicKey, message, preamble?)
    const encrypted = await eciesService.encrypt(EciesEncryptionTypeEnum.WithLength, member.publicKey, plaintext);

    vectors.ecies.push({
      mode: 'withLength',
      privateKey: Array.from(member.privateKey!.value),
      publicKey: Array.from(member.publicKey),
      plaintext: Array.from(plaintext),
      encrypted: Array.from(encrypted),
    });
    
    mnemonic.dispose();
    member.dispose();
  }

  const outputPath = path.join(__dirname, 'test_vectors_ecies.json');
  fs.writeFileSync(outputPath, JSON.stringify(vectors, null, 2));
  console.log(`Generated ECIES test vectors at ${outputPath}`);
}

generateEciesTestVectors().catch(console.error);
