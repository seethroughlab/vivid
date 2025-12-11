cmake_minimum_required(VERSION 3.20)
project(vivid VERSION 3.0.0 LANGUAGES CXX C)

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

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)

# GLM - Math library
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

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
    set(WGPU_TOOLCHAIN "")
    set(WGPU_EXT "zip")
else()
    set(WGPU_OS "linux")
    set(WGPU_ARCH "x86_64")
    set(WGPU_TOOLCHAIN "-gnu")
    set(WGPU_EXT "zip")
endif()

set(WGPU_URL "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_VERSION}/wgpu-${WGPU_OS}-${WGPU_ARCH}${WGPU_TOOLCHAIN}-release.${WGPU_EXT}")
set(WGPU_DIR "${CMAKE_BINARY_DIR}/_deps/wgpu")

message(STATUS "Downloading wgpu-native from: ${WGPU_URL}")

file(DOWNLOAD ${WGPU_URL} "${WGPU_DIR}/wgpu.${WGPU_EXT}" STATUS WGPU_DOWNLOAD_STATUS)
list(GET WGPU_DOWNLOAD_STATUS 0 WGPU_DOWNLOAD_OK)
if(NOT WGPU_DOWNLOAD_OK EQUAL 0)
    message(FATAL_ERROR "Failed to download wgpu-native: ${WGPU_DOWNLOAD_STATUS}")
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

# -----------------------------------------------------------------------------
# Addons (optional libraries with external dependencies)
# Note: ImGui is now integrated into core - it's not optional
# vivid-io must come first (provides image loading for other addons)
# -----------------------------------------------------------------------------
add_subdirectory(addons/vivid-io)
add_subdirectory(addons/vivid-effects-2d)
add_subdirectory(addons/vivid-video)
add_subdirectory(addons/vivid-render3d)
add_subdirectory(addons/vivid-audio)

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
