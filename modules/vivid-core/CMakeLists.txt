# Vivid Core - Runtime library
# Includes: window, timing, input, hot-reload, display, rendering primitives,
# and GUI primitives (OverlayCanvas, NodeGraph, Panel system)
#
# Note: ChainVisualizer is in vivid-devtools module
#
# Production bundles use vivid-production executable (main_production.cpp + Runtime class)
# which does NOT include HotReload or MCP. The dev build (vivid) uses all features.

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

# Vivid core library sources (shared by executable and modules)
set(VIVID_CORE_LIB_SOURCES
    src/context.cpp
    src/chain.cpp
    src/frame_analysis.cpp
    src/audio_analysis.cpp
    src/wav_writer.cpp
    src/waveform_image.cpp
    src/asset_loader.cpp
    src/shader_preprocessor.cpp
    src/window_manager.cpp
    src/operator.cpp
    src/operator_registry.cpp
    src/audio_operator.cpp
    src/audio_output.cpp
    src/audio_graph.cpp
    # Runtime utilities (used by CLI but also useful for library consumers)
    src/display.cpp
    src/runtime.cpp  # Production runtime (no dev tools)
    # Hot-reload is added conditionally below
    src/log.cpp
    src/snapshot.cpp
    src/midi_map.cpp
    src/module_registry.cpp
    src/module_manager.cpp
    # Note: chain_visualizer.cpp is compiled by vivid-devtools module
    # vivid-io (image loading)
    src/io/image_loader.cpp
    # vivid-effects-2d (2D texture effects)
    src/effects/gpu_common.cpp
    src/effects/pipeline_builder.cpp
    src/effects/texture_operator.cpp
    src/effects/noise.cpp
    src/effects/solid_color.cpp
    src/effects/composite.cpp
    src/effects/copy.cpp
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
    src/effects/threshold.cpp
    src/effects/level.cpp
    src/effects/lookup.cpp
    src/effects/crop.cpp
    src/effects/fit.cpp
    src/effects/tone_map.cpp
    src/effects/normal_map.cpp
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
    src/effects/gpu_particles.cpp
    src/effects/particle_system.cpp
    src/effects/fluid_sim.cpp
    src/effects/forces/curl_noise_force.cpp
    src/effects/plexus.cpp
    src/effects/font_atlas.cpp
    src/effects/canvas_renderer.cpp
    src/effects/canvas.cpp
    src/effects/operator_registrations.cpp
    src/effects/trigger.cpp
    # Event operators (input handling)
    src/effects/window_events.cpp
    # GUI primitives (all consolidated in vivid-core)
    src/gui/overlay_canvas.cpp
    src/gui/gui.cpp
    src/gui/node_graph.cpp
    src/gui/scratch_texture.cpp
    src/gui/panel.cpp
    src/gui/panel_manager.cpp
    src/gui/gui_debug.cpp
    src/viz_helpers.cpp
    src/snapshot.cpp
)

# Hot-reload is always included in vivid-core (used by dev build)
# Production bundles use vivid-production which doesn't call HotReload
list(APPEND VIVID_CORE_LIB_SOURCES src/hot_reload.cpp)

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
    include/vivid/runtime.h
    include/vivid/hot_reload.h
    include/vivid/operator.h
    include/vivid/chain.h
    include/vivid/inspect_data.h
    include/vivid/frame_analysis.h
    include/vivid/shader_preprocessor.h
    include/vivid/video_exporter.h
    include/vivid/audio_buffer.h
    include/vivid/audio_operator.h
    include/vivid/audio_output.h
    include/vivid/frame_input.h
    include/vivid/viz_helpers.h
    include/vivid/event.h
    include/vivid/effects/trigger.h
    include/vivid/effects/window_events.h
    # Note: chain_visualizer.h is in vivid-devtools module
    # GUI headers are in include/vivid/gui/
)

# Core library (shared between executable and modules)
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
    magic_enum::magic_enum
)

# Note: ixwebsocket moved to cli/ for RuntimeAPI

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

# Libraries are built to lib/ directly (CMAKE_LIBRARY_OUTPUT_DIRECTORY)
# Executable finds them via RPATH (@executable_path/../lib or $ORIGIN/../lib)

# On Windows, copy GLFW DLL to lib directory (when built as shared library)
if(WIN32)
    add_custom_command(TARGET vivid-core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:glfw>
            ${CMAKE_BINARY_DIR}/lib/
        COMMENT "Copying GLFW DLL"
    )
endif()

# Copy shaders to build root (matches release structure)
file(GLOB CORE_SHADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.wgsl)
file(GLOB CORE_SHADER_LIBS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/lib/*.wgsl)
if(CORE_SHADERS)
    add_custom_command(TARGET vivid-core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders/lib
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CORE_SHADERS} ${CMAKE_BINARY_DIR}/shaders/
        COMMENT "Copying core shaders"
    )
endif()
if(CORE_SHADER_LIBS)
    add_custom_command(TARGET vivid-core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CORE_SHADER_LIBS} ${CMAKE_BINARY_DIR}/shaders/lib/
        COMMENT "Copying core shader libraries"
    )
endif()

message(STATUS "[vivid-core] Configured")

# =============================================================================
# Vivid C API library (for embedding in external applications like Tauri)
# =============================================================================

# vivid-c sources
set(VIVID_C_SOURCES
    src/vivid_c.cpp
)

# Platform-specific vivid-c sources
if(APPLE)
    list(APPEND VIVID_C_SOURCES
        src/vivid_c_window.mm
        src/vivid_c_iosurface.mm
    )
endif()

add_library(vivid-c SHARED
    ${VIVID_C_SOURCES}
)

target_include_directories(vivid-c
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vivid-c
    PRIVATE vivid-core
)

# Note: vivid-devtools is linked in the main CMakeLists.txt after both targets exist

target_compile_definitions(vivid-c PRIVATE VIVID_C_BUILDING_DLL)

# Set library properties
set_target_properties(vivid-c PROPERTIES
    PUBLIC_HEADER "include/vivid/vivid_c.h"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# Platform-specific settings for vivid-c
if(APPLE)
    set_target_properties(vivid-c PROPERTIES
        INSTALL_RPATH "@executable_path;@loader_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
    # Link against required frameworks for IOSurface sharing
    target_link_libraries(vivid-c PRIVATE
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework IOSurface"
    )
elseif(WIN32)
    # Export all symbols to generate import library (.lib)
    set_target_properties(vivid-c PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )
endif()

message(STATUS "[vivid-c] C API library configured")

# Note: The vivid executable is now built in cli/CMakeLists.txt
