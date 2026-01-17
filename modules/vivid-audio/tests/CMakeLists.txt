# vivid-audio Tests
# Unit tests for audio operators

add_executable(test_audio_addon
    ${CMAKE_CURRENT_LIST_DIR}/test_clock.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_sequencer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_synthesis.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_effects.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_analysis.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_modulators.cpp
)

target_link_libraries(test_audio_addon PRIVATE
    Catch2::Catch2WithMain
    vivid-core
    vivid-audio
)

target_include_directories(test_audio_addon PRIVATE
    ${CMAKE_SOURCE_DIR}/modules/vivid-core/include
    ${CMAKE_SOURCE_DIR}/modules/vivid-audio/include
)

catch_discover_tests(test_audio_addon
    PROPERTIES LABELS "unit"
)

# Example smoke tests
add_executable(test_audio_examples
    ${CMAKE_CURRENT_LIST_DIR}/test_examples.cpp
)

target_link_libraries(test_audio_examples PRIVATE
    Catch2::Catch2WithMain
)

target_compile_definitions(test_audio_examples PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

catch_discover_tests(test_audio_examples
    PROPERTIES LABELS "smoke;examples;audio"
)
