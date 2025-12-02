# vivid-nuklear addon
# Builds Nuklear GUI integration as a static library
#
# This addon is built once during the main Vivid build.
# User projects automatically link against the pre-built library
# when they #include <vivid/nuklear/nuklear_integration.h>
#
# Unlike ImGUI, Nuklear is a single-header ANSI C library with no WebGPU
# API compatibility issues. This addon uses software rendering to a
# framebuffer that gets uploaded to a GPU texture.

cmake_minimum_required(VERSION 3.20)

project(vivid-nuklear VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === Fetch Nuklear ===

include(FetchContent)

FetchContent_Declare(
    nuklear
    GIT_REPOSITORY https://github.com/Immediate-Mode-UI/Nuklear.git
    GIT_TAG master
)

FetchContent_MakeAvailable(nuklear)

# === Build vivid-nuklear static library ===

add_library(vivid-nuklear STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nuklear_integration.cpp
)

# Get vivid runtime include directory
set(VIVID_RUNTIME_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/runtime/include")

target_include_directories(vivid-nuklear
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${nuklear_SOURCE_DIR}
        ${VIVID_RUNTIME_INCLUDE_DIR}
)

target_link_libraries(vivid-nuklear
    PUBLIC
        glm::glm
)

# Set output directory to addons/lib
set_target_properties(vivid-nuklear PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${VIVID_ADDONS_LIB_DIR}"
)

# === Install addon files ===

# Copy headers to addons/include
add_custom_command(TARGET vivid-nuklear POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VIVID_ADDONS_INCLUDE_DIR}"
    COMMENT "[vivid-nuklear] Installing headers"
)

# Copy addon.json to addons/meta
add_custom_command(TARGET vivid-nuklear POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/addon.json"
        "${VIVID_ADDONS_META_DIR}/nuklear.json"
    COMMENT "[vivid-nuklear] Installing addon metadata"
)

# Copy nuklear.h to addons/include for reference
add_custom_command(TARGET vivid-nuklear POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${nuklear_SOURCE_DIR}/nuklear.h"
        "${VIVID_ADDONS_INCLUDE_DIR}/nuklear.h"
    COMMENT "[vivid-nuklear] Installing nuklear.h"
)

message(STATUS "[vivid-nuklear] Configured successfully")
