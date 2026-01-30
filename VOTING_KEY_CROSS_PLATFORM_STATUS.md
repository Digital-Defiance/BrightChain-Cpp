# Cross-Platform Voting Key Derivation Issue

## Status: ⚠️ BLOCKED - Awaiting Package Republish

## Problem

TypeScript and C++ are generating **different Paillier voting keys** from the same mnemonic, despite having matching ECDH keys and HKDF seeds.

## Root Cause

The published `@digitaldefiance/ecies-lib@4.16.0` package contains the **correct code comments** but is still using the **old behavior** (32-byte X-coordinate only for ECDH shared secret).

### Evidence

**ECDH Keys** (✅ MATCH):
```
Private: 1ab42cc412b618bdea3a599e3c9bae199ebf030895b039e9db1e30dafb12b727
Public:  0237b0bb7a8288d38ed49a524b5dc98cff3eb5ca824c9f9dc0dfdb3d9cd600f299
```

**Shared Secret** (✅ MATCH when using debug script):
```
C++:  0493328c19d6bbc9e3c4c9b24f10dbc85e32cb99cd503bad390f96724913e003d4eb9f24c3febc79b284a672cde098b0bc5748dfdfc88abee451fadf07e8908ac9
Node: 0493328c19d6bbc9e3c4c9b24f10dbc85e32cb99cd503bad390f96724913e003d4eb9f24c3febc79b284a672cde098b0bc5748dfdfc88abee451fadf07e8908ac9
```

**HKDF Seed** (✅ MATCH when using debug script):
```
C++:  5588ff94ae274d90d311c1f3498e961390191eeb7392d0de008e17d9965a72cfa88dd492557482a6a237e668ed7f31db91b638b74cc9c541a833bdb3a28d49f7
Node: 5588ff94ae274d90d311c1f3498e961390191eeb7392d0de008e17d9965a72cfa88dd492557482a6a237e668ed7f31db91b638b74cc9c541a833bdb3a28d49f7
```

**Paillier Keys** (❌ MISMATCH):
```
C++ N:  9ab7b3e7f395459537ac86f5d6f36d3b7c796078dc7a261b2e47d5d38549e2a4...
TS N:   711d574111e7e100b6d02e63e03d865d464a8eb2c4178d6e93594c4e90f9d73e...
```

## What Was Fixed

### C++ Code (✅ CORRECT)
- Uses full 65-byte uncompressed ECDH point (0x04 + X + Y coordinates)
- Provides maximum entropy for key derivation
- Cryptographically superior approach

### TypeScript Code (✅ UPDATED in source, ❌ NOT in published package)
- **Source code** in `voting.service.ts` was updated to use full 65-byte point
- **Published package** `@digitaldefiance/ecies-lib@4.16.0` still has old behavior
- Package shows correct comments but wrong runtime behavior

## Solution Required

**Republish the package as version 4.16.1** with the corrected `voting.service.ts`:

```typescript
// Compute shared secret using @noble/secp256k1
// Use uncompressed format (65 bytes with 0x04 prefix) for maximum entropy
const sharedSecret = secp256k1.getSharedSecret(
  ecdhPrivKey,
  publicKeyForECDH,
  false, // false = uncompressed (65 bytes with 0x04 prefix)
);

// Use FULL shared secret (65 bytes) for HKDF - includes both X and Y coordinates
// This provides maximum entropy and is cryptographically superior to using X alone
const seed = await hkdf(
  sharedSecret,  // <-- Use full 65 bytes, not sharedSecret.slice(1)
  null,
  hkdfInfo,
  hkdfLength,
  hmacAlgorithm,
);
```

## Why This Matters

### Security Impact
Using only the X coordinate (32 bytes) instead of the full point (65 bytes):
- **Loses 256 bits of entropy** from the Y coordinate
- **Not standard practice** in cryptographic implementations
- **Weaker security margin** than necessary
- **Ambiguous**: For any X, there are 2 possible Y values

### Correct Approach
Using the full uncompressed point (65 bytes):
- **Maximum entropy** from both X and Y coordinates
- **Standard practice** (OpenSSL, BoringSSL, etc.)
- **Better security margin**
- **Unambiguous** point representation

## Testing After Republish

1. Install new package:
   ```bash
   cd ecies-lib
   npm install @digitaldefiance/ecies-lib@4.16.1
   ```

2. Regenerate TypeScript test vectors:
   ```bash
   npx tsx generate-mnemonic-voting-vectors-simple.ts
   ```

3. Run C++ cross-platform test:
   ```bash
   cd build
   ./tests/brightchain_tests --gtest_filter="MnemonicVotingKeyCrossPlatformTest.*"
   ```

4. Expected result:
   - ✅ All ECDH keys match
   - ✅ All HKDF seeds match
   - ✅ All Paillier keys match
   - ✅ Cross-encryption/decryption works
   - ✅ Homomorphic operations match

## Files Modified

### C++ (✅ Complete)
- `src/paillier.cpp` - Uses full 65-byte ECDH point
- `src/CMakeLists.txt` - Added `hmac_drbg.c` and `nist256p1.c`
- Fixed double-free bug in cleanup code

### TypeScript (✅ Source updated, ⚠️ Package needs republish)
- `ecies-lib/src/services/voting.service.ts` - Updated to use full 65-byte point
- `ecies-lib/generate-mnemonic-voting-vectors-simple.ts` - Updated to use published package

## Current Test Results

**C++ Tests**: 243/247 passing (98.4%)

**Failing tests** (expected until package republish):
- `MnemonicVotingKeyCrossPlatformTest.SameMnemonicProducesSameVotingKeys`
- `MnemonicVotingKeyCrossPlatformTest.VotesAreInteroperable`
- `MnemonicVotingKeyCrossPlatformTest.HomomorphicOperationsMatch`
- `PaillierCrossPlatformTest.VotingKeysMatch`

All failures are due to Paillier key mismatch caused by the package issue.

## Next Steps

1. ✅ C++ code is correct and ready
2. ✅ TypeScript source code is correct
3. ⚠️ **REQUIRED**: Republish `@digitaldefiance/ecies-lib` as version 4.16.1
4. ⏳ After republish: Regenerate test vectors and verify all tests pass
