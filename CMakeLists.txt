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

FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket
    GIT_TAG        v11.4.5
    GIT_SHALLOW    TRUE
)
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ixwebsocket)

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

# --- miniaudio (vendored, header-only) ---
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE deps/miniaudio)

# --- stb_truetype (vendored, header-only) ---
add_library(stb_truetype INTERFACE)
target_include_directories(stb_truetype INTERFACE deps/stb)

# --- RtMidi (vendored, CoreMIDI backend on macOS) ---
add_library(rtmidi STATIC deps/rtmidi/RtMidi.cpp)
target_include_directories(rtmidi PUBLIC deps/rtmidi)
target_compile_definitions(rtmidi PUBLIC __MACOSX_CORE__)
target_link_libraries(rtmidi PUBLIC
    "-framework CoreMIDI"
    "-framework CoreAudio"
    "-framework CoreFoundation"
)

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

add_library(envelope MODULE operators/control/envelope/envelope.cpp)
set_target_properties(envelope PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(envelope PRIVATE vivid_operator_api)

add_library(midi_input MODULE operators/control/midi_input/midi_input.cpp)
set_target_properties(midi_input PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(midi_input PRIVATE vivid_operator_api rtmidi)

add_library(fft_analysis MODULE operators/control/fft_analysis/fft_analysis.cpp)
set_target_properties(fft_analysis PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(fft_analysis PRIVATE vivid_operator_api)

# --- GPU operator plugins ---
add_library(noise MODULE operators/gpu/noise/noise.cpp)
set_target_properties(noise PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(noise PRIVATE vivid_operator_api webgpu)

add_library(shape MODULE operators/gpu/shape/shape.cpp)
set_target_properties(shape PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(shape PRIVATE vivid_operator_api webgpu)

add_library(bars MODULE operators/gpu/bars/bars.cpp)
set_target_properties(bars PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(bars PRIVATE vivid_operator_api webgpu)

add_library(composite MODULE operators/gpu/composite/composite.cpp)
set_target_properties(composite PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(composite PRIVATE vivid_operator_api webgpu)

# --- Audio operator plugins ---
add_library(oscillator MODULE operators/audio/oscillator/oscillator.cpp)
set_target_properties(oscillator PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(oscillator PRIVATE vivid_operator_api)

add_library(gain MODULE operators/audio/gain/gain.cpp)
set_target_properties(gain PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(gain PRIVATE vivid_operator_api)

add_library(drum MODULE operators/audio/drum/drum.cpp)
set_target_properties(drum PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(drum PRIVATE vivid_operator_api)

# --- UI library ---
add_library(vivid_ui STATIC
    src/ui/renderer_2d.cpp
    src/ui/thumbnail_renderer.cpp
    src/ui/thumbnail_cache.cpp
    src/ui/node_graph.cpp
    src/ui/node_graph_draw.cpp
    src/ui/node_graph_input.cpp
)
target_include_directories(vivid_ui PRIVATE src)
target_link_libraries(vivid_ui PUBLIC vivid_operator_api webgpu)
target_link_libraries(vivid_ui PRIVATE stb_truetype glfw)

# --- Vivid executable ---
add_executable(vivid
    src/runtime/main.cpp
    src/runtime/gpu_context.cpp
    src/runtime/fullscreen_blit.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
    src/runtime/file_watcher.cpp
    src/runtime/hot_reload.cpp
    src/runtime/runtime_api.cpp
    src/runtime/screenshot.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/control_server.cpp
)

target_include_directories(vivid PRIVATE src)
target_link_libraries(vivid PRIVATE vivid_ui webgpu glfw glfw3webgpu vivid_operator_api yyjson miniaudio stb_truetype ixwebsocket)

# Copy demo graphs to build directory
configure_file(graphs/demo.json ${CMAKE_BINARY_DIR}/graph.json COPYONLY)
configure_file(graphs/audio_demo.json ${CMAKE_BINARY_DIR}/audio_demo.json COPYONLY)
configure_file(graphs/av_sync_demo.json ${CMAKE_BINARY_DIR}/av_sync_demo.json COPYONLY)
configure_file(graphs/midi_demo.json ${CMAKE_BINARY_DIR}/midi_demo.json COPYONLY)
configure_file(graphs/fft_bars_demo.json ${CMAKE_BINARY_DIR}/fft_bars_demo.json COPYONLY)

# Copy font to build directory
configure_file(fonts/JetBrainsMono-Regular.ttf ${CMAKE_BINARY_DIR}/JetBrainsMono-Regular.ttf COPYONLY)

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

enable_testing()

# --- Tests ---
# Test operator plugins (two versions of the same operator)
add_library(test_op_v1 MODULE tests/operators/test_op_v1.cpp)
set_target_properties(test_op_v1 PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(test_op_v1 PRIVATE vivid_operator_api)

add_library(test_op_v2 MODULE tests/operators/test_op_v2.cpp)
set_target_properties(test_op_v2 PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(test_op_v2 PRIVATE vivid_operator_api)

# Hot-reload integration test (no GPU, no audio, no window)
add_executable(test_hot_reload
    tests/test_hot_reload.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
)
target_include_directories(test_hot_reload PRIVATE src)
target_link_libraries(test_hot_reload PRIVATE vivid_operator_api yyjson webgpu)
add_dependencies(test_hot_reload test_op_v1 test_op_v2)
add_test(NAME test_hot_reload COMMAND test_hot_reload WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# RuntimeAPI integration test (no GPU, no audio, no window)
add_executable(test_runtime_api
    tests/test_runtime_api.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
    src/runtime/runtime_api.cpp
)
target_include_directories(test_runtime_api PRIVATE src)
target_link_libraries(test_runtime_api PRIVATE vivid_operator_api yyjson miniaudio webgpu)
add_dependencies(test_runtime_api test_op_v1)
add_test(NAME test_runtime_api COMMAND test_runtime_api WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# ControlServer integration test (HTTP endpoints, no GPU, no audio, no window)
add_executable(test_control_server
    tests/test_control_server.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
    src/runtime/runtime_api.cpp
    src/runtime/control_server.cpp
)
target_include_directories(test_control_server PRIVATE src)
target_link_libraries(test_control_server PRIVATE
    vivid_operator_api yyjson miniaudio ixwebsocket webgpu)
add_dependencies(test_control_server test_op_v1)
add_test(NAME test_control_server COMMAND test_control_server WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Audio test operator plugin
add_library(audio_test_op MODULE tests/operators/audio_test_op.cpp)
set_target_properties(audio_test_op PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(audio_test_op PRIVATE vivid_operator_api)

add_library(control_pass_op MODULE tests/operators/control_pass_op.cpp)
set_target_properties(control_pass_op PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(control_pass_op PRIVATE vivid_operator_api)

add_library(spread_source_op MODULE tests/operators/spread_source_op.cpp)
set_target_properties(spread_source_op PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(spread_source_op PRIVATE vivid_operator_api)

# AudioEngine integration test (null audio backend, no GPU, no window)
add_executable(test_audio_engine
    tests/test_audio_engine.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
    src/runtime/runtime_api.cpp
)
target_include_directories(test_audio_engine PRIVATE src)
target_link_libraries(test_audio_engine PRIVATE
    vivid_operator_api yyjson miniaudio webgpu)
add_dependencies(test_audio_engine test_op_v1 audio_test_op)
add_test(NAME test_audio_engine COMMAND test_audio_engine WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Scheduler unit test (no GPU, no audio playback, no window)
add_executable(test_scheduler
    tests/test_scheduler.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
)
target_include_directories(test_scheduler PRIVATE src)
target_link_libraries(test_scheduler PRIVATE vivid_operator_api yyjson webgpu)
add_dependencies(test_scheduler test_op_v1 control_pass_op spread_source_op audio_test_op)
add_test(NAME test_scheduler COMMAND test_scheduler WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Graph data-model unit test (no operators, no scheduler, no GPU)
add_executable(test_graph
    tests/test_graph.cpp
    src/runtime/graph.cpp
)
target_include_directories(test_graph PRIVATE src)
target_link_libraries(test_graph PRIVATE yyjson)
add_test(NAME test_graph COMMAND test_graph WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# OperatorLoader + OperatorRegistry unit tests (no GPU, no scheduler, no graph)
add_executable(test_operator_loader
    tests/test_operator_loader.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
)
target_include_directories(test_operator_loader PRIVATE src)
target_link_libraries(test_operator_loader PRIVATE vivid_operator_api)
add_dependencies(test_operator_loader test_op_v1 test_op_v2 control_pass_op)
add_test(NAME test_operator_loader COMMAND test_operator_loader WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# GPU operator plugin for testing (solid color fill)
add_library(gpu_fill_op MODULE tests/operators/gpu_fill_op.cpp)
set_target_properties(gpu_fill_op PROPERTIES PREFIX "" SUFFIX ".dylib")
target_link_libraries(gpu_fill_op PRIVATE vivid_operator_api webgpu)

# GPU operator integration tests (headless WebGPU, no window)
add_executable(test_gpu_operators
    tests/test_gpu_operators.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
)
target_include_directories(test_gpu_operators PRIVATE src)
target_link_libraries(test_gpu_operators PRIVATE vivid_operator_api yyjson webgpu)
add_dependencies(test_gpu_operators gpu_fill_op shape)
add_test(NAME test_gpu_operators COMMAND test_gpu_operators WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Copy test graphs to build directory
configure_file(tests/graphs/test_reload.json ${CMAKE_BINARY_DIR}/test_reload.json COPYONLY)
configure_file(tests/graphs/test_runtime_api.json ${CMAKE_BINARY_DIR}/test_runtime_api.json COPYONLY)
configure_file(tests/graphs/test_audio_engine.json ${CMAKE_BINARY_DIR}/test_audio_engine.json COPYONLY)
