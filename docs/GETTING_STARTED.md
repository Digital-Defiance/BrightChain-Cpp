# Getting Started with BrightChain C++

## Quick Start (5 minutes)

### 1. Build
```bash
cd brightchain-cpp
cmake -B build -S .
cmake --build build
```

### 2. Run Tests
```bash
./build/tests/brightchain_tests
# Expected: [  PASSED  ] 98 tests.
```

### 3. Try Examples
```bash
# Member management with BIP39 mnemonics
./build/examples/member_example

# Block storage
./build/examples/block_storage_example

# ECIES encryption
./build/examples/ecies_example
```

That's it! You're ready to use BrightChain C++.

---

## What Can I Do?

### 1. Member Management
Create cryptographic identities with BIP39 mnemonic backup:

```cpp
#include <brightchain/member.hpp>

// Generate new member with 12-word mnemonic
auto mnemonic = Member::generateMnemonic();
auto member = Member::fromMnemonic(
    mnemonic, MemberType::User, "Alice", "alice@example.com"
);

// Later, recreate from mnemonic (login)
auto restored = Member::fromMnemonic(
    mnemonic, MemberType::User, "Alice", "alice@example.com"
);
// Same keys and ID!
```

### 2. Block Storage
Store and retrieve encrypted data blocks:

```cpp
#include <brightchain/disk_block_store.hpp>

DiskBlockStore store("/path/to/storage", BlockSize::Medium);

// Store data
std::vector<uint8_t> data = {1, 2, 3, 4, 5};
auto checksum = store.put(data);

// Retrieve data
auto retrieved = store.get(checksum);
```

### 3. ECIES Encryption
Encrypt data for one or multiple recipients:

```cpp
#include <brightchain/ecies.hpp>

// Single recipient
auto encrypted = ECIES::encrypt(data, recipientPublicKey);
auto decrypted = ECIES::decrypt(encrypted, recipientPrivateKey);

// Multiple recipients
auto encrypted = ECIES::encryptMultipleRecipients(
    data, {pubKey1, pubKey2, pubKey3}
);
```

### 4. Shamir's Secret Sharing
Split secrets with threshold recovery:

```cpp
#include <brightchain/shamir.hpp>

// Split secret: need 2 of 3 shares to recover
auto shares = Shamir::split(secret, 3, 2);

// Recover with any 2 shares
auto recovered = Shamir::combine({shares[0], shares[2]});
```

---

## Common Tasks

### Run Specific Tests
```bash
# Member tests (BIP39, signing, verification)
./build/tests/brightchain_tests --gtest_filter="MemberTest.*"

# Encryption tests
./build/tests/brightchain_tests --gtest_filter="ECIESTest.*"

# Storage tests
./build/tests/brightchain_tests --gtest_filter="DiskBlockStoreTest.*"
```

### Build Only What You Need
```bash
# Just the library
cmake --build build --target brightchain

# Just tests
cmake --build build --target brightchain_tests

# Specific example
cmake --build build --target member_example
```

### Clean Rebuild
```bash
rm -rf build
cmake -B build -S .
cmake --build build
```

---

## Project Status

✅ **98 tests passing**
- Block storage and retrieval
- AES-256-GCM encryption
- ECIES (single and multi-recipient)
- Shamir's Secret Sharing
- Member management with BIP39
- SHA3-512 hashing
- Cross-language compatibility

✅ **4 working examples**
- Member management
- Block storage
- ECIES encryption
- Block types (CBL, ExtendedCBL, SuperCBL)

✅ **Full documentation**
- API documentation
- Quick reference guide
- Usage examples

---

## Next Steps

1. **Read the docs**:
   - [Quick Reference](QUICK_REFERENCE.md) - Common commands
   - [Member API](MEMBER_API.md) - Member management guide
   - [README](../README.md) - Full documentation

2. **Explore examples**:
   - `examples/member_example.cpp` - Member management
   - `examples/block_storage_example.cpp` - Block storage
   - `examples/ecies_example.cpp` - Encryption

3. **Run tests**:
   - `./build/tests/brightchain_tests --gtest_list_tests` - See all tests
   - `./build/tests/brightchain_tests` - Run all tests

4. **Start coding**:
   - Include headers from `include/brightchain/`
   - Link against `libbrightchain.a`
   - See examples for usage patterns

---

## Need Help?

- **Build issues**: See [Quick Reference](QUICK_REFERENCE.md) troubleshooting section
- **API questions**: See [Member API](MEMBER_API.md) and example code
- **Test failures**: Run with `--gtest_verbose` for details
- **Recent changes**: See [Complete Fix Summary](COMPLETE_FIX_SUMMARY.md)

---

## Key Features

- ✅ **BIP39 Mnemonics**: 12-word backup phrases for keys
- ✅ **ECIES Encryption**: Elliptic curve encryption (secp256k1)
- ✅ **Multi-Recipient**: Encrypt once, decrypt by multiple recipients
- ✅ **Shamir Sharing**: Threshold-based secret recovery
- ✅ **Block Storage**: Hierarchical disk storage with metadata
- ✅ **Cross-Compatible**: Works with TypeScript implementation
- ✅ **Well Tested**: 98 comprehensive tests

---

## System Requirements

- **CMake**: 3.20+
- **Compiler**: C++20 (GCC 10+, Clang 12+, MSVC 2019+)
- **OpenSSL**: 3.0+ (for cryptography)
- **nlohmann/json**: 3.0+ (for JSON)
- **Google Test**: 1.10+ (for testing)

Most systems have these via package managers:
```bash
# macOS
brew install cmake openssl nlohmann-json googletest

# Ubuntu/Debian
sudo apt-get install cmake libssl-dev nlohmann-json3-dev libgtest-dev
```

---

## Quick Commands Cheat Sheet

```bash
# Build
cmake -B build -S . && cmake --build build

# Test
./build/tests/brightchain_tests

# Run example
./build/examples/member_example

# Specific test suite
./build/tests/brightchain_tests --gtest_filter="MemberTest.*"

# List all tests
./build/tests/brightchain_tests --gtest_list_tests

# Clean rebuild
rm -rf build && cmake -B build -S . && cmake --build build
```

Happy coding! 🚀
