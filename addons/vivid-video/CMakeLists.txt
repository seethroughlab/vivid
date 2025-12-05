# vivid-video addon CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

project(vivid-video CXX OBJCXX)

# Find FFmpeg for HAP codec support
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET
        libavformat
        libavcodec
        libavutil
        libswscale
    )
endif()

if(FFMPEG_FOUND)
    message(STATUS "[vivid-video] FFmpeg found - HAP codec support enabled")
else()
    message(STATUS "[vivid-video] FFmpeg not found - HAP codec support disabled")
endif()

# Sources (platform-specific)
set(VIDEO_ADDON_HEADERS
    include/vivid/video/video.h
)

if(PLATFORM_MACOS)
    set(VIDEO_ADDON_SOURCES
        src/video_macos.mm
    )
    # Add HAP decoder if FFmpeg available
    if(FFMPEG_FOUND)
        list(APPEND VIDEO_ADDON_SOURCES src/hap_decoder.cpp)
    endif()
endif()

# Create shared library
add_library(vivid-video SHARED
    ${VIDEO_ADDON_SOURCES}
    ${VIDEO_ADDON_HEADERS}
)

# Diligent include directories
set(DILIGENT_DIR "${CMAKE_SOURCE_DIR}/external/DiligentEngine")

# Include directories
target_include_directories(vivid-video
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/runtime/include
        ${CMAKE_SOURCE_DIR}/external/glm
        ${DILIGENT_DIR}/DiligentCore/Primitives/interface
        ${DILIGENT_DIR}/DiligentCore/Common/interface
        ${DILIGENT_DIR}/DiligentCore/Graphics/GraphicsEngine/interface
        ${DILIGENT_DIR}/DiligentCore/Graphics/GraphicsAccessories/interface
        ${DILIGENT_DIR}/DiligentCore/Graphics/GraphicsTools/interface
)

# FFmpeg support for HAP codec
if(FFMPEG_FOUND)
    target_compile_definitions(vivid-video PRIVATE VIVID_HAS_FFMPEG=1)
    target_include_directories(vivid-video PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(vivid-video PRIVATE ${FFMPEG_LINK_LIBRARIES})
endif()

# Platform-specific settings
if(PLATFORM_MACOS)
    target_compile_definitions(vivid-video PRIVATE PLATFORM_MACOS=1)

    # Link macOS frameworks for video
    target_link_libraries(vivid-video PRIVATE
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework Foundation"
    )

    set_target_properties(vivid-video PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
        LINK_FLAGS "-undefined dynamic_lookup"
    )
elseif(PLATFORM_WIN32)
    target_compile_definitions(vivid-video PRIVATE PLATFORM_WIN32=1)
elseif(PLATFORM_LINUX)
    target_compile_definitions(vivid-video PRIVATE PLATFORM_LINUX=1)
endif()

# Set C++ standard
target_compile_features(vivid-video PUBLIC cxx_std_17)

# Set output directory
set_target_properties(vivid-video PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    FOLDER "Vivid/Addons"
)

# Copy headers to output directory
add_custom_command(TARGET vivid-video POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VIVID_ADDONS_INCLUDE_DIR}"
)

# Copy addon.json to meta directory
add_custom_command(TARGET vivid-video POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/addon.json"
        "${VIVID_ADDONS_META_DIR}/video.json"
)

message(STATUS "[vivid-video] Configured")
