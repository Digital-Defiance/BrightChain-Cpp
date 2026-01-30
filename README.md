# BrightChain C++

C++ implementation of [BrightChain](https://github.com/Digital-Defiance/BrightChain) backend services for block storage and quorum operations.

**[📖 Getting Started Guide](docs/GETTING_STARTED.md)** | **[⚡ Quick Reference](docs/QUICK_REFERENCE.md)** | **[👤 Member API](docs/MEMBER_API.md)**

## Overview

BrightChain is an offshoot of the owner-free-filesystem (OFF) with additional features:
- **Block Storage**: Efficient storage of encrypted data blocks in multiple sizes
- **ECIES Encryption**: Elliptic Curve Integrated Encryption Scheme using secp256k1
- **Quorum System**: Democratic governance with "Brokered Anonymity"
- **Shamir's Secret Sharing**: Threshold-based key recovery

This C++ implementation is compatible with the TypeScript implementation located in the `BrightChain/` directory.

## Features

- **Multiple Block Sizes**: Message (512B), Tiny (1KB), Small (4KB), Medium (1MB), Large (64MB), Huge (256MB)
- **Structured Blocks**: CBL, SuperCBL, ExtendedCBL, MessageCBL
- **Disk Block Store**: Hierarchical directory structure for efficient block storage
- **Cryptography**: AES-256-GCM, SHA3-512, ECIES with secp256k1
- **Member Management**: BIP39 mnemonic support for key backup/recovery
- **Quorum Operations**: Document sealing/unsealing with member consensus

## Building

### Prerequisites

- CMake 3.20+
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- vcpkg (for dependency management)

### Dependencies

- OpenSSL (for cryptography)
- nlohmann/json (for JSON serialization)
- Google Test (for testing)

### Build Instructions

**Using the helper script (recommended):**
```bash
# Build everything
./brightchain.sh build

# Run all tests
./brightchain.sh test

# Run examples
./brightchain.sh examples

# Full verification
./brightchain.sh verify

# See all commands
./brightchain.sh help
```

**Using CMake directly:**
```bash
# Clone the repository
git clone <repository-url>
cd brightchain-cpp

# Configure (dependencies auto-detected via pkg-config/homebrew)
cmake -B build -S .

# Build everything
cmake --build build

# Run all tests
./build/tests/brightchain_tests
```

### Build Options

Configure build with CMake options:

```bash
cmake -B build -S . \
  -DBRIGHTCHAIN_BUILD_TESTS=ON \
  -DBRIGHTCHAIN_BUILD_EXAMPLES=ON \
  -DBRIGHTCHAIN_BUILD_SERVER=ON
```

**Available Options:**
- `BRIGHTCHAIN_BUILD_TESTS` - Build test suite (default: ON)
- `BRIGHTCHAIN_BUILD_EXAMPLES` - Build example programs (default: ON)
- `BRIGHTCHAIN_BUILD_SERVER` - Build HTTP server (default: ON)

### Build Targets

```bash
# Build only the library
cmake --build build --target brightchain

# Build only tests
cmake --build build --target brightchain_tests

# Build specific example
cmake --build build --target member_example
cmake --build build --target block_storage_example
cmake --build build --target ecies_example
```

## Project Structure

```
brightchain-cpp/
├── CMakeLists.txt          # Root build configuration
├── vcpkg.json              # Dependency manifest
├── TODO.md                 # Implementation checklist
├── README.md               # This file
├── include/                # Public headers
│   └── brightchain/
│       ├── block_size.hpp
│       ├── checksum.hpp
│       ├── constants.hpp
│       └── ...
├── src/                    # Implementation files
│   ├── CMakeLists.txt
│   ├── block_size.cpp
│   ├── checksum.cpp
│   └── ...
├── tests/                  # Unit tests
│   ├── CMakeLists.txt
│   └── ...
├── examples/               # Usage examples
│   ├── CMakeLists.txt
│   └── ...
├── server/                 # HTTP server
│   ├── CMakeLists.txt
│   └── main.cpp
└── BrightChain/           # TypeScript reference implementation
```

## Usage

### Member Management with BIP39 Mnemonics

```cpp
#include <brightchain/member.hpp>

// Generate new member with mnemonic
std::string mnemonic = brightchain::Member::generateMnemonic();
auto member = brightchain::Member::fromMnemonic(
    mnemonic,
    brightchain::MemberType::User,
    "Alice",
    "alice@example.com"
);

// Later, recreate member from saved mnemonic (login)
if (brightchain::Member::validateMnemonic(userMnemonic)) {
    auto member = brightchain::Member::fromMnemonic(
        userMnemonic,
        brightchain::MemberType::User,
        "Alice",
        "alice@example.com"
    );
    // Same keys and ID as original
}
```

### Block Storage

```cpp
#include <brightchain/disk_block_store.hpp>
#include <brightchain/block_size.hpp>

// Create a block store
brightchain::DiskBlockStore store("/path/to/storage", brightchain::BlockSize::Medium);

// Store a block
std::vector<uint8_t> data = /* ... */;
auto checksum = store.put(data);

// Retrieve a block
auto retrieved = store.get(checksum);
```

### Quorum Operations

```cpp
#include <brightchain/quorum.hpp>

// Create quorum members
auto member1 = brightchain::Member::generate("Org1");
auto member2 = brightchain::Member::generate("Org2");
auto member3 = brightchain::Member::generate("Org3");

// Create quorum
brightchain::BrightChainQuorum quorum(nodeAgent, "MainQuorum");

// Seal a document (requires 2 of 3 members to unseal)
auto document = quorum.addDocument(
    creator,
    sensitiveData,
    {member1, member2, member3},
    2  // threshold
);

// Later, unseal with member consensus
auto recovered = quorum.getDocument(
    document.id(),
    {member1.id(), member2.id()}
);
```

## Standalone Cryptographic Libraries

BrightChain includes several powerful cryptographic components that can be used independently, without any BrightChain-specific functionality. These are useful for general-purpose encryption, secret sharing, and voting systems.

### ECIES (Elliptic Curve Integrated Encryption Scheme)

Encrypt data for one or more recipients using secp256k1 + AES-256-GCM:

```cpp
#include <brightchain/ecies.hpp>
#include <brightchain/ec_key_pair.hpp>

// Generate recipient key pair
auto recipient = brightchain::EcKeyPair::generate();

// Encrypt for single recipient
std::vector<uint8_t> plaintext = {'H', 'e', 'l', 'l', 'o'};
auto ciphertext = brightchain::Ecies::encryptBasic(plaintext, recipient.publicKey());

// Decrypt
auto decrypted = brightchain::Ecies::decrypt(ciphertext, recipient);

// Encrypt for multiple recipients (each can decrypt independently)
auto recipient2 = brightchain::EcKeyPair::generate();
auto multiCiphertext = brightchain::Ecies::encryptMultiple(
    plaintext, 
    {recipient.publicKey(), recipient2.publicKey()}
);
```

### AES-256-GCM

Authenticated symmetric encryption:

```cpp
#include <brightchain/aes_gcm.hpp>

auto key = brightchain::AesGcm::generateKey();
auto iv = brightchain::AesGcm::generateIV();
brightchain::AesGcm::Tag tag;

std::vector<uint8_t> plaintext = {'S', 'e', 'c', 'r', 'e', 't'};
auto ciphertext = brightchain::AesGcm::encrypt(plaintext, key, iv, tag);
auto decrypted = brightchain::AesGcm::decrypt(ciphertext, key, iv, tag);
```

### Shamir's Secret Sharing

Split secrets into shares requiring a threshold to reconstruct:

```cpp
#include <brightchain/shamir.hpp>

brightchain::ShamirSecretSharing shamir;

// Split a secret into 5 shares, requiring 3 to reconstruct
std::string secret = "48656c6c6f"; // "Hello" in hex
auto shares = shamir.share(secret, 5, 3);

// Reconstruct with any 3 shares
std::vector<std::string> subset = {shares[0], shares[2], shares[4]};
auto recovered = shamir.combine(subset);  // Returns original secret
```

### Paillier Homomorphic Encryption

Perform arithmetic on encrypted values without decrypting:

```cpp
#include <brightchain/paillier.hpp>

// Generate key pair (2048-bit recommended for security)
auto keyPair = brightchain::generatePaillierKeyPair(2048);
auto publicKey = keyPair.publicKey;
auto privateKey = keyPair.privateKey;

// Encrypt values
std::vector<uint8_t> value1 = {0x05};  // 5
std::vector<uint8_t> value2 = {0x03};  // 3
auto cipher1 = publicKey->encrypt(value1);
auto cipher2 = publicKey->encrypt(value2);

// Homomorphic addition: result decrypts to 8
auto sumCipher = publicKey->addition({cipher1, cipher2});
auto sum = privateKey->decrypt(sumCipher);

// Scalar multiplication: result decrypts to 15 (5 * 3)
auto productCipher = publicKey->multiply(cipher1, 3);
auto product = privateKey->decrypt(productCipher);
```

### Voting System

Create cryptographically secure polls with various voting methods:

```cpp
#include <brightchain/poll_factory.hpp>
#include <brightchain/poll_tallier.hpp>
#include <brightchain/vote_encoder.hpp>

// Create a poll authority (organizer)
auto authority = brightchain::Member::generate("Election Authority");

// Generate voting keys
auto votingKeys = brightchain::generatePaillierKeyPair(2048);

// Create a poll using the factory
std::vector<std::string> choices = {"Alice", "Bob", "Charlie"};
auto poll = brightchain::PollFactory::createPoll(
    authority,
    choices,
    brightchain::VotingMethod::Plurality,  // Or Approval, RankedChoice, Borda, etc.
    votingKeys.publicKey
);

// Voters cast encrypted votes
brightchain::VoteEncoder encoder(votingKeys.publicKey);
auto voter1 = brightchain::Member::generate("Voter 1");
auto vote1 = encoder.encodePlurality(0, choices.size());  // Vote for Alice
poll->vote(voter1, vote1);

// Close poll and tally results
poll->close();
brightchain::PollTallier tallier(votingKeys.privateKey);
auto results = tallier.tally(*poll);
// results[0] = votes for Alice, results[1] = votes for Bob, etc.
```

**Supported Voting Methods:**
- `Plurality` - Single choice, most votes wins
- `Approval` - Vote for multiple candidates
- `Weighted` - Votes have different weights
- `Borda` - Ranked voting with points
- `RankedChoice` - Instant-runoff voting (IRV)
- `Score` / `STAR` - Rate candidates on a scale
- `STV` - Single Transferable Vote
- `Supermajority` - Requires >50% threshold
- `Quadratic` - Cost increases quadratically
- `Consensus` / `ConsentBased` - Collaborative decision-making

### Linking Standalone Components

To use only the cryptographic components without BrightChain's block storage:

```cmake
# In your CMakeLists.txt
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

# Link against brightchain library
target_link_libraries(your_app PRIVATE brightchain)
```

Or include only the specific source files you need:
- `aes_gcm.cpp` - AES-256-GCM encryption
- `ec_key_pair.cpp`, `ecies.cpp` - ECIES encryption
- `shamir.cpp` - Shamir's Secret Sharing
- `paillier.cpp` - Paillier homomorphic encryption
- `vote_encoder.cpp`, `poll.cpp`, `poll_tallier.cpp` - Voting system

## Integrating BrightChain as a Dependency

There are several ways to use BrightChain in your own project:

### Option 1: CMake FetchContent (Recommended)

The easiest way to integrate BrightChain into a CMake project:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_project)

