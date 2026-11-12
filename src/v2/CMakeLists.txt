# BrightDB storage format v2 (portable local disk profile). Independent of the
# main brightchain library so it can be built and tested on its own:
#   cmake --build . --target brightdb_v2 brightdb_v2_tool
add_library(brightdb_v2
    codec.cpp
    tree.cpp
    index_key.cpp
    index_tree.cpp
    attachments.cpp
    merge.cpp
    disk_store.cpp
    database.cpp
)
target_include_directories(brightdb_v2 PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)
find_package(Threads REQUIRED)
target_link_libraries(brightdb_v2 PUBLIC OpenSSL::Crypto nlohmann_json::nlohmann_json Threads::Threads)
target_compile_features(brightdb_v2 PUBLIC cxx_std_20)

# Command-line worker with the same commands as brightchain-db/test-support/v2-worker.ts,
# used by the cross-runtime handoff tests.
add_executable(brightdb_v2_tool tool.cpp)
target_link_libraries(brightdb_v2_tool PRIVATE brightdb_v2)
