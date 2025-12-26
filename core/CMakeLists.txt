# Vivid Core - Runtime library
# Includes: window, timing, input, hot-reload, display, chain visualization (OverlayCanvas)

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
    GIT_TAG VER-2-14-1
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

# Vivid core library sources (shared by executable and addons)
set(VIVID_CORE_LIB_SOURCES
    src/context.cpp
    src/chain.cpp
    src/asset_loader.cpp
    src/window_manager.cpp
    src/operator.cpp
    src/operator_registry.cpp
    src/audio_operator.cpp
    src/audio_output.cpp
    src/audio_graph.cpp
    # Runtime utilities (used by CLI but also useful for library consumers)
    src/display.cpp
    src/hot_reload.cpp
    src/addon_registry.cpp
    src/addon_manager.cpp
    # Chain visualizer (uses OverlayCanvas, no ImGui)
    src/chain_visualizer.cpp
    # Node graph (OverlayCanvas-based node editor)
    src/node_graph.cpp
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
    src/effects/film_grain.cpp
    src/effects/flash.cpp
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
    src/overlay_canvas.cpp
    src/viz_helpers.cpp
)

# Platform-specific library sources
if(APPLE)
    list(APPEND VIVID_CORE_LIB_SOURCES src/video_exporter.mm src/platform_macos.mm)
elseif(WIN32)
    list(APPEND VIVID_CORE_LIB_SOURCES src/video_exporter_win.cpp src/platform_stub.cpp)
elseif(UNIX)
    list(APPEND VIVID_CORE_LIB_SOURCES src/video_exporter_linux.cpp src/platform_stub.cpp)
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
    include/vivid/overlay_canvas.h
    include/vivid/frame_input.h
    include/vivid/chain_visualizer.h
    include/vivid/node_graph.h
    include/vivid/viz_helpers.h
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
        ${CMAKE_CURRENT_SOURCE_DIR}
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

# Note: ixwebsocket moved to cli/ for EditorBridge

# Platform-specific settings for vivid-core
if(APPLE)
    set_target_properties(vivid-core PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
    # Link against required frameworks for video export
    target_link_libraries(vivid-core PUBLIC
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework Metal"
        "-framework VideoToolbox"
        "-framework AVFoundation"
        "-framework CoreMedia"
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

message(STATUS "[vivid-core] Configured")

# Note: The vivid executable is now built in cli/CMakeLists.txt
