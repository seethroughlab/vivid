# Vivid Video Addon
# Video playback with HAP codec support (GPU-compressed textures)

# macOS-only for now (uses AVFoundation)
if(NOT APPLE)
    message(WARNING "vivid-video addon is currently macOS-only")
    return()
endif()

# Fetch snappy for HAP codec decompression
include(FetchContent)

# Set policy version for older snappy CMakeLists
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

FetchContent_Declare(
    snappy
    GIT_REPOSITORY https://github.com/google/snappy.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
set(SNAPPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SNAPPY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SNAPPY_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(snappy)

add_library(vivid-video SHARED
    src/audio_player.cpp
    src/hap.c
    src/hap_decoder.mm
    src/avf_decoder.mm
    src/video_player.cpp
)

target_include_directories(vivid-video PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/addons/vivid-effects-2d/include
)

# Include snappy headers (generated config is in binary dir)
target_include_directories(vivid-video PRIVATE
    ${snappy_SOURCE_DIR}
    ${snappy_BINARY_DIR}
)

target_link_libraries(vivid-video PUBLIC
    webgpu
    glm
    glfw
    vivid-effects-2d
)

# Link snappy for HAP codec
target_link_libraries(vivid-video PRIVATE snappy)

# macOS frameworks for AVFoundation
if(APPLE)
    target_link_libraries(vivid-video PRIVATE
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )

    # Enable Objective-C++
    set_source_files_properties(src/hap_decoder.mm src/avf_decoder.mm PROPERTIES
        COMPILE_FLAGS "-x objective-c++"
    )

    # Set install name for macOS so chains can find the library
    set_target_properties(vivid-video PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Copy library next to vivid executable
add_custom_command(TARGET vivid-video POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-video>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-video library"
)
