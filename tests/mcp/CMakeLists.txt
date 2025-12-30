# MCP Server Tests
# =================
# Tests for the Model Context Protocol server

add_executable(test_mcp
    test_mcp_server.cpp
)

target_link_libraries(test_mcp PRIVATE
    Catch2::Catch2WithMain
)

# nlohmann_json is already available in the project
target_link_libraries(test_mcp PRIVATE
    nlohmann_json::nlohmann_json
)

target_compile_definitions(test_mcp PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
)

# Ensure vivid is built before running MCP tests
add_dependencies(test_mcp vivid)

catch_discover_tests(test_mcp
    PROPERTIES
        LABELS "mcp"
        TIMEOUT 30
)