include(FetchContent)

FetchContent_Declare(
    brightchain
    GIT_REPOSITORY https://github.com/Digital-Defiance/brightchain-cpp.git
    GIT_TAG        main  # or a specific tag/commit
)

# Don't build tests/examples/server for the dependency
set(BRIGHTCHAIN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BRIGHTCHAIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BRIGHTCHAIN_BUILD_SERVER OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(brightchain)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE brightchain)
```

### Option 2: Git Submodule

Add BrightChain as a submodule in your project:

```bash
git submodule add https://github.com/Digital-Defiance/brightchain-cpp.git external/brightchain
git submodule update --init --recursive
```

Then in your `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_project)

# Disable optional components
set(BRIGHTCHAIN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BRIGHTCHAIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BRIGHTCHAIN_BUILD_SERVER OFF CACHE BOOL "" FORCE)

add_subdirectory(external/brightchain)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE brightchain)
```

### Option 3: System Installation

Build and install BrightChain system-wide:

```bash
# Clone and build
git clone https://github.com/Digital-Defiance/brightchain-cpp.git
cd brightchain-cpp
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
sudo cmake --install build
```

Then in your project:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_project)

# Find installed brightchain
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

# Add brightchain include path and link library
find_library(BRIGHTCHAIN_LIB brightchain REQUIRED)

add_executable(my_app main.cpp)
target_include_directories(my_app PRIVATE /usr/local/include)
target_link_libraries(my_app PRIVATE 
    ${BRIGHTCHAIN_LIB}
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)
```

