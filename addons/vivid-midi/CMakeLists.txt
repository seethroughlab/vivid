# Vivid MIDI Addon
# MIDI input, output, and file playback
# - RtMidi for hardware I/O
# - midifile for Standard MIDI File parsing

include(FetchContent)

# On Linux, static libraries linked into shared libraries need -fPIC
if(UNIX AND NOT APPLE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# RtMidi - Cross-platform MIDI I/O
FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG        6.0.0
    GIT_SHALLOW    TRUE
)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rtmidi)

# midifile - Standard MIDI File parsing
FetchContent_Declare(
    midifile
    GIT_REPOSITORY https://github.com/craigsapp/midifile.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(midifile)

set(MIDI_SOURCES
    src/midi_in.cpp
    src/midi_out.cpp
    src/midi_file_player.cpp
)

add_library(vivid-midi SHARED ${MIDI_SOURCES})

target_include_directories(vivid-midi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/addons/vivid-audio/include
    ${midifile_SOURCE_DIR}/include
)

target_link_libraries(vivid-midi PUBLIC
    vivid-core
    vivid-audio
    rtmidi
    midifile
)

# Platform-specific configuration
if(APPLE)
    set_target_properties(vivid-midi PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(WIN32)
    set_target_properties(vivid-midi PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

# Copy library next to vivid executable
add_custom_command(TARGET vivid-midi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-midi>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-midi library"
)
