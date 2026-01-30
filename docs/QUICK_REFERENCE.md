# Quick Reference Guide

## Using the Helper Script (Easiest)

```bash
# Build everything
./brightchain.sh build

# Run all tests
./brightchain.sh test

# Run specific test suite
./brightchain.sh test-suite MemberTest

# Run examples
./brightchain.sh examples
./brightchain.sh example member

# Full verification (rebuild + test + examples)
./brightchain.sh verify

# Quick build & test
./brightchain.sh quick

# See all commands
./brightchain.sh help
```

---

## Build Commands (CMake)

### Initial Setup
```bash
cd brightchain-cpp
cmake -B build -S .
```

### Build Everything
```bash
cmake --build build
```

### Build Specific Targets
```bash
cmake --build build --target brightchain          # Library only
cmake --build build --target brightchain_tests    # Tests
cmake --build build --target member_example       # Member example
cmake --build build --target block_storage_example
cmake --build build --target ecies_example
```

### Clean Build
```bash
rm -rf build
cmake -B build -S .
cmake --build build
```

---

## Testing Commands

### Run All Tests (98 tests)
```bash
./build/tests/brightchain_tests
```

### Run Specific Test Suite
```bash
./build/tests/brightchain_tests --gtest_filter="MemberTest.*"
./build/tests/brightchain_tests --gtest_filter="ECIESTest.*"
./build/tests/brightchain_tests --gtest_filter="SHA3CrossCompatTest.*"
./build/tests/brightchain_tests --gtest_filter="ShamirTest.*"
./build/tests/brightchain_tests --gtest_filter="DiskBlockStoreTest.*"
```

### List All Tests
```bash
./build/tests/brightchain_tests --gtest_list_tests
```

### Verbose Test Output
```bash
./build/tests/brightchain_tests --gtest_verbose
```

---

## Example Programs

### Member Management (BIP39 Mnemonics)
```bash
./build/examples/member_example
```
Demonstrates:
- Generating 12-word mnemonics
- Creating members from mnemonics
- Recreating members (login)
- Signing and verification

### Block Storage
```bash
./build/examples/block_storage_example
```
Demonstrates:
- Storing blocks
- Retrieving blocks
- Block metadata

### ECIES Encryption
```bash
./build/examples/ecies_example
```
Demonstrates:
- Single recipient encryption
- Multi-recipient encryption
- Key management

### Block Types
```bash
./build/examples/block_types_example
```
Demonstrates:
- CBL (Convergent Block List)
- ExtendedCBL
- SuperCBL

---

## Build Options

### Configure with Options
```bash
cmake -B build -S . \
  -DBRIGHTCHAIN_BUILD_TESTS=OFF \
  -DBRIGHTCHAIN_BUILD_EXAMPLES=OFF \
  -DBRIGHTCHAIN_BUILD_SERVER=OFF
```

**Options:**
- `BRIGHTCHAIN_BUILD_TESTS` - Build test suite (default: ON)
- `BRIGHTCHAIN_BUILD_EXAMPLES` - Build examples (default: ON)
- `BRIGHTCHAIN_BUILD_SERVER` - Build HTTP server (default: ON)

---

## Test Suites

| Suite | Tests | Description |
|-------|-------|-------------|
| `BlockSizeTest` | 3 | Block size enum and conversion |
| `ChecksumTest` | 5 | SHA3-512 hashing |
| `DiskBlockStoreTest` | 8 | Block storage operations |
| `AESGCMTest` | 5 | AES-256-GCM encryption |
| `EcKeyPairTest` | 6 | EC key generation and signing |
| `ECIESTest` | 15 | ECIES encryption (single/multi) |
| `ShamirTest` | 8 | Shamir's Secret Sharing |
| `MemberTest` | 14 | Member management and BIP39 |
| `SHA3CrossCompatTest` | 6 | Cross-language compatibility |
| `CBLTest` | 5 | Convergent Block List |
| `ExtendedCBLTest` | 5 | Extended CBL |
| `SuperCBLTest` | 4 | Super CBL |
| **Total** | **98** | **All tests passing** ✅ |

---

## Common Workflows

### Development Cycle
```bash
# 1. Make changes to code
vim src/member.cpp

# 2. Rebuild
cmake --build build

# 3. Run tests
./build/tests/brightchain_tests --gtest_filter="MemberTest.*"

# 4. Run example
./build/examples/member_example
```

### Full Verification
```bash
# Clean build
rm -rf build
cmake -B build -S .
cmake --build build

# Run all tests
./build/tests/brightchain_tests

# Run all examples
./build/examples/member_example
./build/examples/block_storage_example
./build/examples/ecies_example
./build/examples/block_types_example
```

### Quick Test
```bash
# Rebuild and test in one command
cmake --build build && ./build/tests/brightchain_tests
```

---

## Troubleshooting

### Build Fails
```bash
# Clean and rebuild
rm -rf build
cmake -B build -S .
cmake --build build
```

### Tests Fail
```bash
# Run with verbose output
./build/tests/brightchain_tests --gtest_verbose

# Run specific failing test
./build/tests/brightchain_tests --gtest_filter="TestName"
```

### Missing Dependencies
```bash
# macOS (Homebrew)
brew install openssl nlohmann-json googletest

# Ubuntu/Debian
sudo apt-get install libssl-dev nlohmann-json3-dev libgtest-dev
```

---

## File Locations

- **Library**: `build/src/libbrightchain.a`
- **Tests**: `build/tests/brightchain_tests`
- **Examples**: `build/examples/*_example`
- **Headers**: `include/brightchain/*.hpp`
- **Source**: `src/*.cpp`
- **Test Vectors**: `tests/test_vectors_*.json`

---

## Quick Links

- [README.md](../README.md) - Full documentation
- [Member API](MEMBER_API.md) - Member management guide
- [TODO.md](../TODO.md) - Implementation status
- [Complete Fix Summary](COMPLETE_FIX_SUMMARY.md) - Recent changes
