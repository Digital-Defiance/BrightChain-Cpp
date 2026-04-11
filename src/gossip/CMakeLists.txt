find_package(Boost REQUIRED)

# Optional dependencies - not needed until later tasks
find_package(unofficial-miniupnpc CONFIG QUIET)
if(NOT unofficial-miniupnpc_FOUND)
    find_package(miniupnpc QUIET)
endif()

# jwt-cpp is header-only; find its include path if available
find_path(JWT_CPP_INCLUDE_DIRS "jwt-cpp/jwt.h")

add_library(brightchain_gossip
    block_announcement.cpp
    bloom_filter.cpp
    discovery_protocol.cpp
    gossip_config.cpp
    gossip_engine.cpp
    peer_manager.cpp
    placeholder.cpp
    retry_service.cpp
    websocket_server.cpp
    introspection_api.cpp
    upnp_manager.cpp
)

target_include_directories(brightchain_gossip
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(JWT_CPP_INCLUDE_DIRS)
    target_include_directories(brightchain_gossip PRIVATE ${JWT_CPP_INCLUDE_DIRS})
endif()

target_link_libraries(brightchain_gossip
    PUBLIC
        brightchain
        Boost::boost
)

# Link miniupnpc if available
if(unofficial-miniupnpc_FOUND)
    target_link_libraries(brightchain_gossip PRIVATE unofficial::miniupnpc::miniupnpc)
elseif(miniupnpc_FOUND)
    target_link_libraries(brightchain_gossip PRIVATE miniupnpc::miniupnpc)
endif()

target_compile_features(brightchain_gossip PUBLIC cxx_std_20)
