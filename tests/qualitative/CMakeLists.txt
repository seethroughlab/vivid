# Qualitative Output Tests
# ========================
# Discovers all projects with vivid-assertions.json,
# runs `vivid check` on each, and verifies assertions pass.

add_executable(test_qualitative
    test_qualitative.cpp
)

target_link_libraries(test_qualitative PRIVATE
    Catch2::Catch2WithMain
)

target_compile_definitions(test_qualitative PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Ensure vivid is built before running qualitative tests
add_dependencies(test_qualitative vivid)

catch_discover_tests(test_qualitative
    PROPERTIES
        LABELS "qualitative"
        TIMEOUT 300
)
