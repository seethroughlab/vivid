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
# WebGPU - Using the distribution from Learn WebGPU (eliemichel)
# This packages Dawn properly without the internal submodule issues
# -----------------------------------------------------------------------------

# Use the webgpu-distribution which provides pre-packaged Dawn
FetchContent_Declare(
    webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(webgpu)

# Also get the GLFW integration
FetchContent_Declare(
    glfw3webgpu
    GIT_REPOSITORY https://github.com/eliemichel/glfw3webgpu.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw3webgpu)

# -----------------------------------------------------------------------------
# Addons
# -----------------------------------------------------------------------------
add_subdirectory(addons/vivid-effects-2d)

# -----------------------------------------------------------------------------
# Core library and runtime
# -----------------------------------------------------------------------------
add_subdirectory(core)

# -----------------------------------------------------------------------------
# Examples
# -----------------------------------------------------------------------------
# Note: Example chains are hot-reloaded, not statically compiled.
# The examples/ directory contains chain.cpp files that are loaded at runtime.
