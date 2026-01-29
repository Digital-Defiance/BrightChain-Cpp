add_executable(block_storage_example
    block_storage_example.cpp
)

target_link_libraries(block_storage_example
    PRIVATE
        brightchain
)

add_executable(ecies_example
    ecies_example.cpp
)

target_link_libraries(ecies_example
    PRIVATE
        brightchain
)
