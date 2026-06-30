cmake_minimum_required(VERSION 3.20)
project(vivid_poc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
endif()

# ---- build options ----
# The GUI app pulls heavy, partly macOS-only deps (wgpu/glfw/VST3/AVFoundation);
# the headless test suite needs none of them. Keeping them separable lets CI
# build+run the tests fast and cross-platform (VIVID_BUILD_APP=OFF).
option(VIVID_BUILD_APP   "Build the GUI/GPU/audio app (heavy deps; macOS)" ON)
option(VIVID_BUILD_TESTS "Build the headless test suite (cheap, portable)"  ON)
option(VIVID_SANITIZE        "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(VIVID_SANITIZE_THREAD "Enable ThreadSanitizer (mutually exclusive with VIVID_SANITIZE)" OFF)

if(VIVID_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls)
    add_link_options(-fsanitize=address,undefined)
endif()
if(VIVID_SANITIZE_THREAD)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

# nlohmann/json is header-only and shared by the app AND the headless tests
# (control-server JSON shape, persistence), so fetch it before the app guard.
include(FetchContent)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

if(VIVID_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NOT VIVID_BUILD_APP)
    return()  # tests-only configure: skip all the heavy app deps + target below
endif()

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
# Pinned to a specific commit (was tracking `main`) for reproducible builds. This
# is the exact commit the PoC has been building against; the project's latest tag
# (v1.2.0) is newer and untested here, so bump deliberately, not implicitly.
# GIT_SHALLOW is FALSE here: a shallow clone can't reliably fetch a commit that
# isn't a branch/tag tip, and this is a mid-history SHA. The repo is tiny.
FetchContent_Declare(glfw3webgpu
    GIT_REPOSITORY https://github.com/eliemichel/glfw3webgpu
    GIT_TAG        8f14534e79c590e8a7f7795ea42e01bedea9e137
    GIT_SHALLOW    FALSE)
FetchContent_MakeAvailable(glfw3webgpu)

# --- miniaudio (vendored, header-only) ---
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE deps/miniaudio)

# --- nlohmann/json: fetched above the app guard (shared with headless tests) ---

# --- cpp-httplib (header-only; loopback control server for MCP) ---
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE    OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
    GIT_TAG        v0.18.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(cpp_httplib)

# --- VST3 SDK (header-only pluginterfaces + IID sources) ---
# SOURCE_SUBDIR points at a nonexistent dir so MakeAvailable populates the
# sources without running the SDK's own CMakeLists (hundreds of unwanted targets).
FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG        v3.8.0_build_66
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES "base pluginterfaces"
    SOURCE_SUBDIR  _skip_sdk_cmake)
FetchContent_MakeAvailable(vst3sdk)

add_library(vst3_headers INTERFACE)
target_include_directories(vst3_headers INTERFACE ${vst3sdk_SOURCE_DIR})

add_library(vst3_iids STATIC
    ${vst3sdk_SOURCE_DIR}/pluginterfaces/base/coreiids.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/baseiids.cpp
    src/audio/vst3_vstiids.cpp)
target_link_libraries(vst3_iids PUBLIC vst3_headers)

# MACOSX_BUNDLE is required so hosted VST3 plugin GUI windows receive mouse
# events: a non-bundled CLI process can't become a proper foreground app, so its
# secondary NSWindows render but stay non-interactive.
add_executable(vivid_poc MACOSX_BUNDLE
    src/main.cpp
    src/app/app.cpp
    src/app/input.cpp
    src/app/frame.cpp
    src/miniaudio_impl.c
    src/gpu/gpu_context.cpp
    src/gpu/video_player.mm
    src/gpu/visual_graph.cpp
    src/gpu/shader_op.cpp
    src/gpu/effect_op.cpp
    src/audio/vst3_host.cpp
    src/audio/audio_callback.cpp
    src/audio/sampler.cpp
    src/audio/vst3_plugin_window.mm
    src/platform/platform.cpp
    src/platform/macos_frame_timer.cpp
    src/platform/macos_app_nap.mm
    src/platform/frame_loop_generic.cpp
    src/platform/app_nap_stub.cpp
    src/persist.cpp
    src/operator_api/operator_descriptor_validation.cpp
    src/gpu/op_runtime.cpp
    src/gpu/builtin_ops.cpp
    src/gpu/operator_loader.cpp
    src/gpu/loaded_operator.cpp
    src/gpu/operator_scan.cpp
    src/packages/package_manifest.cpp
    src/packages/package_compiler.cpp
    src/packages/package_manager.cpp
    src/packages/file_watcher.cpp
    src/packages/hot_reload.cpp
    src/packages/hot_reload_manager.cpp
    src/cli/control_server.cpp
    src/ui/renderer_2d.cpp
    src/ui/mapping_overview.cpp
    src/ui/session_view.cpp
    src/ui/node_graph.cpp
    src/ui/clip_editor.cpp)
set_target_properties(vivid_poc PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Vivid PoC"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.vivid.poc"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1")
target_include_directories(vivid_poc PRIVATE src src/gpu src/audio src/ui deps/stb)
# Package compiler (P2.3) compiles operator sources at runtime via clang++; pass it
# the toolchain + the in-tree operator_api headers + the wgpu include/lib dirs.
file(GLOB _wgpu_dist "${CMAKE_BINARY_DIR}/_deps/wgpu-*-src")
set(_wgpu_root "")
if(_wgpu_dist)
    list(GET _wgpu_dist 0 _wgpu_root)
endif()
target_compile_definitions(vivid_poc PRIVATE
    VIVID_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/JetBrainsMono-Regular.ttf"
    VIVID_PKG_CXX="${CMAKE_CXX_COMPILER}"
    VIVID_PKG_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
    VIVID_PKG_WEBGPU_INCLUDE_DIR="${_wgpu_root}/include"
    VIVID_PKG_WEBGPU_LIB_DIR="${_wgpu_root}/lib"
    VIVID_PKG_ARCH="${CMAKE_OSX_ARCHITECTURES}")
target_link_libraries(vivid_poc PRIVATE webgpu glfw glfw3webgpu miniaudio vst3_iids z nlohmann_json::nlohmann_json httplib::httplib "-framework Cocoa")

if(APPLE)
    target_link_libraries(vivid_poc PRIVATE
        "-framework Cocoa" "-framework Metal" "-framework QuartzCore"
        "-framework IOKit" "-framework CoreAudio" "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo")
endif()

# Copy the wgpu-native dylib next to the executable at build time.
target_copy_webgpu_binaries(vivid_poc)

# Loadable operator dylibs (P2). Declared after vivid_poc so the operator targets
# can reference the bundle (for the PlugIns/ copy) + force them to build with the app.
add_subdirectory(operators)
add_dependencies(vivid_poc vivid_spike_solid)
