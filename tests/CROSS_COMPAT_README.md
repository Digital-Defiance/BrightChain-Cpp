# Cross-Compatibility Testing

This directory contains tests to verify compatibility between C++ and TypeScript implementations of ECIES and Shamir's Secret Sharing.

## Setup

```bash
cd tests
npm install
```

## Generate Test Vectors from TypeScript

```bash
npm run generate:all
```

This creates:
- `test_vectors_ecies.json` - ECIES encrypted data from TypeScript
- `test_vectors_shamir.json` - Shamir shares from TypeScript

## Run C++ Tests

```bash
cd ../build
cmake --build . --target brightchain_tests
./tests/brightchain_tests --gtest_filter="*CrossCompat*"
```

The C++ tests will:
1. Decrypt TypeScript-generated ECIES data
2. Combine TypeScript-generated Shamir shares
3. Generate C++ test vectors for TypeScript to verify

## Verify C++ Generated Vectors

```bash
cd ../tests
npm run verify
```

This verifies:
- TypeScript can decrypt C++ ECIES encrypted data
- TypeScript can combine C++ Shamir shares

## Test Flow

```
TypeScript → JSON → C++ (decrypt/combine)
C++ → JSON → TypeScript (decrypt/combine)
```

Both directions must work for full compatibility.
