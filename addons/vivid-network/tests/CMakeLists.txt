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
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/addons/vivid-network/include
)

catch_discover_tests(test_network_addon)
