# Contributing to BrightChain C++

Thank you for your interest in contributing to BrightChain C++!

## Development Setup

### Prerequisites

- CMake 3.20+
- C++20 compatible compiler
- vcpkg for dependency management
- Git

### Building

```bash
# Clone repository
git clone <repository-url>
cd brightchain-cpp

# Install dependencies
vcpkg install

# Configure
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build

# Test
ctest --test-dir build
```

## Code Style

- Use clang-format for formatting (config provided)
- Follow C++20 best practices
- Use meaningful variable and function names
- Add comments for complex logic
- Document public APIs with Doxygen-style comments

## Testing

- Write unit tests for all new functionality
- Ensure all tests pass before submitting PR
- Aim for high code coverage
- Test edge cases and error conditions

## Pull Request Process

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Run tests and ensure they pass
5. Format code with clang-format
6. Commit your changes (`git commit -m 'Add amazing feature'`)
7. Push to your fork (`git push origin feature/amazing-feature`)
8. Open a Pull Request

## Commit Messages

- Use clear, descriptive commit messages
- Start with a verb in present tense (Add, Fix, Update, etc.)
- Reference issues when applicable

## Compatibility

- Maintain compatibility with TypeScript implementation
- Test cross-language interoperability when changing formats
- Document any breaking changes

## Questions?

Open an issue for discussion before starting major changes.
