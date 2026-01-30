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

add_executable(block_types_example
    block_types_example.cpp
)

target_link_libraries(block_types_example
    PRIVATE
        brightchain
)

add_executable(generate_test_vectors
    generate_test_vectors.cpp
)

target_link_libraries(generate_test_vectors
    PRIVATE
        brightchain
)

add_executable(member_example
    member_example.cpp
)

target_link_libraries(member_example
    PRIVATE
        brightchain
)

add_executable(voting_examples
    voting_examples.cpp
)

target_link_libraries(voting_examples
    PRIVATE
        brightchain
)
