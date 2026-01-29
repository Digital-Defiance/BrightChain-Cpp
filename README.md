# BrightChain C++

C++ implementation of BrightChain backend services for block storage and quorum operations.

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

```bash
# Clone the repository
git clone <repository-url>
cd brightchain-cpp

# Install dependencies with vcpkg
vcpkg install

# Configure
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build

# Test
ctest --test-dir build
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
# Run all tests
ctest --test-dir build

# Run specific test
./build/tests/block_size_test
```

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Documentation

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
