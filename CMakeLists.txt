cmake_minimum_required(VERSION 3.20)
project(vivid LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dawn/WebGPU via eliemichel's distribution (pre-built binaries) ---
include(FetchContent)
FetchContent_Declare(
    webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(webgpu)

# --- GLFW (submodule) ---
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/glfw)

# --- glfw3webgpu bridge (submodule) ---
add_subdirectory(deps/glfw3webgpu)

# --- Operator API (header-only interface library) ---
add_library(vivid_operator_api INTERFACE)
target_include_directories(vivid_operator_api INTERFACE src)

# --- LFO operator (dynamically loaded plugin) ---
add_library(lfo MODULE
    operators/control/lfo/lfo.cpp
)
set_target_properties(lfo PROPERTIES PREFIX "" SUFFIX ".dylib")  # lfo.dylib
target_link_libraries(lfo PRIVATE vivid_operator_api)

# --- Vivid executable ---
add_executable(vivid
    src/runtime/main.cpp
    src/runtime/gpu_context.cpp
    src/runtime/operator_loader.cpp
)

target_include_directories(vivid PRIVATE src)
target_link_libraries(vivid PRIVATE webgpu glfw glfw3webgpu vivid_operator_api)

# Copy Dawn shared library next to the executable (if dynamically linked)
target_copy_webgpu_binaries(vivid)

# Apple framework fallback (if not already linked by glfw3webgpu)
if(APPLE)
    target_link_libraries(vivid PRIVATE
        "-framework Cocoa"
        "-framework CoreVideo"
        "-framework IOKit"
        "-framework QuartzCore"
    )
endif()
