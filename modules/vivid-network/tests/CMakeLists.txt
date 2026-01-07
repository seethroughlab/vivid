# vivid-network Tests
# Unit tests for network operators

add_executable(test_network_addon
    ${CMAKE_CURRENT_LIST_DIR}/test_network.cpp
)

target_link_libraries(test_network_addon PRIVATE
    Catch2::Catch2WithMain
    vivid-core
    vivid-network
)

target_include_directories(test_network_addon PRIVATE
    ${CMAKE_SOURCE_DIR}/src/vivid-core/include
    ${CMAKE_SOURCE_DIR}/modules/vivid-network/include
)

catch_discover_tests(test_network_addon
    PROPERTIES LABELS "unit"
)

# Example smoke tests (hidden - bind to ports)
add_executable(test_network_examples
    ${CMAKE_CURRENT_LIST_DIR}/test_examples.cpp
)

target_link_libraries(test_network_examples PRIVATE
    Catch2::Catch2WithMain
)

target_compile_definitions(test_network_examples PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

catch_discover_tests(test_network_examples
    PROPERTIES LABELS "smoke;examples;network"
)
