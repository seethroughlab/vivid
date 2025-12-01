cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0077 NEW)  # Allow setting options before FetchContent
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)  # For older dependencies
project(vivid VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Dependencies via FetchContent
include(FetchContent)

# GLFW for windowing
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# GLM for math
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)

# WebSocket library
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.5
)
set(USE_TLS OFF CACHE BOOL "" FORCE)

# JSON library
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

# File watcher
FetchContent_Declare(
    efsw
    GIT_REPOSITORY https://github.com/SpartanJ/efsw.git
    GIT_TAG 1.4.0
)

# stb for image encoding (preview thumbnails)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)

# Snappy compression (for HAP video codec)
FetchContent_Declare(
    snappy
    GIT_REPOSITORY https://github.com/google/snappy.git
    GIT_TAG 1.2.1
)
set(SNAPPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SNAPPY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SNAPPY_INSTALL OFF CACHE BOOL "" FORCE)

# miniaudio for audio playback (single-header library)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.21
)

# Note: Assimp has been moved to optional addons
# Use addons/vivid-models for 3D model loading

FetchContent_MakeAvailable(glfw glm ixwebsocket nlohmann_json efsw stb snappy miniaudio)

# WebGPU via wgpu-native
# Note: wgpu-native releases provide prebuilt binaries
set(WGPU_VERSION "v24.0.0.2")

if(WIN32)
    set(WGPU_OS "windows")
    set(WGPU_ARCH "x86_64")
    set(WGPU_EXT "zip")
elseif(APPLE)
    set(WGPU_OS "macos")
    # Detect Apple Silicon vs Intel
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE HOST_ARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(HOST_ARCH STREQUAL "arm64")
        set(WGPU_ARCH "aarch64")
    else()
        set(WGPU_ARCH "x86_64")
    endif()
    set(WGPU_EXT "zip")
else()
    set(WGPU_OS "linux")
    set(WGPU_ARCH "x86_64")
    set(WGPU_EXT "zip")
endif()

set(WGPU_URL "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_VERSION}/wgpu-${WGPU_OS}-${WGPU_ARCH}-release.${WGPU_EXT}")
set(WGPU_DIR "${CMAKE_BINARY_DIR}/_deps/wgpu")

message(STATUS "Downloading wgpu-native from: ${WGPU_URL}")

file(DOWNLOAD ${WGPU_URL} "${WGPU_DIR}/wgpu.${WGPU_EXT}" STATUS WGPU_DOWNLOAD_STATUS)
list(GET WGPU_DOWNLOAD_STATUS 0 WGPU_DOWNLOAD_OK)
if(NOT WGPU_DOWNLOAD_OK EQUAL 0)
    message(FATAL_ERROR "Failed to download wgpu-native: ${WGPU_DOWNLOAD_STATUS}")
endif()

file(ARCHIVE_EXTRACT INPUT "${WGPU_DIR}/wgpu.${WGPU_EXT}" DESTINATION "${WGPU_DIR}")

# Create imported target for wgpu
add_library(wgpu STATIC IMPORTED GLOBAL)
if(WIN32)
    set_target_properties(wgpu PROPERTIES
        IMPORTED_LOCATION "${WGPU_DIR}/lib/wgpu_native.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${WGPU_DIR}/include"
    )
elseif(APPLE)
    set_target_properties(wgpu PROPERTIES
        IMPORTED_LOCATION "${WGPU_DIR}/lib/libwgpu_native.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WGPU_DIR}/include"
        INTERFACE_LINK_LIBRARIES "-framework Metal -framework QuartzCore -framework Foundation"
    )
else()
    set_target_properties(wgpu PROPERTIES
        IMPORTED_LOCATION "${WGPU_DIR}/lib/libwgpu_native.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WGPU_DIR}/include"
    )
endif()

add_subdirectory(runtime)
# Operators will be added in Phase 7
# add_subdirectory(operators)
