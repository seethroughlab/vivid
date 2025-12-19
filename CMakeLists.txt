cmake_minimum_required(VERSION 3.20)
project(vivid VERSION 0.1.1 LANGUAGES CXX C)

# Allow version override from CI (extracts from git tag)
if(DEFINED VIVID_VERSION_OVERRIDE)
    set(VIVID_VERSION "${VIVID_VERSION_OVERRIDE}")
    message(STATUS "Using version from CI: ${VIVID_VERSION}")
else()
    set(VIVID_VERSION "${CMAKE_PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(VIVID_BUILD_EXAMPLES "Build example projects" ON)

# -----------------------------------------------------------------------------
# Dependencies via FetchContent
# -----------------------------------------------------------------------------
include(FetchContent)

# GLFW - Cross-platform window management
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# On Windows, build GLFW as a shared library (DLL) so that vivid-core.dll
# and vivid.exe share the same GLFW instance. Without this, window handles
# created by one copy of GLFW aren't recognized by the other copy.
if(WIN32)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)

# Reset BUILD_SHARED_LIBS after GLFW
if(WIN32)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

# GLM - Math library
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# CLI11 - Command-line argument parser (header-only)
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cli11)

# nlohmann/json - JSON library (header-only)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# -----------------------------------------------------------------------------
# WebGPU via wgpu-native (prebuilt binaries)
# Using prebuilt binaries for reliable cross-platform support
# -----------------------------------------------------------------------------

set(WGPU_VERSION "v24.0.0.2")

if(WIN32)
    set(WGPU_OS "windows")
    set(WGPU_ARCH "x86_64")
    set(WGPU_TOOLCHAIN "-msvc")
    set(WGPU_EXT "zip")
elseif(APPLE)
    set(WGPU_OS "macos")
    # Allow override for cross-compilation (e.g., building x86_64 on ARM64)
    if(DEFINED WGPU_FORCE_ARCH)
        set(WGPU_ARCH "${WGPU_FORCE_ARCH}")
        message(STATUS "Using forced wgpu architecture: ${WGPU_ARCH}")
    else()
        execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE HOST_ARCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        message(STATUS "Detected macOS architecture: ${HOST_ARCH}")
        if(HOST_ARCH STREQUAL "arm64")
            set(WGPU_ARCH "aarch64")
        else()
            set(WGPU_ARCH "x86_64")
        endif()
    endif()
    set(WGPU_TOOLCHAIN "")
    set(WGPU_EXT "zip")
else()
    set(WGPU_OS "linux")
    # Support cross-compilation via WGPU_FORCE_ARCH or CMAKE_SYSTEM_PROCESSOR
    if(DEFINED WGPU_FORCE_ARCH)
        set(WGPU_ARCH "${WGPU_FORCE_ARCH}")
        message(STATUS "Using forced wgpu architecture: ${WGPU_ARCH}")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(WGPU_ARCH "aarch64")
        message(STATUS "Detected Linux aarch64 cross-compile")
    else()
        set(WGPU_ARCH "x86_64")
    endif()
    set(WGPU_TOOLCHAIN "")
    set(WGPU_EXT "zip")
endif()

set(WGPU_URL "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_VERSION}/wgpu-${WGPU_OS}-${WGPU_ARCH}${WGPU_TOOLCHAIN}-release.${WGPU_EXT}")
set(WGPU_DIR "${CMAKE_BINARY_DIR}/_deps/wgpu")

# Create directory first
file(MAKE_DIRECTORY "${WGPU_DIR}")

# Check if already downloaded (e.g., by CI pre-download step)
if(EXISTS "${WGPU_DIR}/wgpu.${WGPU_EXT}")
    file(SIZE "${WGPU_DIR}/wgpu.${WGPU_EXT}" WGPU_FILE_SIZE)
    if(WGPU_FILE_SIZE GREATER 1000)
        message(STATUS "Using pre-downloaded wgpu-native (${WGPU_FILE_SIZE} bytes)")
        set(WGPU_SKIP_DOWNLOAD TRUE)
    endif()
endif()

