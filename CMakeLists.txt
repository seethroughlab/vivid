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

# --- Operator plugin suffix (platform-aware) ---
if(APPLE)
    set(VIVID_PLUGIN_SUFFIX ".dylib")
elseif(WIN32)
    set(VIVID_PLUGIN_SUFFIX ".dll")
else()
    set(VIVID_PLUGIN_SUFFIX ".so")
endif()

# --- Operator plugin helper ---
function(add_vivid_operator name source)
    cmake_parse_arguments(ARG "" "" "EXTRA_LIBS" ${ARGN})
    add_library(${name} MODULE ${source})
    set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
    target_link_libraries(${name} PRIVATE vivid_operator_api ${ARG_EXTRA_LIBS})
endfunction()

# --- Control operator plugins ---
add_vivid_operator(lfo            operators/control/lfo/lfo.cpp)
add_vivid_operator(clock          operators/control/clock/clock.cpp)
add_vivid_operator(math           operators/control/math/math.cpp)
add_vivid_operator(envelope       operators/control/envelope/envelope.cpp)
add_vivid_operator(midi_input     operators/control/midi_input/midi_input.cpp     EXTRA_LIBS rtmidi)
add_vivid_operator(fft_analysis   operators/control/fft_analysis/fft_analysis.cpp)
add_vivid_operator(note_pattern   operators/control/note_pattern/note_pattern.cpp)
add_vivid_operator(chord_progression operators/control/chord_progression/chord_progression.cpp)
add_vivid_operator(arpeggiator      operators/control/arpeggiator/arpeggiator.cpp)

# --- GPU operator plugins ---
add_vivid_operator(noise          operators/gpu/noise/noise.cpp          EXTRA_LIBS webgpu)
add_vivid_operator(shape          operators/gpu/shape/shape.cpp          EXTRA_LIBS webgpu)
add_vivid_operator(bars           operators/gpu/bars/bars.cpp            EXTRA_LIBS webgpu)
add_vivid_operator(composite      operators/gpu/composite/composite.cpp  EXTRA_LIBS webgpu)
add_vivid_operator(posterize      operators/gpu/posterize/posterize.cpp  EXTRA_LIBS webgpu)
add_vivid_operator(blur           operators/gpu/blur/blur.cpp            EXTRA_LIBS webgpu)
add_vivid_operator(halftone       operators/gpu/halftone/halftone.cpp    EXTRA_LIBS webgpu)
add_vivid_operator(levels         operators/gpu/levels/levels.cpp        EXTRA_LIBS webgpu)
add_vivid_operator(displace       operators/gpu/displace/displace.cpp    EXTRA_LIBS webgpu)
add_vivid_operator(solid_color          operators/gpu/solid_color/solid_color.cpp          EXTRA_LIBS webgpu)
add_vivid_operator(pixelate             operators/gpu/pixelate/pixelate.cpp                 EXTRA_LIBS webgpu)
add_vivid_operator(tile                 operators/gpu/tile/tile.cpp                         EXTRA_LIBS webgpu)
add_vivid_operator(chromatic_aberration operators/gpu/chromatic_aberration/chromatic_aberration.cpp EXTRA_LIBS webgpu)
add_vivid_operator(gradient             operators/gpu/gradient/gradient.cpp                       EXTRA_LIBS webgpu)
add_vivid_operator(hsv                  operators/gpu/hsv/hsv.cpp                                 EXTRA_LIBS webgpu)
add_vivid_operator(mirror               operators/gpu/mirror/mirror.cpp                           EXTRA_LIBS webgpu)
add_vivid_operator(edge                 operators/gpu/edge/edge.cpp                               EXTRA_LIBS webgpu)
add_vivid_operator(scanlines            operators/gpu/scanlines/scanlines.cpp                     EXTRA_LIBS webgpu)

