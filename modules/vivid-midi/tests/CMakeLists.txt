# vivid-midi Tests
# Example existence tests for vivid-midi

add_executable(test_midi_examples
    ${CMAKE_CURRENT_LIST_DIR}/test_examples.cpp
)

target_link_libraries(test_midi_examples PRIVATE
    Catch2::Catch2WithMain
)

target_compile_definitions(test_midi_examples PRIVATE
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

catch_discover_tests(test_midi_examples
    PROPERTIES LABELS "smoke;examples;midi"
)
