# Vivid Video Addon
# Cross-platform video playback
# - HAP codec: GPU-compressed textures (all platforms)
# - Standard codecs: Platform-specific decoders
#   - macOS: AVFoundation
#   - Windows: Media Foundation
#   - Linux: FFmpeg (stub)

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

# Common sources (all platforms)
set(VIDEO_SOURCES
    src/audio_player.cpp
    src/hap.c
    src/video_player.cpp
    src/webcam.cpp
)

# Platform-specific sources
if(APPLE)
    list(APPEND VIDEO_SOURCES
        src/hap_decoder.mm
        src/avf_decoder.mm
        src/avf_webcam.mm
    )
elseif(WIN32)
    list(APPEND VIDEO_SOURCES
        src/hap_decoder.cpp      # Windows version without Obj-C
        src/mf_decoder.cpp
        src/mf_webcam.cpp
    )
else()
    # Linux
    list(APPEND VIDEO_SOURCES
        src/hap_decoder.cpp      # Linux version without Obj-C
        src/ffmpeg_decoder.cpp
    )
endif()

add_library(vivid-video SHARED ${VIDEO_SOURCES})

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

# Platform-specific configuration
if(APPLE)
    # macOS frameworks
    target_link_libraries(vivid-video PRIVATE
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )

    # Enable Objective-C++
    set_source_files_properties(
        src/hap_decoder.mm
        src/avf_decoder.mm
        src/avf_webcam.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )

    # Set install name for macOS so chains can find the library
    set_target_properties(vivid-video PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

elseif(WIN32)
    # Windows Media Foundation libraries
    target_link_libraries(vivid-video PRIVATE
        mf
        mfplat
        mfreadwrite
        mfuuid
        ole32
        oleaut32
        propsys
    )

    # Enable exports for DLL
    set_target_properties(vivid-video PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )

else()
    # Linux - FFmpeg libraries (when implemented)
    # find_package(PkgConfig REQUIRED)
    # pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale)
    # target_include_directories(vivid-video PRIVATE ${FFMPEG_INCLUDE_DIRS})
    # target_link_libraries(vivid-video PRIVATE ${FFMPEG_LIBRARIES})
    message(STATUS "vivid-video: Linux FFmpeg support is a stub - video playback not yet implemented")
endif()

# Copy library next to vivid executable
add_custom_command(TARGET vivid-video POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-video>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-video library"
)