# --- Movie File In (image + video loader) ---
if(APPLE)
    add_library(movie_file_in MODULE
        operators/gpu/movie_file_in/movie_file_in.cpp
        operators/gpu/movie_file_in/avf_decoder.mm
    )
    target_link_libraries(movie_file_in PRIVATE
        vivid_operator_api webgpu
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
        "-framework Foundation"
    )
    set_source_files_properties(operators/gpu/movie_file_in/avf_decoder.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    add_library(movie_file_in MODULE operators/gpu/movie_file_in/movie_file_in.cpp)
    target_link_libraries(movie_file_in PRIVATE vivid_operator_api webgpu)
endif()
set_target_properties(movie_file_in PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
target_include_directories(movie_file_in PRIVATE ${CMAKE_SOURCE_DIR}/deps/stb)

# --- Audio operator plugins ---
add_vivid_operator(oscillator     operators/audio/oscillator/oscillator.cpp)
add_vivid_operator(gain           operators/audio/gain/gain.cpp)
add_vivid_operator(drum_kick      operators/audio/drum_kick/drum_kick.cpp)
add_vivid_operator(drum_snare     operators/audio/drum_snare/drum_snare.cpp)
add_vivid_operator(drum_hihat     operators/audio/drum_hihat/drum_hihat.cpp)
add_vivid_operator(drum_clap      operators/audio/drum_clap/drum_clap.cpp)
add_vivid_operator(drum_tom       operators/audio/drum_tom/drum_tom.cpp)
add_vivid_operator(drum_cymbal    operators/audio/drum_cymbal/drum_cymbal.cpp)
add_vivid_operator(wavetable_synth operators/audio/wavetable_synth/wavetable_synth.cpp)
add_vivid_operator(spread_adsr    operators/audio/spread_adsr/spread_adsr.cpp)
add_vivid_operator(spread_lfo     operators/audio/spread_lfo/spread_lfo.cpp)
add_vivid_operator(reverb         operators/audio/reverb/reverb.cpp)
add_vivid_operator(delay          operators/audio/delay/delay.cpp)
add_vivid_operator(bitcrush       operators/audio/bitcrush/bitcrush.cpp)
add_vivid_operator(distortion     operators/audio/distortion/distortion.cpp)

# --- Glitch operator plugins ---
add_vivid_operator(stutter        operators/audio/stutter/stutter.cpp)
add_vivid_operator(tape_stop      operators/audio/tape_stop/tape_stop.cpp)
add_vivid_operator(beat_repeat    operators/audio/beat_repeat/beat_repeat.cpp)
add_vivid_operator(reverse        operators/audio/reverse/reverse.cpp)
add_vivid_operator(scratch        operators/audio/scratch/scratch.cpp)
add_vivid_operator(stretch        operators/audio/stretch/stretch.cpp)
add_vivid_operator(freq_shift     operators/audio/freq_shift/freq_shift.cpp)
add_vivid_operator(glitch         operators/audio/glitch/glitch.cpp)

# --- Movie File Audio In (audio extraction from video files) ---
if(APPLE)
    add_library(movie_file_audio_in MODULE
        operators/audio/movie_file_audio_in/movie_file_audio_in.cpp
        operators/audio/movie_file_audio_in/avf_audio_extractor.mm
    )
    target_link_libraries(movie_file_audio_in PRIVATE
        vivid_operator_api
        "-framework AVFoundation" "-framework CoreMedia" "-framework Foundation"
    )
    set_source_files_properties(
        operators/audio/movie_file_audio_in/avf_audio_extractor.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_target_properties(movie_file_audio_in PROPERTIES
        PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
endif()

# --- UI library ---
add_library(vivid_ui STATIC
    src/ui/renderer_2d.cpp
    src/ui/thumbnail_renderer.cpp
    src/ui/thumbnail_cache.cpp
    src/ui/node_graph.cpp
    src/ui/node_graph_draw.cpp
    src/ui/node_graph_input.cpp
    src/ui/ui_style.cpp
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
    src/runtime/system_midi.cpp
    src/runtime/platform.cpp
    src/runtime/settings.cpp
    src/runtime/editor_detect.cpp
)

target_include_directories(vivid PRIVATE src)
target_link_libraries(vivid PRIVATE vivid_ui webgpu glfw glfw3webgpu vivid_operator_api yyjson miniaudio stb_truetype ixwebsocket rtmidi)

# Copy demo graphs to build directory
configure_file(graphs/demo.json ${CMAKE_BINARY_DIR}/graph.json COPYONLY)
configure_file(graphs/audio_demo.json ${CMAKE_BINARY_DIR}/audio_demo.json COPYONLY)
configure_file(graphs/av_sync_demo.json ${CMAKE_BINARY_DIR}/av_sync_demo.json COPYONLY)
configure_file(graphs/midi_demo.json ${CMAKE_BINARY_DIR}/midi_demo.json COPYONLY)
configure_file(graphs/fft_bars_demo.json ${CMAKE_BINARY_DIR}/fft_bars_demo.json COPYONLY)
configure_file(graphs/polysynth_demo.json ${CMAKE_BINARY_DIR}/polysynth_demo.json COPYONLY)
configure_file(graphs/wavetable_demo.json ${CMAKE_BINARY_DIR}/wavetable_demo.json COPYONLY)
configure_file(graphs/external_adsr_demo.json ${CMAKE_BINARY_DIR}/external_adsr_demo.json COPYONLY)
configure_file(graphs/chord_progression_demo.json ${CMAKE_BINARY_DIR}/chord_progression_demo.json COPYONLY)
configure_file(graphs/displace_demo.json ${CMAKE_BINARY_DIR}/displace_demo.json COPYONLY)
configure_file(graphs/movie_file_in_demo.json ${CMAKE_BINARY_DIR}/movie_file_in_demo.json COPYONLY)
configure_file(graphs/mfi_halftone_demo.json ${CMAKE_BINARY_DIR}/mfi_halftone_demo.json COPYONLY)
configure_file(graphs/mfi_posterize_demo.json ${CMAKE_BINARY_DIR}/mfi_posterize_demo.json COPYONLY)
configure_file(graphs/mfi_blur_demo.json ${CMAKE_BINARY_DIR}/mfi_blur_demo.json COPYONLY)
configure_file(graphs/mfi_displace_demo.json ${CMAKE_BINARY_DIR}/mfi_displace_demo.json COPYONLY)
configure_file(graphs/mfi_composite_demo.json ${CMAKE_BINARY_DIR}/mfi_composite_demo.json COPYONLY)
configure_file(graphs/mfi_kitchen_sink_demo.json ${CMAKE_BINARY_DIR}/mfi_kitchen_sink_demo.json COPYONLY)
configure_file(graphs/mfi_av_sync_demo.json ${CMAKE_BINARY_DIR}/mfi_av_sync_demo.json COPYONLY)
configure_file(graphs/breakbeat_demo.json ${CMAKE_BINARY_DIR}/breakbeat_demo.json COPYONLY)
configure_file(graphs/glitch_demo.json ${CMAKE_BINARY_DIR}/glitch_demo.json COPYONLY)
configure_file(graphs/arpeggiator_demo.json ${CMAKE_BINARY_DIR}/arpeggiator_demo.json COPYONLY)
configure_file(graphs/gradient_demo.json ${CMAKE_BINARY_DIR}/gradient_demo.json COPYONLY)
configure_file(graphs/hsv_demo.json ${CMAKE_BINARY_DIR}/hsv_demo.json COPYONLY)
configure_file(graphs/mirror_demo.json ${CMAKE_BINARY_DIR}/mirror_demo.json COPYONLY)
configure_file(graphs/edge_demo.json ${CMAKE_BINARY_DIR}/edge_demo.json COPYONLY)
configure_file(graphs/scanlines_demo.json ${CMAKE_BINARY_DIR}/scanlines_demo.json COPYONLY)

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
add_vivid_operator(test_op_v1     tests/operators/test_op_v1.cpp)
add_vivid_operator(test_op_v2     tests/operators/test_op_v2.cpp)

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
    src/runtime/system_midi.cpp
)
target_include_directories(test_runtime_api PRIVATE src)
target_link_libraries(test_runtime_api PRIVATE vivid_operator_api yyjson miniaudio webgpu rtmidi)
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
    src/runtime/system_midi.cpp
)
target_include_directories(test_control_server PRIVATE src)
target_link_libraries(test_control_server PRIVATE
    vivid_operator_api yyjson miniaudio ixwebsocket webgpu rtmidi)
add_dependencies(test_control_server test_op_v1)
add_test(NAME test_control_server COMMAND test_control_server WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Test operator plugins
add_vivid_operator(audio_test_op    tests/operators/audio_test_op.cpp)
add_vivid_operator(control_pass_op  tests/operators/control_pass_op.cpp)
add_vivid_operator(spread_source_op tests/operators/spread_source_op.cpp)
add_vivid_operator(audio_spread_op  tests/operators/audio_spread_op.cpp)

# AudioEngine integration test (null audio backend, no GPU, no window)
add_executable(test_audio_engine
    tests/test_audio_engine.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
    src/runtime/runtime_api.cpp
    src/runtime/system_midi.cpp
)
target_include_directories(test_audio_engine PRIVATE src)
target_link_libraries(test_audio_engine PRIVATE
    vivid_operator_api yyjson miniaudio webgpu rtmidi)
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
target_link_libraries(test_operator_loader PRIVATE vivid_operator_api webgpu)
add_dependencies(test_operator_loader test_op_v1 test_op_v2 control_pass_op)
add_test(NAME test_operator_loader COMMAND test_operator_loader WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# GPU operator plugin for testing (solid color fill)
add_vivid_operator(gpu_fill_op      tests/operators/gpu_fill_op.cpp      EXTRA_LIBS webgpu)

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

# Cross-domain spread integration test
add_executable(test_cross_domain_spread
    tests/test_cross_domain_spread.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
)
target_include_directories(test_cross_domain_spread PRIVATE src)
target_link_libraries(test_cross_domain_spread PRIVATE
    vivid_operator_api yyjson miniaudio webgpu)
add_dependencies(test_cross_domain_spread spread_source_op audio_spread_op)
add_test(NAME test_cross_domain_spread COMMAND test_cross_domain_spread
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Audio spread wire integration test
add_executable(test_audio_spread_wire
    tests/test_audio_spread_wire.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
)
target_include_directories(test_audio_spread_wire PRIVATE src)
target_link_libraries(test_audio_spread_wire PRIVATE
    vivid_operator_api yyjson miniaudio webgpu)
add_dependencies(test_audio_spread_wire spread_source_op audio_spread_op spread_adsr)
add_test(NAME test_audio_spread_wire COMMAND test_audio_spread_wire
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Copy test graphs to build directory
configure_file(tests/graphs/test_reload.json ${CMAKE_BINARY_DIR}/test_reload.json COPYONLY)
configure_file(tests/graphs/test_runtime_api.json ${CMAKE_BINARY_DIR}/test_runtime_api.json COPYONLY)
configure_file(tests/graphs/test_audio_engine.json ${CMAKE_BINARY_DIR}/test_audio_engine.json COPYONLY)
configure_file(tests/graphs/test_cross_domain_spread.json ${CMAKE_BINARY_DIR}/test_cross_domain_spread.json COPYONLY)
configure_file(tests/graphs/test_audio_spread_wire.json ${CMAKE_BINARY_DIR}/test_audio_spread_wire.json COPYONLY)
