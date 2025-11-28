# Vivid Implementation Plan — Part 1: Overview

This document describes how to build the Vivid creative coding framework. It is intended to be used with Claude Code or similar LLM-assisted development tools.

## Project Overview

Vivid is a C++ creative coding framework with:
- Hot-reloading C++ operators (compiled as shared libraries)
- Hot-reloading WGSL shaders
- WebGPU rendering via wgpu-native (cross-platform: Windows, Linux, macOS)
- A VS Code extension that shows live previews inline with code
- WebSocket communication between runtime and editor

## Why WebGPU

WebGPU via wgpu-native provides:
- **True cross-platform support** — Native backends for Vulkan (Linux/Windows), Metal (macOS), and DX12 (Windows)
- **Modern API** — Explicit resource management, compute shaders, better performance
- **Future-proof** — WebGPU is the successor to WebGL, actively developed
- **WGSL shaders** — Clean, modern shader language (or use Naga to transpile from GLSL/SPIR-V)
- **No macOS deprecation issues** — Unlike OpenGL which is frozen at 4.1 on Mac

## Directory Structure

```
vivid/
├── CMakeLists.txt                 # Root CMake configuration
├── README.md
├── runtime/
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp               # Entry point, argument parsing
│   │   ├── app.h                  # Application state
│   │   ├── app.cpp                # Main loop, coordinates subsystems
│   │   ├── window.h               # GLFW window wrapper
│   │   ├── window.cpp
│   │   ├── renderer.h             # WebGPU renderer
│   │   ├── renderer.cpp
│   │   ├── shader.h               # Shader compilation and caching
│   │   ├── shader.cpp
│   │   ├── texture.h              # Texture management
│   │   ├── texture.cpp
│   │   ├── graph.h                # Operator graph, execution order
│   │   ├── graph.cpp
│   │   ├── hotload.h              # Shared library loading/unloading
│   │   ├── hotload.cpp
│   │   ├── file_watcher.h         # File system monitoring
│   │   ├── file_watcher.cpp
│   │   ├── compiler.h             # Invokes CMake/compiler for operators
│   │   ├── compiler.cpp
│   │   ├── preview_server.h       # WebSocket server for editor
│   │   ├── preview_server.cpp
│   │   ├── preview_capture.h      # Captures node outputs as images
│   │   └── preview_capture.cpp
│   └── include/
│       └── vivid/
│           ├── vivid.h            # Main include for user projects
│           ├── operator.h         # Base operator class
│           ├── context.h          # Passed to operators each frame
│           ├── types.h            # Texture, Buffer, Param types
│           ├── node_macro.h       # NODE() macro for registration
│           └── params.h           # Parameter declaration helpers
├── operators/
│   ├── CMakeLists.txt             # Builds each operator as .so/.dylib/.dll
│   ├── noise.cpp
│   ├── lfo.cpp
│   ├── feedback.cpp
│   ├── composite.cpp
│   ├── brightness.cpp
│   └── output.cpp
├── shaders/
│   ├── fullscreen.wgsl            # Fullscreen triangle vertex shader
│   ├── noise.wgsl
│   ├── feedback.wgsl
│   ├── composite.wgsl
│   └── util.wgsl                  # Common functions (noise, etc.)
├── extension/
│   ├── package.json
│   ├── tsconfig.json
│   ├── src/
│   │   ├── extension.ts           # Extension entry point
│   │   ├── runtimeClient.ts       # WebSocket connection to runtime
│   │   ├── previewPanel.ts        # Side panel showing all nodes
│   │   ├── decorations.ts         # Inline decorations in editor
│   │   └── commands.ts            # Command palette commands
│   └── resources/
│       └── icon.png
├── examples/
│   └── hello/
│       ├── CMakeLists.txt
│       ├── chain.cpp              # Example operator chain
│       └── shaders/
│           └── wobble.wgsl
└── scripts/
    ├── build.sh                   # Convenience build script
    └── run.sh                     # Build and run with example
```

