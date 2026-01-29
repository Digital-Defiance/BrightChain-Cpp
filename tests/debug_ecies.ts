import { 
  Member, 
  ECIESService,
  EciesEncryptionTypeEnum,
  EmailString,
  MemberType
} from '@digitaldefiance/ecies-lib';

async function test() {
  const eciesService = new ECIESService();

  const { member, mnemonic } = Member.newMember(
    eciesService,
    MemberType.User,
    'Test User Basic',
    new EmailString('test-basic@example.com')
  );

  const plaintext = Buffer.from([0xde, 0xad, 0xbe, 0xef]);
  
  console.log('Encrypting with Basic (33)');
  const encryptedBasic = await eciesService.encrypt(EciesEncryptionTypeEnum.Basic, member.publicKey, plaintext);
  console.log('Basic encrypted length:', encryptedBasic.length);
  console.log('Basic first 10 bytes:', Array.from(encryptedBasic.slice(0, 10)));
  console.log('Basic type byte (index 2):', encryptedBasic[2]);
  
  console.log('\nEncrypting with WithLength (66)');
  const encryptedWithLength = await eciesService.encrypt(EciesEncryptionTypeEnum.WithLength, member.publicKey, plaintext);
  console.log('WithLength encrypted length:', encryptedWithLength.length);
  console.log('WithLength first 10 bytes:', Array.from(encryptedWithLength.slice(0, 10)));
  console.log('WithLength type byte (index 2):', encryptedWithLength[2]);
  
  console.log('\nExpected byte 0 = version (2)');
  console.log('Expected byte 1 = cipher suite');
  console.log('Expected byte 2 = encryption type');
  
  mnemonic.dispose();
  member.dispose();
}

test().catch(console.error);
