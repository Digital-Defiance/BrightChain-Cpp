# Paillier Cross-Platform Implementation Plan

## Critical Issues Found

1. **ECDH Shared Secret**: TypeScript uses FULL shared secret (65 bytes with 0x04 prefix), not just x-coordinate
2. **HKDF Input**: Must use entire shared secret as input to HKDF
3. **Constants Mismatch**: Need to match exact constants from TypeScript
4. **Deterministic Prime Generation**: Must use HMAC-DRBG exactly as TypeScript does
5. **JSON Serialization**: Need voting key serialization matching TypeScript format

## Required Changes

### 1. Fix paillier.cpp Key Derivation
- Use full ECDH shared secret (65 bytes) as HKDF input
- Match exact HKDF parameters: SHA-512, info="PaillierPrimeGen", length=64
- Implement HMAC-DRBG for deterministic prime generation
- Use Miller-Rabin with exact same witness selection

### 2. Add JSON Serialization
- Serialize voting public key to JSON
- Serialize voting private key to JSON (encrypted)
- Match TypeScript format exactly

### 3. Add Member JSON with Voting Keys
- Include votingPublicKey in Member::toJson()
- Optionally include votingPrivateKey (encrypted)
- Match TypeScript Member JSON format

### 4. Create Cross-Platform Test Vectors
Generate from TypeScript:
- Member with known mnemonic → voting keys
- Encrypt vote with voting public key
- Decrypt vote with voting private key
- Homomorphic addition of encrypted votes
- JSON serialization of member with voting keys

Verify in C++:
- Same mnemonic → same voting keys
- Can decrypt TypeScript-encrypted votes
- Can encrypt votes that TypeScript can decrypt
- Homomorphic operations produce same results
- JSON deserialization matches

### 5. Translate TypeScript Voting Tests
From `ecies-lib/src/lib/voting/*.spec.ts`:
- poll-core.spec.ts
- encoder.spec.ts
- tallier.ts tests
- security.spec.ts
- All voting method tests

## Implementation Order

1. Fix ECDH shared secret handling in paillier.cpp
2. Implement HMAC-DRBG for deterministic primes
3. Add voting key JSON serialization
4. Create TypeScript test vector generator
5. Create C++ cross-platform tests
6. Translate all TypeScript voting tests
7. Verify bidirectional compatibility

## Files to Create/Modify

### New Files
- `tests/generate_paillier_vectors.ts` - Generate test vectors
- `tests/paillier_cross_compat_test.cpp` - Cross-platform tests
- `tests/paillier_voting_methods_test.cpp` - All voting methods

### Modified Files
- `src/paillier.cpp` - Fix key derivation, add DRBG
- `include/brightchain/paillier.hpp` - Add serialization methods
- `src/member.cpp` - Add voting key JSON support
- `include/brightchain/member.hpp` - Add JSON methods

## Test Vector Format

```json
{
  "mnemonic": "abandon abandon abandon...",
  "ecdhPrivateKey": "hex...",
  "ecdhPublicKey": "hex...",
  "sharedSecret": "hex...",
  "hkdfSeed": "hex...",
  "votingPublicKey": {
    "n": "hex...",
    "g": "hex..."
  },
  "votingPrivateKey": {
    "lambda": "hex...",
    "mu": "hex..."
  },
  "testVotes": [
    {
      "plaintext": 1,
      "ciphertext": "hex..."
    }
  ],
  "homomorphicSum": {
    "ciphertexts": ["hex1", "hex2"],
    "result": "hex..."
  }
}
```

## Success Criteria

- [ ] Same mnemonic produces same voting keys in both platforms
- [ ] C++ can decrypt TypeScript-encrypted votes
- [ ] TypeScript can decrypt C++ encrypted votes
- [ ] Homomorphic operations match exactly
- [ ] JSON serialization is bidirectional
- [ ] All TypeScript voting tests pass in C++
- [ ] Performance is acceptable (< 100ms for key derivation)
