add_library(brightchain
    block_size.cpp
    block_metadata.cpp
    checksum.cpp
    disk_block_store.cpp
    aes_gcm.cpp
    ec_key_pair.cpp
    ecies.cpp
    shamir.cpp
    raw_data_block.cpp
    cbl.cpp
    extended_cbl.cpp
    super_cbl.cpp
    member.cpp
    paillier.cpp
    hmac_drbg.cpp
    # Voting library
    voting_method.cpp
    vote_encoder.cpp
    poll.cpp
    poll_tallier.cpp
    audit_log.cpp
    poll_factory.cpp
    event_logger.cpp
    bulletin_board.cpp
    hierarchical_aggregator.cpp
    # Trezor-crypto BIP39
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/bip39.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/bip32.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/curves.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/secp256k1.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ecdsa.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/bignum.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/base58.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/address.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/blake256.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/blake2b.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/blake2s.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/groestl.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ripemd160.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/sha3.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/hasher.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/rfc6979.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/hmac_drbg.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/nist256p1.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ed25519-donna/ed25519.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ed25519-donna/curve25519-donna-32bit.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ed25519-donna/curve25519-donna-helpers.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ed25519-donna/curve25519-donna-scalarmult-base.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ed25519-donna/ed25519-donna-32bit-tables.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ed25519-donna/ed25519-donna-basepoint-table.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ed25519-donna/ed25519-donna-impl-base.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ed25519-donna/ed25519-keccak.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ed25519-donna/ed25519-sha3.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/ed25519-donna/modm-donna-32bit.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/pbkdf2.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/sha2.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/hmac.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/memzero.c
    ${CMAKE_SOURCE_DIR}/external/trezor-crypto/rand.c
)

target_include_directories(brightchain
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/trezor-crypto
)

target_link_libraries(brightchain
    PUBLIC
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
)

target_compile_features(brightchain PUBLIC cxx_std_20)

# Install rules
install(TARGETS brightchain
    EXPORT brightchain-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/brightchain
    DESTINATION include
)