if(NOT WGPU_SKIP_DOWNLOAD)
    message(STATUS "Downloading wgpu-native from: ${WGPU_URL}")

    # Try CMake download first
    file(DOWNLOAD ${WGPU_URL} "${WGPU_DIR}/wgpu.${WGPU_EXT}"
        STATUS WGPU_DOWNLOAD_STATUS
        TIMEOUT 120
        INACTIVITY_TIMEOUT 30
        SHOW_PROGRESS
    )
    list(GET WGPU_DOWNLOAD_STATUS 0 WGPU_DOWNLOAD_OK)
    list(GET WGPU_DOWNLOAD_STATUS 1 WGPU_DOWNLOAD_MSG)

    # If CMake download failed, try curl as fallback (handles redirects better)
    if(NOT WGPU_DOWNLOAD_OK EQUAL 0)
        message(STATUS "CMake download failed (${WGPU_DOWNLOAD_MSG}), trying curl...")
        find_program(CURL_EXECUTABLE curl)
        if(CURL_EXECUTABLE)
            execute_process(
                COMMAND ${CURL_EXECUTABLE} -fL --retry 3 --retry-delay 5 -o "${WGPU_DIR}/wgpu.${WGPU_EXT}" "${WGPU_URL}"
                RESULT_VARIABLE CURL_RESULT
            )
            if(NOT CURL_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to download wgpu-native via curl (exit code ${CURL_RESULT})\nURL: ${WGPU_URL}")
            endif()
        else()
            message(FATAL_ERROR "Failed to download wgpu-native: ${WGPU_DOWNLOAD_MSG}\nURL: ${WGPU_URL}")
        endif()
    endif()

    # Verify the downloaded file isn't empty
    file(SIZE "${WGPU_DIR}/wgpu.${WGPU_EXT}" WGPU_FILE_SIZE)
    if(WGPU_FILE_SIZE LESS 1000)
        message(FATAL_ERROR "Downloaded wgpu-native file is too small (${WGPU_FILE_SIZE} bytes) - download may have failed")
    endif()
    message(STATUS "Downloaded wgpu-native (${WGPU_FILE_SIZE} bytes)")
endif()

file(ARCHIVE_EXTRACT INPUT "${WGPU_DIR}/wgpu.${WGPU_EXT}" DESTINATION "${WGPU_DIR}")

# Create imported target for wgpu-native
add_library(webgpu STATIC IMPORTED GLOBAL)
if(WIN32)
    set_target_properties(webgpu PROPERTIES
        IMPORTED_LOCATION "${WGPU_DIR}/lib/wgpu_native.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${WGPU_DIR}/include"
    )
    # Windows needs these system libraries for wgpu-native
    set_property(TARGET webgpu APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES "ws2_32;userenv;bcrypt;d3dcompiler;ntdll;opengl32;propsys;runtimeobject"
    )
elseif(APPLE)
    set_target_properties(webgpu PROPERTIES
        IMPORTED_LOCATION "${WGPU_DIR}/lib/libwgpu_native.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WGPU_DIR}/include"
        INTERFACE_LINK_LIBRARIES "-framework Metal -framework QuartzCore -framework Foundation"
    )
else()
    set_target_properties(webgpu PROPERTIES
        IMPORTED_LOCATION "${WGPU_DIR}/lib/libwgpu_native.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WGPU_DIR}/include"
    )
endif()

# GLFW-WebGPU integration
FetchContent_Declare(
    glfw3webgpu
    GIT_REPOSITORY https://github.com/eliemichel/glfw3webgpu.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw3webgpu)

# glfw3webgpu needs platform defines that are private in glfw
if(WIN32)
    target_compile_definitions(glfw3webgpu PRIVATE _GLFW_WIN32)
endif()

# IXWebSocket - WebSocket server for EditorBridge and vivid-network
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(USE_OPEN_SSL OFF CACHE BOOL "" FORCE)
set(USE_MBED_TLS OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v11.4.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ixwebsocket)

# -----------------------------------------------------------------------------
# Addons (optional libraries with external dependencies)
# Note: ImGui is now integrated into core - it's not optional
# vivid-io is now part of core (provides image loading)
# vivid-effects-2d is now part of core (provides 2D texture effects)
# -----------------------------------------------------------------------------

add_subdirectory(addons/vivid-network)
add_subdirectory(addons/vivid-serial)

# Cross-platform addons
add_subdirectory(addons/vivid-video)
add_subdirectory(addons/vivid-render3d)
add_subdirectory(addons/vivid-audio)

# MIDI addon (depends on vivid-audio)
add_subdirectory(addons/vivid-midi)
# vivid-ml moved to separate repo: https://github.com/jeff/vivid-ml

# -----------------------------------------------------------------------------
# Core library and runtime
# -----------------------------------------------------------------------------
add_subdirectory(core)

# -----------------------------------------------------------------------------
# Examples
# -----------------------------------------------------------------------------
# Note: Example chains are hot-reloaded, not statically compiled.
# The examples/ directory contains chain.cpp files that are loaded at runtime.

# -----------------------------------------------------------------------------
# Tests (optional)
# -----------------------------------------------------------------------------
option(BUILD_TESTS "Build the test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
# Documentation (optional)
# -----------------------------------------------------------------------------
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    message(STATUS "Doxygen found - 'make docs' target available")
else()
    message(STATUS "Doxygen not found - documentation target disabled")
endif()
