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
)

target_include_directories(brightchain
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
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