### Option 4: Copy Source Files Directly

For minimal integration, copy only the files you need:

**For encryption only (ECIES + AES-GCM):**
```
include/brightchain/aes_gcm.hpp
include/brightchain/ec_key_pair.hpp
include/brightchain/ecies.hpp
src/aes_gcm.cpp
src/ec_key_pair.cpp
src/ecies.cpp
external/trezor-crypto/  (for BIP39 support)
```

**For Shamir's Secret Sharing:**
```
include/brightchain/shamir.hpp
src/shamir.cpp
```

**For Paillier homomorphic encryption:**
```
include/brightchain/paillier.hpp
src/paillier.cpp
```

**For the voting system:**
```
include/brightchain/poll.hpp
include/brightchain/poll_types.hpp
include/brightchain/poll_factory.hpp
include/brightchain/poll_tallier.hpp
include/brightchain/vote_encoder.hpp
include/brightchain/voting_method.hpp
include/brightchain/encrypted_vote.hpp
include/brightchain/audit_log.hpp
include/brightchain/audit_types.hpp
src/poll.cpp
src/poll_factory.cpp
src/poll_tallier.cpp
src/vote_encoder.cpp
src/voting_method.cpp
src/audit_log.cpp
```

### Dependencies

BrightChain requires:
- **C++20** compiler (GCC 10+, Clang 12+, MSVC 2019+)
- **OpenSSL** (for cryptography primitives)
- **nlohmann/json** (for JSON serialization)

