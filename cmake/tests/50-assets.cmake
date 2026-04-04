# Asset library manifest parsing test
add_executable(test_asset_manifest_parsing
    tests/assets/test_asset_manifest_parsing.cpp
)
target_include_directories(test_asset_manifest_parsing PRIVATE src tests)
target_link_libraries(test_asset_manifest_parsing PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_asset_manifest_parsing
    COMMAND test_asset_manifest_parsing ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_asset_manifest_parsing PROPERTIES LABELS "ASSET")

# Asset library discovery test
add_executable(test_asset_discovery
    tests/assets/test_asset_discovery.cpp
)
target_include_directories(test_asset_discovery PRIVATE src tests)
target_link_libraries(test_asset_discovery PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_asset_discovery
    COMMAND test_asset_discovery ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_asset_discovery PROPERTIES LABELS "ASSET")

# Asset library import test
add_executable(test_asset_import
    tests/assets/test_asset_import.cpp
)
target_include_directories(test_asset_import PRIVATE src tests)
target_link_libraries(test_asset_import PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_asset_import
    COMMAND test_asset_import ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_asset_import PROPERTIES LABELS "ASSET")

# Asset library index round-trip test
add_executable(test_asset_index
    tests/assets/test_asset_index.cpp
)
target_include_directories(test_asset_index PRIVATE src tests)
target_link_libraries(test_asset_index PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_asset_index
    COMMAND test_asset_index ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_asset_index PROPERTIES LABELS "ASSET")

# Asset library metadata extraction test
add_executable(test_asset_metadata
    tests/assets/test_asset_metadata.cpp
)
target_include_directories(test_asset_metadata PRIVATE src tests)
target_link_libraries(test_asset_metadata PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_asset_metadata
    COMMAND test_asset_metadata ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_asset_metadata PROPERTIES LABELS "ASSET")
