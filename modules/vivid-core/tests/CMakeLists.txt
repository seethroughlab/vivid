# vivid-core Tests
# Example smoke tests for vivid-core

add_executable(test_core_examples
    ${CMAKE_CURRENT_LIST_DIR}/test_examples.cpp
)

target_link_libraries(test_core_examples PRIVATE
    Catch2::Catch2WithMain
)

target_compile_definitions(test_core_examples PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

catch_discover_tests(test_core_examples
    PROPERTIES LABELS "smoke;examples;core"
)
