cmake_minimum_required(VERSION 3.16)

# Generate XCode schema files
set(CMAKE_XCODE_GENERATE_SCHEME TRUE)
set(CMAKE_XCODE_SCHEME_MALLOC_SCRIBBLE YES)
set(CMAKE_XCODE_SCHEME_MALLOC_GUARD_EDGES YES)

project(Vivid CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Platform detection
if(APPLE)
    set(PLATFORM_MACOS TRUE)
    enable_language(OBJC OBJCXX)
elseif(WIN32)
    set(PLATFORM_WIN32 TRUE)
elseif(UNIX)
    set(PLATFORM_LINUX TRUE)
endif()

# Diligent Engine options - we want Vulkan, Tools, and FX
set(DILIGENT_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_FX ON CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_DOCS OFF CACHE BOOL "" FORCE)

# Add Diligent Engine
add_subdirectory(external/DiligentEngine)

# Add glm
set(GLM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/glm")
if(NOT EXISTS "${GLM_DIR}/CMakeLists.txt")
    message(STATUS "Fetching glm...")
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        SOURCE_DIR "${GLM_DIR}"
    )
    FetchContent_MakeAvailable(glm)
else()
    add_subdirectory(${GLM_DIR})
endif()

# GLFW is already part of DiligentSamples/ThirdParty
# We'll use it from there
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/DiligentEngine/DiligentSamples/ThirdParty/glfw")
if(NOT TARGET glfw)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${GLFW_DIR})
endif()

# Add Vivid runtime
add_subdirectory(runtime)

# Set startup project for IDEs
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT vivid)
