# Member API Documentation

## Overview

The Member class provides cryptographic identity management with BIP39 mnemonic support for BrightChain. Members can sign data, verify signatures, and be created from mnemonics for easy backup and recovery.

## Features

- **BIP39 Mnemonic Generation**: Generate 12-word mnemonics for key backup
- **Mnemonic Validation**: Validate BIP39 mnemonics
- **Deterministic Key Derivation**: Recreate same keys from same mnemonic
- **Multiple Member Types**: Admin, System, User, Anonymous
- **ECDSA Signing**: Sign and verify data using secp256k1
- **Public-Only Members**: Create members without private keys for verification

## Creating Members

### Generate New Member with Mnemonic

```cpp
// Generate a 12-word mnemonic
std::string mnemonic = Member::generateMnemonic();

// Create member from mnemonic
auto member = Member::fromMnemonic(
    mnemonic,
    MemberType::User,
    "Alice",
    "alice@example.com"
);
```

### Recreate Member from Existing Mnemonic (Login)

```cpp
// User provides their mnemonic
std::string userMnemonic = "garment opinion monitor gold never catalog pond sunset spell penalty wrist few";

// Validate mnemonic first
if (!Member::validateMnemonic(userMnemonic)) {
    throw std::runtime_error("Invalid mnemonic");
}

// Recreate member - will have same keys and ID
auto member = Member::fromMnemonic(
    userMnemonic,
    MemberType::User,
    "Alice",
    "alice@example.com"
);
```

## Mnemonic Operations

### Generate Mnemonic

```cpp
std::string mnemonic = Member::generateMnemonic();
// Returns: 12-word BIP39 mnemonic (128 bits of entropy)
```

### Validate Mnemonic

```cpp
bool isValid = Member::validateMnemonic(mnemonic);
```

## Usage Examples

### Complete Registration Flow

```cpp
// 1. Generate new member with mnemonic
std::string mnemonic = Member::generateMnemonic();
auto member = Member::fromMnemonic(
    mnemonic,
    MemberType::User,
    "Alice",
    "alice@example.com"
);

// 2. Display mnemonic to user for backup
std::cout << "IMPORTANT: Save this mnemonic phrase:\n";
std::cout << mnemonic << "\n";

// 3. Store member ID and public key
std::string memberId = member.idHex();
```

### Complete Login Flow

```cpp
// 1. User provides mnemonic
std::string userMnemonic = getUserInput();

// 2. Validate mnemonic
if (!Member::validateMnemonic(userMnemonic)) {
    std::cerr << "Invalid mnemonic phrase\n";
    return;
}

// 3. Recreate member
auto member = Member::fromMnemonic(
    userMnemonic,
    MemberType::User,
    "Alice",
    "alice@example.com"
);

std::cout << "Login successful!\n";
```
