# vivid-models addon
# Builds 3D model loading as a static library via Assimp
#
# This addon is built once during the main Vivid build.
# User projects automatically link against the pre-built library
# when they #include <vivid/models/model_loader.h>

cmake_minimum_required(VERSION 3.20)

project(vivid-models VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === Fetch Assimp ===

include(FetchContent)

FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.4.3
)

# Assimp build options - minimize build
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(assimp)

# === Build vivid-models static library ===

add_library(vivid-models STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model_loader.cpp
)

# Get vivid runtime include directory
set(VIVID_RUNTIME_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/runtime/include")

target_include_directories(vivid-models
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${VIVID_RUNTIME_INCLUDE_DIR}
)

target_link_libraries(vivid-models
    PUBLIC
        assimp
        glm::glm
)

# Set output directory to addons/lib
set_target_properties(vivid-models PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${VIVID_ADDONS_LIB_DIR}"
)

# === Install addon files ===

# Copy headers to addons/include
add_custom_command(TARGET vivid-models POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VIVID_ADDONS_INCLUDE_DIR}"
    COMMENT "[vivid-models] Installing headers"
)

# Copy addon.json to addons/meta
add_custom_command(TARGET vivid-models POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/addon.json"
        "${VIVID_ADDONS_META_DIR}/models.json"
    COMMENT "[vivid-models] Installing addon metadata"
)

# Copy assimp library to addons/lib
add_custom_command(TARGET vivid-models POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:assimp>"
        "${VIVID_ADDONS_LIB_DIR}/"
    COMMENT "[vivid-models] Installing assimp library"
)

# Note: Assimp uses system zlib on macOS/Linux, no need to copy

message(STATUS "[vivid-models] Configured successfully")
