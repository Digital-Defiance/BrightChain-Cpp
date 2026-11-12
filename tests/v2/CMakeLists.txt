find_package(GTest REQUIRED)

# Fixtures live in the TypeScript repository, which is the reference implementation.
set(BRIGHTDB_V2_FIXTURES "${CMAKE_SOURCE_DIR}/../BrightChain/brightchain-db/fixtures/storage-v2"
    CACHE PATH "Directory holding vectors.json and stores/")

add_executable(brightdb_v2_tests conformance_test.cpp)
target_link_libraries(brightdb_v2_tests PRIVATE brightdb_v2 GTest::gtest_main)
target_compile_definitions(brightdb_v2_tests PRIVATE BRIGHTDB_V2_FIXTURES="${BRIGHTDB_V2_FIXTURES}")
add_test(NAME brightdb_v2_conformance COMMAND brightdb_v2_tests)
