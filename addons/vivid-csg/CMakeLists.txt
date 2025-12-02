# vivid-csg addon
# Builds CSG boolean operations as a static library via Manifold
#
# This addon is built once during the main Vivid build.
# User projects automatically link against the pre-built library
# when they #include <vivid/csg/csg.h>

cmake_minimum_required(VERSION 3.20)

project(vivid-csg VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === Fetch Manifold ===

include(FetchContent)

FetchContent_Declare(
    manifold
    GIT_REPOSITORY https://github.com/elalish/manifold.git
    GIT_TAG v3.0.1
)

# Manifold build options - minimize build
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CROSS_SECTION OFF CACHE BOOL "" FORCE)
set(MANIFOLD_EXPORT OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PAR OFF CACHE BOOL "" FORCE)  # Disable TBB for simpler builds
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(manifold)

# === Build vivid-csg static library ===

add_library(vivid-csg STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/csg.cpp
)

# Get vivid runtime include directory
set(VIVID_RUNTIME_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/runtime/include")

target_include_directories(vivid-csg
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${VIVID_RUNTIME_INCLUDE_DIR}
)

target_link_libraries(vivid-csg
    PUBLIC
        manifold
        glm::glm
)

# Set output directory to addons/lib
set_target_properties(vivid-csg PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${VIVID_ADDONS_LIB_DIR}"
)

# === Install addon files ===

# Copy headers to addons/include
add_custom_command(TARGET vivid-csg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VIVID_ADDONS_INCLUDE_DIR}"
    COMMENT "[vivid-csg] Installing headers"
)

# Copy addon.json to addons/meta
add_custom_command(TARGET vivid-csg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/addon.json"
        "${VIVID_ADDONS_META_DIR}/csg.json"
    COMMENT "[vivid-csg] Installing addon metadata"
)

# Copy manifold library to addons/lib
add_custom_command(TARGET vivid-csg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:manifold>"
        "${VIVID_ADDONS_LIB_DIR}/"
    COMMENT "[vivid-csg] Installing manifold library"
)

message(STATUS "[vivid-csg] Configured successfully")
