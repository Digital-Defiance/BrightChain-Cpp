find_package(GTest REQUIRED)

add_executable(brightchain_tests
    block_size_test.cpp
    checksum_test.cpp
    block_metadata_test.cpp
    disk_block_store_test.cpp
    aes_gcm_test.cpp
    ec_key_pair_test.cpp
    ecies_test.cpp
    ecies_multiple_test.cpp
    ecies_cross_compat_test.cpp
    ecies_multirecipient_bidir_test.cpp
    sha3_cross_compat_test.cpp
    shamir_test.cpp
    shamir_cross_compat_test.cpp
    edge_case_cross_compat_test.cpp
    block_types_test.cpp
    cbl_cross_compat_test.cpp
    cbl_bidirectional_test.cpp
    member_test.cpp
    paillier_test.cpp
    paillier_comprehensive_test.cpp
    paillier_cross_platform_test.cpp
    paillier_bigint_test.cpp
    paillier_json_test.cpp
    paillier_random_factor_test.cpp
    paillier_full_cross_platform_test.cpp
    # Voting library tests
    vote_encoder_test.cpp
    poll_test.cpp
    poll_tallier_test.cpp
    audit_log_test.cpp
    poll_factory_test.cpp
    event_logger_test.cpp
    bulletin_board_test.cpp
    hierarchical_aggregator_test.cpp
    member_json_test.cpp
    member_json_cross_platform_test.cpp
    mnemonic_voting_cross_platform_test.cpp
    drbg_test.cpp
)

target_link_libraries(brightchain_tests
    PRIVATE
        brightchain
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(brightchain_tests)
