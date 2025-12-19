# Vivid Core - Runtime executable
# Includes: window, timing, input, hot-reload, display, chain visualization (ImGui)

include(FetchContent)

# Fetch stb for image loading (vivid-io)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# FreeType for font rendering (vivid-effects-2d)
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-2
    GIT_SHALLOW TRUE
)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
# Enable position-independent code for Linux shared library linking
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
FetchContent_MakeAvailable(freetype)

# Earcut for polygon tessellation (Canvas path fill)
# Header-only library, skip its CMakeLists.txt which includes demo dependencies
FetchContent_Declare(
    earcut
    GIT_REPOSITORY https://github.com/mapbox/earcut.hpp.git
    GIT_TAG v2.2.4
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR nonexistent  # Skip CMakeLists.txt, we only need the header
)
FetchContent_MakeAvailable(earcut)
message(STATUS "[vivid-core] Earcut source: ${earcut_SOURCE_DIR}")

# Fetch ImGui from master (required for wgpu-native v24.x compatibility)
# We manually specify source files, so use SOURCE_SUBDIR to skip its CMakeLists
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR nonexistent  # Prevents CMake from processing imgui's CMakeLists.txt
)
FetchContent_MakeAvailable(imgui)

# Fetch ImNodes for node graph editor
# We manually specify source files, so use SOURCE_SUBDIR to skip its CMakeLists
FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR nonexistent  # Prevents CMake from processing imnodes's CMakeLists.txt
)
FetchContent_MakeAvailable(imnodes)

message(STATUS "[vivid-core] ImGui source: ${imgui_SOURCE_DIR}")
message(STATUS "[vivid-core] ImNodes source: ${imnodes_SOURCE_DIR}")

# ImGui core sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
)

# ImNodes sources
set(IMNODES_SOURCES
    ${imnodes_SOURCE_DIR}/imnodes.cpp
)

# Vivid core library sources (shared by executable and addons)
set(VIVID_CORE_LIB_SOURCES
    src/context.cpp
    src/chain.cpp
    src/window_manager.cpp
    src/operator.cpp
    src/operator_registry.cpp
    src/audio_operator.cpp
    src/audio_output.cpp
    src/audio_graph.cpp
    # vivid-io (image loading)
    src/io/image_loader.cpp
    # vivid-effects-2d (2D texture effects)
    src/effects/gpu_common.cpp
    src/effects/pipeline_builder.cpp
    src/effects/texture_operator.cpp
    src/effects/noise.cpp
    src/effects/solid_color.cpp
    src/effects/composite.cpp
    src/effects/ramp.cpp
    src/effects/feedback.cpp
    src/effects/output.cpp
    src/effects/image.cpp
    src/effects/displace.cpp
    src/effects/blur.cpp
    src/effects/transform.cpp
    src/effects/shape.cpp
    src/effects/lfo.cpp
    src/effects/hsv.cpp
    src/effects/brightness.cpp
    src/effects/mirror.cpp
    src/effects/edge.cpp
    src/effects/pixelate.cpp
    src/effects/tile.cpp
    src/effects/chromatic_aberration.cpp
    src/effects/switch_op.cpp
    src/effects/bloom.cpp
    src/effects/gradient.cpp
    src/effects/dither.cpp
    src/effects/quantize.cpp
    src/effects/scanlines.cpp
    src/effects/crt_effect.cpp
    src/effects/downsample.cpp
    src/effects/barrel_distortion.cpp
    src/effects/vignette.cpp
    src/effects/frame_cache.cpp
    src/effects/time_machine.cpp
    src/effects/particle_renderer.cpp
    src/effects/particles.cpp
    src/effects/point_sprites.cpp
    src/effects/plexus.cpp
    src/effects/font_atlas.cpp
    src/effects/canvas_renderer.cpp
    src/effects/canvas.cpp
    src/effects/operator_registrations.cpp
)

# Vivid runtime sources (executable only)
set(VIVID_CORE_SOURCES
    src/main.cpp
    src/cli.cpp
    src/display.cpp
    src/hot_reload.cpp
    src/addon_registry.cpp
    src/addon_manager.cpp
    src/editor_bridge.cpp
    imgui/imgui_integration.cpp
    imgui/chain_visualizer.cpp
)

# Platform-specific sources
if(APPLE)
    list(APPEND VIVID_CORE_SOURCES src/video_exporter.mm)
elseif(WIN32)
    list(APPEND VIVID_CORE_SOURCES src/video_exporter_win.cpp)
elseif(UNIX)
    list(APPEND VIVID_CORE_SOURCES src/video_exporter_linux.cpp)
