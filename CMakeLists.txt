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

# --- yyjson (vendored JSON parser) ---
add_library(yyjson STATIC deps/yyjson/yyjson.c)
target_include_directories(yyjson PUBLIC deps/yyjson)

# --- Operator API (header-only interface library) ---
add_library(vivid_operator_api INTERFACE)
target_include_directories(vivid_operator_api INTERFACE src)

# --- Operator plugins (dynamically loaded) ---
add_library(lfo MODULE operators/control/lfo/lfo.cpp)
set_target_properties(lfo PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(lfo PRIVATE vivid_operator_api)

add_library(clock MODULE operators/control/clock/clock.cpp)
set_target_properties(clock PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(clock PRIVATE vivid_operator_api)

add_library(math MODULE operators/control/math/math.cpp)
set_target_properties(math PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(math PRIVATE vivid_operator_api)

# --- GPU operator plugins ---
add_library(noise MODULE operators/gpu/noise/noise.cpp)
set_target_properties(noise PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(noise PRIVATE vivid_operator_api webgpu)

# --- Vivid executable ---
add_executable(vivid
    src/runtime/main.cpp
    src/runtime/gpu_context.cpp
    src/runtime/fullscreen_blit.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
)

target_include_directories(vivid PRIVATE src)
target_link_libraries(vivid PRIVATE webgpu glfw glfw3webgpu vivid_operator_api yyjson)

# Copy demo graph to build directory
configure_file(graphs/demo.json ${CMAKE_BINARY_DIR}/graph.json COPYONLY)

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
