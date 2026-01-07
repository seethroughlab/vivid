# vivid-video Tests
# Unit tests for video playback operators

# Example smoke tests
add_executable(test_video_examples
    ${CMAKE_CURRENT_LIST_DIR}/test_examples.cpp
)

target_link_libraries(test_video_examples PRIVATE
    Catch2::Catch2WithMain
)

target_compile_definitions(test_video_examples PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

catch_discover_tests(test_video_examples
    PROPERTIES LABELS "smoke;examples;video"
)

# TODO: Add unit tests for video decoding, frame timing, etc.
