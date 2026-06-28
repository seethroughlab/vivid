cmake_minimum_required(VERSION 3.20)
project(vivid_poc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
endif()

include(FetchContent)

# --- wgpu-native via eliemichel's WebGPU-distribution (precompiled) ---
set(WGPU_VERSION "v29.0.0.0" CACHE STRING "wgpu-native release" FORCE)
FetchContent_Declare(webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
    GIT_TAG        17dcd42a7683355e7a40ac4e97e77f36dff5b5ab
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(webgpu)

# --- GLFW (window + input) ---
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glfw)

# --- glfw3webgpu (GLFW <-> WebGPU surface bridge) ---
FetchContent_Declare(glfw3webgpu
    GIT_REPOSITORY https://github.com/eliemichel/glfw3webgpu
    GIT_TAG        main
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glfw3webgpu)

# --- miniaudio (vendored, header-only) ---
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE deps/miniaudio)

add_executable(vivid_poc
    src/main.cpp
    src/miniaudio_impl.c
    src/gpu/gpu_context.cpp)
target_include_directories(vivid_poc PRIVATE src src/gpu)
target_link_libraries(vivid_poc PRIVATE webgpu glfw glfw3webgpu miniaudio)

if(APPLE)
    target_link_libraries(vivid_poc PRIVATE
        "-framework Cocoa" "-framework Metal" "-framework QuartzCore"
        "-framework IOKit" "-framework CoreAudio" "-framework AudioToolbox"
        "-framework CoreFoundation")
endif()

# Copy the wgpu-native dylib next to the executable at build time.
target_copy_webgpu_binaries(vivid_poc)