Install on macOS:
```bash
brew install openssl nlohmann-json
```

Install on Ubuntu/Debian:
```bash
sudo apt install libssl-dev nlohmann-json3-dev
```

Install on Windows (vcpkg):
```bash
vcpkg install openssl nlohmann-json
```

## Compatibility

This implementation is designed to be compatible with the TypeScript implementation:
- Block format is identical
- Encryption schemes are compatible
- Metadata format is JSON-based
- API endpoints match TypeScript server

## Development

### Code Style

This project uses clang-format for code formatting:

```bash
clang-format -i src/**/*.cpp include/**/*.hpp
```

### Testing

Tests use Google Test framework:

```bash
# Run all tests (98 tests)
./build/tests/brightchain_tests

# Run specific test suite
./build/tests/brightchain_tests --gtest_filter="MemberTest.*"
./build/tests/brightchain_tests --gtest_filter="ECIESTest.*"
./build/tests/brightchain_tests --gtest_filter="SHA3CrossCompatTest.*"

# Run with verbose output
./build/tests/brightchain_tests --gtest_verbose

# List all tests
./build/tests/brightchain_tests --gtest_list_tests
```

**Test Suites:**
- `BlockSizeTest` - Block size enumeration and conversion
- `ChecksumTest` - SHA3-512 hashing
- `DiskBlockStoreTest` - Block storage operations
- `AESGCMTest` - AES-256-GCM encryption
- `EcKeyPairTest` - EC key generation and signing
- `ECIESTest` - ECIES encryption (single and multi-recipient)
- `ShamirTest` - Shamir's Secret Sharing
- `MemberTest` - Member management and BIP39 mnemonics
- `SHA3CrossCompatTest` - Cross-language SHA3 compatibility
- `CBLTest`, `ExtendedCBLTest`, `SuperCBLTest` - Block types

### Running Examples

```bash
# Member management with BIP39 mnemonics
./build/examples/member_example

# Block storage operations
./build/examples/block_storage_example

# ECIES encryption demo
./build/examples/ecies_example

# Block types demo
./build/examples/block_types_example
```

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Documentation

- [Getting Started Guide](docs/GETTING_STARTED.md) - 5-minute quick start
- [Quick Reference](docs/QUICK_REFERENCE.md) - Common commands and workflows
- [Member API](docs/MEMBER_API.md) - Member management and BIP39 mnemonics
- [Member Comparison](docs/MEMBER_COMPARISON.md) - C++ vs TypeScript feature comparison
- [Complete Fix Summary](docs/COMPLETE_FIX_SUMMARY.md) - Recent fixes and improvements
- [TODO.md](TODO.md) - Implementation checklist
- [TypeScript Reference](BrightChain/README.md) - Original implementation
- [Quorum System](BrightChain/docs/Quorum.md) - Quorum documentation
- [Architecture](BrightChain/docs/ARCHITECTURE_RULES.md) - System architecture

## License

See [LICENSE](LICENSE) file.

## References

- TypeScript Implementation: `BrightChain/`
- Owner-Free Filesystem: Original OFF specification
- ECIES: Elliptic Curve Integrated Encryption Scheme
- Shamir's Secret Sharing: Threshold cryptography
