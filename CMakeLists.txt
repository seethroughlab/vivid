cmake_minimum_required(VERSION 3.20)
project(vivid VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(VIVID_BUILD_EXAMPLES "Build Vivid examples" ON)
option(VIVID_BUILD_ADDONS "Build Vivid addons" OFF)

# Platform detection
if(APPLE)
    message(STATUS "Building for macOS - will use Vulkan via MoltenVK")
    set(DILIGENT_NO_METAL ON CACHE BOOL "" FORCE)  # Metal requires DiligentCorePro
elseif(WIN32)
    message(STATUS "Building for Windows")
elseif(UNIX)
    message(STATUS "Building for Linux")
endif()

# Diligent Engine configuration - must be set BEFORE adding subdirectory
set(DILIGENT_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DILIGENT_NO_OPENGL ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_DIRECT3D12 ON CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_FX ON CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_TOOLS ON CACHE BOOL "" FORCE)

# Dependencies via FetchContent
include(FetchContent)

# GLFW for windowing
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# GLM for math
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)

# Diligent Engine (unified repo with all components)
FetchContent_Declare(
    DiligentEngine
    GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentEngine.git
    GIT_TAG v2.5.6
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE
)

message(STATUS "Fetching dependencies...")
FetchContent_MakeAvailable(glfw glm)

# Diligent needs special handling - populate first, then add
FetchContent_GetProperties(DiligentEngine)
if(NOT diligentengine_POPULATED)
    FetchContent_Populate(DiligentEngine)
    add_subdirectory(${diligentengine_SOURCE_DIR} ${diligentengine_BINARY_DIR})
endif()

# Runtime library
add_subdirectory(runtime)

# Examples
if(VIVID_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Addons
if(VIVID_BUILD_ADDONS)
    add_subdirectory(addons)
endif()
