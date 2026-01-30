cmake_minimum_required(VERSION 3.20)
project(brightchain-cpp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BRIGHTCHAIN_BUILD_TESTS "Build tests" ON)
option(BRIGHTCHAIN_BUILD_EXAMPLES "Build examples" ON)
option(BRIGHTCHAIN_BUILD_SERVER "Build HTTP server" ON)

# Find dependencies
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

# Library target
add_subdirectory(src)

# Tests
if(BRIGHTCHAIN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(BRIGHTCHAIN_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Server
if(BRIGHTCHAIN_BUILD_SERVER)
    add_subdirectory(server)
endif()
