# vivid-storage addon
# Builds JSON-based key/value storage as a static library
#
# This addon is built once during the main Vivid build.
# User projects automatically link against the pre-built library
# when they #include <vivid/storage/storage.h>

cmake_minimum_required(VERSION 3.20)

project(vivid-storage VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === Fetch nlohmann/json ===

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(nlohmann_json)

# === Build vivid-storage static library ===

add_library(vivid-storage STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/storage.cpp
)

# Get vivid runtime include directory
set(VIVID_RUNTIME_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/runtime/include")

target_include_directories(vivid-storage
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${VIVID_RUNTIME_INCLUDE_DIR}
)

target_link_libraries(vivid-storage
    PUBLIC
        nlohmann_json::nlohmann_json
)

# Set output directory to addons/lib
set_target_properties(vivid-storage PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${VIVID_ADDONS_LIB_DIR}"
)

# === Install addon files ===

# Copy headers to addons/include
add_custom_command(TARGET vivid-storage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VIVID_ADDONS_INCLUDE_DIR}"
    COMMENT "[vivid-storage] Installing headers"
)

# Copy addon.json to addons/meta
add_custom_command(TARGET vivid-storage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/addon.json"
        "${VIVID_ADDONS_META_DIR}/storage.json"
    COMMENT "[vivid-storage] Installing addon metadata"
)

message(STATUS "[vivid-storage] Configured successfully")
