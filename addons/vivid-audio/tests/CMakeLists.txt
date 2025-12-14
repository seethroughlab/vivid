# vivid-audio Tests
# Unit tests for audio operators

add_executable(test_audio_addon
    ${CMAKE_CURRENT_LIST_DIR}/test_clock.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_sequencer.cpp
)

target_link_libraries(test_audio_addon PRIVATE
    Catch2::Catch2WithMain
    vivid-core
    vivid-audio
)

target_include_directories(test_audio_addon PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/addons/vivid-audio/include
)

catch_discover_tests(test_audio_addon)
