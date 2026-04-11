find_package(GTest REQUIRED)

# RapidCheck for property-based testing
# Try to find it installed first, otherwise fetch from GitHub
find_package(rapidcheck CONFIG QUIET)
if(NOT rapidcheck_FOUND)
    find_path(RAPIDCHECK_INCLUDE_DIRS "rapidcheck/rapidcheck.h")
    find_library(RAPIDCHECK_LIBRARY rapidcheck)

    if(NOT RAPIDCHECK_INCLUDE_DIRS OR NOT RAPIDCHECK_LIBRARY)
        include(FetchContent)
        FetchContent_Declare(
            rapidcheck
            GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
            GIT_TAG        master
        )
        set(RC_ENABLE_GTEST ON CACHE BOOL "Enable GTest integration for RapidCheck")
        FetchContent_MakeAvailable(rapidcheck)
        set(RAPIDCHECK_FETCHED TRUE)
    endif()
endif()

add_executable(gossip_tests
    placeholder_test.cpp
    block_announcement_test.cpp
    bloom_filter_test.cpp
    gossip_config_test.cpp
    gossip_engine_test.cpp
    gossip_engine_batch_test.cpp
    gossip_engine_priority_test.cpp
    gossip_engine_local_recipient_test.cpp
    gossip_engine_auto_ack_test.cpp
    peer_manager_test.cpp
    retry_service_test.cpp
    discovery_protocol_test.cpp
    discovery_bloom_precheck_test.cpp
    ecies_batch_test.cpp
    ecies_batch_roundtrip_test.cpp
    head_sync_test.cpp
    head_sync_property_test.cpp
    head_update_signature_test.cpp
    pool_lifecycle_test.cpp
    pool_cache_property_test.cpp
    encrypted_pool_metadata_test.cpp
    quorum_gossip_test.cpp
    quorum_high_priority_property_test.cpp
    encrypted_share_preservation_property_test.cpp
    websocket_server_test.cpp
    websocket_event_access_tier_property_test.cpp
    introspection_api_test.cpp
    introspection_admin_access_property_test.cpp
    introspection_jwt_auth_property_test.cpp
    introspection_pool_filtering_property_test.cpp
    upnp_manager_test.cpp
)

target_link_libraries(gossip_tests
    PRIVATE
        brightchain_gossip
        GTest::gtest
        GTest::gtest_main
)

if(rapidcheck_FOUND)
    target_link_libraries(gossip_tests PRIVATE rapidcheck)
elseif(RAPIDCHECK_FETCHED)
    target_link_libraries(gossip_tests PRIVATE rapidcheck rapidcheck_gtest)
else()
    target_include_directories(gossip_tests PRIVATE ${RAPIDCHECK_INCLUDE_DIRS})
    target_link_libraries(gossip_tests PRIVATE ${RAPIDCHECK_LIBRARY})
endif()

include(GoogleTest)
gtest_discover_tests(gossip_tests)
