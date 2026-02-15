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

# Export Tests (slower — renders frames and probes output with ffprobe)
add_executable(test_export
    test_export.cpp
)

target_link_libraries(test_export PRIVATE
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
)

target_compile_definitions(test_export PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_dependencies(test_export vivid)

catch_discover_tests(test_export
    PROPERTIES
        LABELS "cli"
        TIMEOUT 120
)

# Audio Evaluation Tests (check with audio assertions, export sidecar)
add_executable(test_audio_evaluation
    test_audio_evaluation.cpp
)

target_link_libraries(test_audio_evaluation PRIVATE
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
)

target_compile_definitions(test_audio_evaluation PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_dependencies(test_audio_evaluation vivid)

catch_discover_tests(test_audio_evaluation
    PROPERTIES
        LABELS "cli"
        TIMEOUT 120
)
