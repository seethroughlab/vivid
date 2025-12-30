# Smoke Tests
# ============
# Tests that verify examples run without crashing using snapshot mode

# Test helper executable for running smoke tests
add_executable(test_smoke
    test_smoke.cpp
)

target_link_libraries(test_smoke PRIVATE
    Catch2::Catch2WithMain
)

target_compile_definitions(test_smoke PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Ensure vivid is built before running smoke tests
add_dependencies(test_smoke vivid)

catch_discover_tests(test_smoke
    PROPERTIES
        LABELS "smoke"
        TIMEOUT 60
)