endif()

set(VIVID_CORE_HEADERS
    include/vivid/vivid.h
    include/vivid/context.h
    include/vivid/display.h
    include/vivid/hot_reload.h
    include/vivid/operator.h
    include/vivid/chain.h
    include/vivid/addon_registry.h
    include/vivid/video_exporter.h
    include/vivid/audio_buffer.h
    include/vivid/audio_operator.h
    include/vivid/audio_output.h
    include/vivid/editor_bridge.h
    include/vivid/cli.h
    imgui/imgui_integration.h
    imgui/chain_visualizer.h
)

# Core library (shared between executable and addons)
add_library(vivid-core SHARED
    ${VIVID_CORE_LIB_SOURCES}
)

target_include_directories(vivid-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
        ${freetype_SOURCE_DIR}/include
        ${earcut_SOURCE_DIR}/include
)

target_link_libraries(vivid-core PUBLIC
    webgpu
    glm::glm
    glfw
    glfw3webgpu
    freetype
    nlohmann_json::nlohmann_json
)

# IXWebSocket for EditorBridge
target_link_libraries(vivid-core PUBLIC ixwebsocket)

# Platform-specific settings for vivid-core
if(APPLE)
    set_target_properties(vivid-core PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(WIN32)
    # Export all symbols to generate import library (.lib)
    set_target_properties(vivid-core PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )
endif()

# Copy vivid-core library next to vivid executable
add_custom_command(TARGET vivid-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-core>
        ${CMAKE_BINARY_DIR}/bin/
    COMMENT "Copying vivid-core library"
)

# On Windows, copy GLFW DLL to bin directory (when built as shared library)
if(WIN32)
    add_custom_command(TARGET vivid-core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:glfw>
            ${CMAKE_BINARY_DIR}/bin/
        COMMENT "Copying GLFW DLL"
    )
endif()

# Main runtime executable
add_executable(vivid
    ${VIVID_CORE_SOURCES}
    ${VIVID_CORE_HEADERS}
    ${IMGUI_SOURCES}
    ${IMNODES_SOURCES}
)

target_include_directories(vivid
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imnodes_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/addons/vivid-render3d/include
        ${CMAKE_SOURCE_DIR}/addons/vivid-audio/include
        ${stb_SOURCE_DIR}
)

# Use wgpu-native backend for ImGui, pass version from CMake (or CI override)
target_compile_definitions(vivid PRIVATE
    IMGUI_IMPL_WEBGPU_BACKEND_WGPU
    VIVID_VERSION="${VIVID_VERSION}"
)

# Link against core library, GLFW-WebGPU integration, render3d for geometry previews, audio for analyzer type info
target_link_libraries(vivid
    PRIVATE
        vivid-core
        glfw3webgpu
        vivid-render3d
        vivid-audio
        CLI11::CLI11
)

# Platform-specific settings
if(APPLE)
    # imgui_impl_wgpu.cpp needs Objective-C++ on macOS for Cocoa/CAMetalLayer
    # Use LANGUAGE property instead of compile flags for proper ObjC++ handling
    set_source_files_properties(
        ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
        PROPERTIES
            COMPILE_OPTIONS "-x;objective-c++"
            LANGUAGE CXX
    )

    # macOS: Link against required frameworks
    target_link_libraries(vivid PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework VideoToolbox"
        "-framework AVFoundation"
        "-framework CoreMedia"
    )
elseif(WIN32)
    # Export symbols for dynamically loaded chain DLLs
    # ENABLE_EXPORTS generates an import library (vivid.lib) for the executable
    # WINDOWS_EXPORT_ALL_SYMBOLS exports all symbols automatically
    set_target_properties(vivid PROPERTIES
        ENABLE_EXPORTS ON
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
elseif(UNIX)
    # Linux: Link against X11 if needed
    find_package(X11)
    if(X11_FOUND)
        target_link_libraries(vivid PRIVATE ${X11_LIBRARIES})
    endif()
endif()

# Suppress warnings in third-party code
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        ${IMGUI_SOURCES}
        ${IMNODES_SOURCES}
        PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations -Wno-unused-parameter"
    )
endif()

# Copy shaders to build directory
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:vivid>/shaders
    COMMENT "Copying shaders..."
)

# Copy templates to build directory
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/templates
        $<TARGET_FILE_DIR:vivid>/templates
    COMMENT "Copying templates..."
)

# Install target
install(TARGETS vivid RUNTIME DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION share/vivid/shaders)

message(STATUS "[vivid-core] Configured with integrated ImGui/ImNodes")
