# Batch Build Verification
# ========================
# Discovers all projects with chain.cpp, verifies they compile,
# and generates an operator coverage index.

add_executable(test_batch_build
    test_batch_build.cpp
)

target_link_libraries(test_batch_build PRIVATE
    Catch2::Catch2WithMain
)

target_compile_definitions(test_batch_build PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Ensure vivid is built before running batch build tests
add_dependencies(test_batch_build vivid)

catch_discover_tests(test_batch_build
    PROPERTIES
        LABELS "batch"
        TIMEOUT 300
)
