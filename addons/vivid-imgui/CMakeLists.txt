# vivid-imgui addon
# Builds Dear ImGui with WebGPU backend as a static library
#
# This addon is built once during the main Vivid build.
# User projects automatically link against the pre-built library
# when they #include <vivid/imgui/imgui_integration.h>

cmake_minimum_required(VERSION 3.20)

project(vivid-imgui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === Fetch Dear ImGui ===

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG master  # Use master for latest wgpu-native API compatibility
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(imgui)

message(STATUS "[vivid-imgui] ImGui source: ${imgui_SOURCE_DIR}")

# === Build vivid-imgui static library ===

# ImGui core sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
)

add_library(vivid-imgui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_integration.cpp
    ${IMGUI_SOURCES}
)

# Get vivid runtime include directory and wgpu headers
set(VIVID_RUNTIME_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/runtime/include")

# Find wgpu-native headers (in build/_deps/wgpu)
set(WGPU_INCLUDE_DIR "${CMAKE_BINARY_DIR}/_deps/wgpu/include")

target_include_directories(vivid-imgui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    PRIVATE
        ${VIVID_RUNTIME_INCLUDE_DIR}
        ${WGPU_INCLUDE_DIR}
)

# Use wgpu-native backend (not Dawn)
target_compile_definitions(vivid-imgui PRIVATE
    IMGUI_IMPL_WEBGPU_BACKEND_WGPU
)

# Link against glm (available from parent project)
target_link_libraries(vivid-imgui
    PUBLIC
        glm::glm
)

# Platform-specific settings
if(APPLE)
    # imgui_impl_wgpu.cpp needs Objective-C++ on macOS for Cocoa/CAMetalLayer
    set_source_files_properties(
        ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )
endif()

# Suppress warnings in ImGui code
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vivid-imgui PRIVATE
        -Wno-deprecated-declarations
        -Wno-unused-parameter
    )
endif()

# Set output directory to addons/lib
set_target_properties(vivid-imgui PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${VIVID_ADDONS_LIB_DIR}"
)

# === Install addon files ===

# Copy headers to addons/include
add_custom_command(TARGET vivid-imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VIVID_ADDONS_INCLUDE_DIR}"
    COMMENT "[vivid-imgui] Installing addon headers"
)

# Also copy ImGui headers so users can #include <imgui.h>
add_custom_command(TARGET vivid-imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${imgui_SOURCE_DIR}/imgui.h"
        "${imgui_SOURCE_DIR}/imconfig.h"
        "${imgui_SOURCE_DIR}/imgui_internal.h"
        "${imgui_SOURCE_DIR}/imstb_rectpack.h"
        "${imgui_SOURCE_DIR}/imstb_textedit.h"
        "${imgui_SOURCE_DIR}/imstb_truetype.h"
        "${VIVID_ADDONS_INCLUDE_DIR}/"
    COMMENT "[vivid-imgui] Installing ImGui headers"
)

# Copy addon.json to addons/meta
add_custom_command(TARGET vivid-imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/addon.json"
        "${VIVID_ADDONS_META_DIR}/imgui.json"
    COMMENT "[vivid-imgui] Installing addon metadata"
)

message(STATUS "[vivid-imgui] Configured successfully")
