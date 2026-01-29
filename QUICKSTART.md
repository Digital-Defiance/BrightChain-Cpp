# BrightChain C++ Quick Start

## Installation

### macOS

```bash
# Install dependencies
brew install cmake ninja openssl vcpkg

# Clone repository
git clone <repository-url>
cd brightchain-cpp

# Install C++ dependencies
vcpkg install

# Build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$(brew --prefix vcpkg)/share/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run example
./build/examples/block_storage_example
```

### Ubuntu/Debian

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install -y cmake ninja-build libssl-dev git

# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh

# Clone repository
git clone <repository-url>
cd brightchain-cpp

# Install C++ dependencies
../vcpkg/vcpkg install

# Build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run example
./build/examples/block_storage_example
```

## Basic Usage

### Storing and Retrieving Blocks

```cpp
#include <brightchain/disk_block_store.hpp>
#include <brightchain/block_size.hpp>

// Create store
brightchain::DiskBlockStore store("./data", brightchain::BlockSize::Medium);

// Store data
std::vector<uint8_t> data = {1, 2, 3, 4, 5};
auto checksum = store.put(data);

// Retrieve data
auto retrieved = store.get(checksum);

// Check existence
if (store.has(checksum)) {
    // Block exists
}
```

### Working with Block Sizes

```cpp
#include <brightchain/block_size.hpp>

// Get block size from length
auto size = brightchain::lengthToBlockSize(1048576); // Medium

// Get closest block size
auto closest = brightchain::lengthToClosestBlockSize(5000); // Small

// Convert to string
std::string name = brightchain::blockSizeToString(size); // "Medium"
```

### Computing Checksums

```cpp
#include <brightchain/checksum.hpp>

// Hash data
std::vector<uint8_t> data = {1, 2, 3, 4, 5};
auto checksum = brightchain::Checksum::fromData(data);

// Get hex representation
std::string hex = checksum.toHex();

// Parse from hex
auto parsed = brightchain::Checksum::fromHex(hex);
```

## Next Steps

- Read the [full documentation](README.md)
- Check the [TODO list](TODO.md) for implementation status
- Review [examples](examples/) for more usage patterns
- See [CONTRIBUTING.md](CONTRIBUTING.md) for development guidelines
