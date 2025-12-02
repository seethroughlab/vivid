# Runtime CMakeLists.txt - Phase 4: Core Types and Context
# Building on Phase 2 & 3 with operator framework

# Find FFmpeg for HAP codec demuxing
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG libavformat libavcodec libavutil libswscale)
endif()

set(RUNTIME_SOURCES
    src/main.cpp
    src/window.cpp
    src/renderer.cpp
    src/context.cpp
    src/hotload.cpp
    src/file_watcher.cpp
    src/compiler.cpp
    src/graph.cpp
    src/preview_server.cpp
    src/async_readback.cpp
    src/shared_preview.cpp
    src/preview_thread.cpp
    src/image_loader.cpp
    src/video_loader.cpp
    src/audio_player.cpp
    src/audio_capture.cpp
    src/fft.cpp
    src/audioin.cpp
    src/mesh.cpp
    src/animation.cpp
    src/pipeline3d.cpp
    src/pipeline3d_instanced.cpp
    src/pipeline3d_skinned.cpp
    src/pipeline3d_lit.cpp
    src/pipeline3d_vertex_lit.cpp
    src/pipeline3d_decal.cpp
    src/pipeline2d.cpp
    src/cubemap.cpp
    src/font_atlas.cpp
    src/text_renderer.cpp
)

# Add HAP decoder if FFmpeg is available
if(FFMPEG_FOUND)
    list(APPEND RUNTIME_SOURCES src/hap_decoder.cpp)
    message(STATUS "FFmpeg found - HAP codec support enabled")
else()
    message(STATUS "FFmpeg not found - HAP codec support disabled")
endif()

# Platform-specific source files
if(APPLE)
    list(APPEND RUNTIME_SOURCES
        src/platform_surface_macos.mm
        src/video_loader_macos.mm
        src/camera_capture_macos.mm
    )
    # Enable Objective-C++ for .mm files
    set_source_files_properties(
        src/platform_surface_macos.mm
        src/video_loader_macos.mm
        src/camera_capture_macos.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )
elseif(WIN32)
    list(APPEND RUNTIME_SOURCES src/video_loader_windows.cpp)
else()
    list(APPEND RUNTIME_SOURCES src/video_loader_linux.cpp)
endif()

add_executable(vivid ${RUNTIME_SOURCES})

target_include_directories(vivid
    PUBLIC include
    PRIVATE src
    PRIVATE ${stb_SOURCE_DIR}
    PRIVATE ${miniaudio_SOURCE_DIR}
)

# Add FFmpeg include directories if available
if(FFMPEG_FOUND)
    target_include_directories(vivid PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_compile_definitions(vivid PRIVATE VIVID_HAS_FFMPEG=1)
endif()

target_link_libraries(vivid
    PRIVATE
    glfw
    glm::glm
    wgpu
    ixwebsocket
    nlohmann_json::nlohmann_json
    efsw
    snappy
    ${CMAKE_DL_LIBS}
)

# Link FFmpeg libraries if available
if(FFMPEG_FOUND)
    target_link_libraries(vivid PRIVATE ${FFMPEG_LIBRARIES})
    target_link_directories(vivid PRIVATE ${FFMPEG_LIBRARY_DIRS})
endif()

# Platform-specific libraries and export symbols for dynamic loading
if(APPLE)
    target_link_libraries(vivid PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework VideoToolbox"
        "-framework AudioToolbox"
        "-framework CoreAudio"
    )
    # Export symbols so dynamically loaded operators can find Context, etc.
    target_link_options(vivid PRIVATE -Wl,-export_dynamic)
elseif(UNIX)
    target_link_libraries(vivid PRIVATE pthread dl)
    # Export symbols for dlopen'd libraries
    target_link_options(vivid PRIVATE -rdynamic)
endif()

# Copy include headers to build directory for operator compilation
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

# Copy shaders to build directory
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders
    ${CMAKE_BINARY_DIR}/shaders
)

# Copy fonts to build directory
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/fonts
    ${CMAKE_BINARY_DIR}/fonts
)
