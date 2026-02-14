# CLI Command Tests
# =================
# Tests for vivid build, params, graph, and docs subcommands

add_executable(test_cli_commands
    test_cli_commands.cpp
)

target_link_libraries(test_cli_commands PRIVATE
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
)

target_compile_definitions(test_cli_commands PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Ensure vivid is built before running CLI tests
add_dependencies(test_cli_commands vivid)

catch_discover_tests(test_cli_commands
    PROPERTIES
        LABELS "cli"
        TIMEOUT 60
)