---

## Root CMake Configuration

Create `CMakeLists.txt` at the project root:

```cmake
cmake_minimum_required(VERSION 3.20)
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

FetchContent_MakeAvailable(glfw glm ixwebsocket nlohmann_json efsw stb)

# WebGPU via wgpu-native
# Note: wgpu-native releases provide prebuilt binaries
# For simplicity, we download and link directly
include(ExternalProject)

set(WGPU_VERSION "v0.19.4.1")
if(WIN32)
    set(WGPU_OS "windows")
    set(WGPU_ARCH "x86_64")
    set(WGPU_EXT "zip")
elseif(APPLE)
    set(WGPU_OS "macos")
    set(WGPU_ARCH "x86_64")  # or arm64 for Apple Silicon
    set(WGPU_EXT "zip")
else()
    set(WGPU_OS "linux")
    set(WGPU_ARCH "x86_64")
    set(WGPU_EXT "zip")
endif()

set(WGPU_URL "https://github.com/AcademySoftwareFoundation/wgpu-native/releases/download/${WGPU_VERSION}/wgpu-${WGPU_OS}-${WGPU_ARCH}-release.${WGPU_EXT}")
set(WGPU_DIR "${CMAKE_BINARY_DIR}/_deps/wgpu")

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
add_subdirectory(operators)
```

---

## Runtime CMakeLists.txt

Create `runtime/CMakeLists.txt`:

```cmake
set(RUNTIME_SOURCES
    src/main.cpp
    src/app.cpp
    src/window.cpp
    src/renderer.cpp
    src/shader.cpp
    src/texture.cpp
    src/graph.cpp
    src/hotload.cpp
    src/file_watcher.cpp
    src/compiler.cpp
    src/preview_server.cpp
    src/preview_capture.cpp
)

add_executable(vivid-runtime ${RUNTIME_SOURCES})

target_include_directories(vivid-runtime
    PUBLIC include
    PRIVATE src
    PRIVATE ${stb_SOURCE_DIR}
)

target_link_libraries(vivid-runtime
    PRIVATE
    glfw
    glm::glm
    wgpu
    ixwebsocket
    nlohmann_json::nlohmann_json
    efsw
    ${CMAKE_DL_LIBS}
)

# Platform-specific libraries
if(APPLE)
    target_link_libraries(vivid-runtime PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
    )
elseif(UNIX)
    target_link_libraries(vivid-runtime PRIVATE pthread dl)
endif()

# Copy include headers to build directory for operator compilation
add_custom_command(TARGET vivid-runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

# Copy shaders to build directory
add_custom_command(TARGET vivid-runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders
    ${CMAKE_BINARY_DIR}/shaders
)
```

---

## Dependencies Summary

| Library | Purpose | Version |
|---------|---------|---------|
| GLFW | Windowing, input | 3.4 |
| GLM | Math (vec2, mat4, etc.) | 1.0.1 |
| wgpu-native | WebGPU implementation | 0.19.4.1 |
| IXWebSocket | WebSocket server | 11.4.5 |
| nlohmann/json | JSON serialization | 3.11.3 |
| efsw | File system watching | 1.4.0 |
| stb | Image encoding | latest |

---

## Build Instructions

```bash
# Clone the repository
git clone https://github.com/your-org/vivid
cd vivid

# Configure (downloads dependencies automatically)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j$(nproc)

# Run with example project
./build/bin/vivid-runtime examples/hello
```

### macOS Apple Silicon Note

For Apple Silicon Macs, you may need to specify the architecture:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
```

And update the `WGPU_ARCH` in CMakeLists.txt to `arm64`.

---

## Next Parts

- **PLAN-02-runtime.md** — Core runtime implementation, WebGPU renderer, hot-reload system
- **PLAN-03-operators.md** — Operator API, built-in operators, WGSL shaders
- **PLAN-04-extension.md** — VS Code extension, WebSocket protocol, inline decorations
