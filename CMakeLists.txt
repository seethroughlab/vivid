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

# CLI11 - Command-line argument parser (header-only)
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.6.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cli11)

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

# --- Composable operator headers (for ChildOp<T> consumers) ---
add_library(vivid_composable_ops INTERFACE)
target_include_directories(vivid_composable_ops INTERFACE ${CMAKE_SOURCE_DIR}/operators)
target_link_libraries(vivid_composable_ops INTERFACE vivid_operator_api)

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
add_vivid_operator(logic             operators/control/logic/logic.cpp)
add_vivid_operator(gate              operators/control/gate/gate.cpp)
add_vivid_operator(random            operators/control/random/random.cpp)
add_vivid_operator(smooth            operators/control/smooth/smooth.cpp)
add_vivid_operator(sequencer         operators/control/sequencer/sequencer.cpp)
add_vivid_operator(drum_sequencer    operators/control/drum_sequencer/drum_sequencer.cpp)
add_vivid_operator(euclidean         operators/control/euclidean/euclidean.cpp)
add_vivid_operator(pattern_seq       operators/control/pattern_seq/pattern_seq.cpp)
add_vivid_operator(stack             operators/control/stack/stack.cpp)
add_vivid_operator(alternate         operators/control/alternate/alternate.cpp)
add_vivid_operator(pat_transform     operators/control/pat_transform/pat_transform.cpp)
add_vivid_operator(modulated_gain   operators/control/modulated_gain/modulated_gain.cpp
                                    EXTRA_LIBS vivid_composable_ops)
add_vivid_operator(state_machine    operators/control/state_machine/state_machine.cpp)

# --- GPU operator plugins (complex operators with custom pipelines) ---
add_vivid_operator(noise          operators/gpu/noise/noise.cpp          EXTRA_LIBS webgpu)
add_vivid_operator(shape          operators/gpu/shape/shape.cpp          EXTRA_LIBS webgpu)
add_vivid_operator(bars           operators/gpu/bars/bars.cpp            EXTRA_LIBS webgpu)
add_vivid_operator(composite      operators/gpu/composite/composite.cpp  EXTRA_LIBS webgpu)
add_vivid_operator(texture_analysis operators/gpu/texture_analysis/texture_analysis.cpp EXTRA_LIBS webgpu)
add_vivid_operator(bloom                operators/gpu/bloom/bloom.cpp                             EXTRA_LIBS webgpu)
add_vivid_operator(feedback             operators/gpu/feedback/feedback.cpp                       EXTRA_LIBS webgpu)
add_vivid_operator(instance             operators/gpu/instance/instance.cpp                       EXTRA_LIBS webgpu)
add_vivid_operator(time_machine          operators/gpu/time_machine/time_machine.cpp               EXTRA_LIBS webgpu)
add_vivid_operator(plexus                operators/gpu/plexus/plexus.cpp                           EXTRA_LIBS webgpu)
# Simple WGSL filters are loaded from filters/*.wgsl (no C++ needed)

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

# --- Webcam In (live camera capture) ---
if(APPLE)
    add_library(webcam_in MODULE
        operators/gpu/webcam_in/webcam_in.cpp
        operators/gpu/webcam_in/avf_capture.mm
    )
    target_link_libraries(webcam_in PRIVATE
        vivid_operator_api webgpu
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
        "-framework Foundation"
    )
    set_source_files_properties(operators/gpu/webcam_in/avf_capture.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    add_library(webcam_in MODULE operators/gpu/webcam_in/webcam_in.cpp)
    target_link_libraries(webcam_in PRIVATE vivid_operator_api webgpu)
endif()
set_target_properties(webcam_in PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")

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
add_vivid_operator(plexus_synth   operators/audio/plexus_synth/plexus_synth.cpp)

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
    src/ui/theme_loader.cpp
)
if(APPLE)
    target_sources(vivid_ui PRIVATE src/ui/file_dialog.mm)
    set_source_files_properties(src/ui/file_dialog.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
target_include_directories(vivid_ui PRIVATE src)
target_link_libraries(vivid_ui PUBLIC vivid_operator_api webgpu)
target_link_libraries(vivid_ui PRIVATE stb_truetype glfw yyjson)

# --- Vivid executable ---
add_executable(vivid
    src/runtime/main.cpp
    src/runtime/gpu_context.cpp
    src/runtime/fullscreen_blit.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
    src/runtime/file_watcher.cpp
    src/runtime/hot_reload.cpp
    src/runtime/runtime_api.cpp
    src/runtime/screenshot.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/control_server.cpp
    src/runtime/capture_coordinator.cpp
    src/runtime/av_exporter.mm
    src/runtime/operator_creator.cpp
    src/runtime/system_midi.cpp
    src/runtime/platform.cpp
    src/runtime/settings.cpp
    src/runtime/editor_detect.cpp
    src/runtime/macos_frame_timer.cpp
)

target_include_directories(vivid PRIVATE src)
target_link_libraries(vivid PRIVATE vivid_ui webgpu glfw glfw3webgpu vivid_operator_api yyjson miniaudio stb_truetype ixwebsocket rtmidi CLI11::CLI11)

# ObjC ARC for AVExporter
set_source_files_properties(src/runtime/av_exporter.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

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
configure_file(graphs/ramp_demo.json ${CMAKE_BINARY_DIR}/ramp_demo.json COPYONLY)
configure_file(graphs/dither_demo.json ${CMAKE_BINARY_DIR}/dither_demo.json COPYONLY)
configure_file(graphs/transform_demo.json ${CMAKE_BINARY_DIR}/transform_demo.json COPYONLY)
configure_file(graphs/crt_effect_demo.json ${CMAKE_BINARY_DIR}/crt_effect_demo.json COPYONLY)
configure_file(graphs/switch_demo.json ${CMAKE_BINARY_DIR}/switch_demo.json COPYONLY)
configure_file(graphs/feedback_demo.json ${CMAKE_BINARY_DIR}/feedback_demo.json COPYONLY)
configure_file(graphs/shape_instance_demo.json ${CMAKE_BINARY_DIR}/shape_instance_demo.json COPYONLY)
configure_file(graphs/wgsl_filters_demo.json ${CMAKE_BINARY_DIR}/wgsl_filters_demo.json COPYONLY)
configure_file(graphs/pattern_algebra_demo.json ${CMAKE_BINARY_DIR}/pattern_algebra_demo.json COPYONLY)
configure_file(graphs/webcam_timemachine_demo.json ${CMAKE_BINARY_DIR}/webcam_timemachine_demo.json COPYONLY)
configure_file(graphs/plexus_demo.json ${CMAKE_BINARY_DIR}/plexus_demo.json COPYONLY)

# Copy all demo graphs into graphs/ subdirectory (for test_demo_graphs)
file(GLOB DEMO_GRAPHS graphs/*.json)
foreach(g ${DEMO_GRAPHS})
    get_filename_component(name ${g} NAME)
    configure_file(${g} ${CMAKE_BINARY_DIR}/graphs/${name} COPYONLY)
endforeach()

# Copy font to build directory
configure_file(fonts/JetBrainsMono-Regular.ttf ${CMAKE_BINARY_DIR}/JetBrainsMono-Regular.ttf COPYONLY)

# Copy self-describing .wgsl filter presets to build directory
file(GLOB WGSL_PRESETS filters/*.wgsl)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/filters)
foreach(f ${WGSL_PRESETS})
    get_filename_component(fname ${f} NAME)
    configure_file(${f} ${CMAKE_BINARY_DIR}/filters/${fname} COPYONLY)
endforeach()

# Copy Dawn shared library next to the executable (if dynamically linked)
target_copy_webgpu_binaries(vivid)

# Apple framework fallback (if not already linked by glfw3webgpu)
if(APPLE)
    target_link_libraries(vivid PRIVATE
        "-framework Cocoa"
        "-framework CoreVideo"
        "-framework IOKit"
        "-framework QuartzCore"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework VideoToolbox"
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
    src/runtime/wgsl_header_parser.cpp
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
    src/runtime/wgsl_header_parser.cpp
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

# StateMachine operator unit test (includes operator source directly, no plugin loading)
add_executable(test_state_machine tests/test_state_machine.cpp)
target_include_directories(test_state_machine PRIVATE src)
target_link_libraries(test_state_machine PRIVATE vivid_operator_api)
add_test(NAME test_state_machine COMMAND test_state_machine WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# ControlServer integration test (HTTP endpoints, no GPU, no audio, no window)
add_executable(test_control_server
    tests/test_control_server.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
    src/runtime/runtime_api.cpp
    src/runtime/control_server.cpp
    src/runtime/capture_coordinator.cpp
    src/runtime/screenshot.cpp
    src/runtime/av_exporter.mm
    src/runtime/operator_creator.cpp
    src/runtime/hot_reload.cpp
    src/runtime/system_midi.cpp
)
target_include_directories(test_control_server PRIVATE src)
target_link_libraries(test_control_server PRIVATE
    vivid_operator_api yyjson miniaudio ixwebsocket webgpu rtmidi stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation")
add_dependencies(test_control_server test_op_v1)
add_test(NAME test_control_server COMMAND test_control_server WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Test operator plugins
add_vivid_operator(audio_test_op      tests/operators/audio_test_op.cpp)
add_vivid_operator(audio_throwing_op  tests/operators/audio_throwing_op.cpp)
add_vivid_operator(control_pass_op    tests/operators/control_pass_op.cpp)
add_vivid_operator(spread_source_op   tests/operators/spread_source_op.cpp)
add_vivid_operator(spread_sink_op     tests/operators/spread_sink_op.cpp)
add_vivid_operator(audio_spread_op    tests/operators/audio_spread_op.cpp)

# AudioEngine integration test (null audio backend, no GPU, no window)
add_executable(test_audio_engine
    tests/test_audio_engine.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
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

# Audio thread robustness test (exception handling in audio callback)
add_executable(test_audio_robustness
    tests/test_audio_robustness.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
)
target_include_directories(test_audio_robustness PRIVATE src)
target_link_libraries(test_audio_robustness PRIVATE
    vivid_operator_api yyjson miniaudio webgpu)
add_dependencies(test_audio_robustness audio_test_op audio_throwing_op)
add_test(NAME test_audio_robustness COMMAND test_audio_robustness WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Scheduler unit test (no GPU, no audio playback, no window)
add_executable(test_scheduler
    tests/test_scheduler.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
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

# Topological sort unit test (header-only, no dependencies)
add_executable(test_topo_sort tests/test_topo_sort.cpp)
target_include_directories(test_topo_sort PRIVATE src tests)
add_test(NAME test_topo_sort COMMAND test_topo_sort WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# OperatorCreator unit test
add_executable(test_operator_creator
    tests/test_operator_creator.cpp
    src/runtime/operator_creator.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/operator_loader.cpp
    src/runtime/graph.cpp
)
target_include_directories(test_operator_creator PRIVATE src tests)
target_link_libraries(test_operator_creator PRIVATE vivid_operator_api yyjson webgpu)
add_test(NAME test_operator_creator COMMAND test_operator_creator WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Settings round-trip unit test
add_executable(test_settings
    tests/test_settings.cpp
    src/runtime/settings.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_settings PRIVATE src tests)
target_link_libraries(test_settings PRIVATE yyjson)
add_test(NAME test_settings COMMAND test_settings WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Theme loader unit test
add_executable(test_theme_loader
    tests/test_theme_loader.cpp
    src/ui/theme_loader.cpp
    src/ui/ui_style.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_theme_loader PRIVATE src tests)
target_link_libraries(test_theme_loader PRIVATE yyjson)
add_test(NAME test_theme_loader COMMAND test_theme_loader WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# WGSL header parser unit test
add_executable(test_wgsl_header
    tests/test_wgsl_header.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_wgsl_header PRIVATE src)
target_link_libraries(test_wgsl_header PRIVATE yyjson)
add_test(NAME test_wgsl_header COMMAND test_wgsl_header WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# String utility unit test (header-only, no dependencies)
add_executable(test_string_util tests/test_string_util.cpp)
target_include_directories(test_string_util PRIVATE src tests)
add_test(NAME test_string_util COMMAND test_string_util WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# OperatorLoader + OperatorRegistry unit tests (no GPU, no scheduler, no graph)
add_executable(test_operator_loader
    tests/test_operator_loader.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_operator_loader PRIVATE src)
target_link_libraries(test_operator_loader PRIVATE vivid_operator_api webgpu yyjson)
add_dependencies(test_operator_loader test_op_v1 test_op_v2 control_pass_op)
add_test(NAME test_operator_loader COMMAND test_operator_loader WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# GPU operator plugin for testing (solid color fill)
add_vivid_operator(gpu_fill_op      tests/operators/gpu_fill_op.cpp      EXTRA_LIBS webgpu)

# GPU operator integration tests (headless WebGPU, no window)
add_executable(test_gpu_operators
    tests/test_gpu_operators.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
)
target_include_directories(test_gpu_operators PRIVATE src)
target_link_libraries(test_gpu_operators PRIVATE vivid_operator_api yyjson webgpu)
add_dependencies(test_gpu_operators gpu_fill_op shape instance)
add_test(NAME test_gpu_operators COMMAND test_gpu_operators WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Cross-domain spread integration test
add_executable(test_cross_domain_spread
    tests/test_cross_domain_spread.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
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

# Spread broadcast/wrap test
add_executable(test_spread_broadcast
    tests/test_spread_broadcast.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
)
target_include_directories(test_spread_broadcast PRIVATE src)
target_link_libraries(test_spread_broadcast PRIVATE
    vivid_operator_api yyjson webgpu)
add_dependencies(test_spread_broadcast spread_source_op spread_sink_op)
add_test(NAME test_spread_broadcast COMMAND test_spread_broadcast WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Audio spread wire integration test
add_executable(test_audio_spread_wire
    tests/test_audio_spread_wire.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
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

# Wavetable position envelope test
add_executable(test_wavetable_position_env
    tests/test_wavetable_position_env.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
)
target_include_directories(test_wavetable_position_env PRIVATE src)
target_link_libraries(test_wavetable_position_env PRIVATE
    vivid_operator_api yyjson miniaudio webgpu)
add_dependencies(test_wavetable_position_env
    clock note_pattern wavetable_synth spread_adsr spread_source_op)
add_test(NAME test_wavetable_position_env
    COMMAND test_wavetable_position_env ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Pattern algebra unit + integration test
add_executable(test_pattern_algebra
    tests/test_pattern_algebra.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
)
target_include_directories(test_pattern_algebra PRIVATE src)
target_link_libraries(test_pattern_algebra PRIVATE vivid_operator_api yyjson webgpu)
add_dependencies(test_pattern_algebra
    euclidean pattern_seq stack alternate pat_transform
    spread_source_op clock)
add_test(NAME test_pattern_algebra COMMAND test_pattern_algebra
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# ChildOp<T> composition unit test (no plugins, no scheduler, header-only)
add_executable(test_child_op tests/test_child_op.cpp)
target_include_directories(test_child_op PRIVATE src tests ${CMAKE_SOURCE_DIR}/operators)
target_link_libraries(test_child_op PRIVATE vivid_operator_api)
add_test(NAME test_child_op COMMAND test_child_op WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Copy test graphs to build directory
configure_file(tests/graphs/test_reload.json ${CMAKE_BINARY_DIR}/test_reload.json COPYONLY)
configure_file(tests/graphs/test_runtime_api.json ${CMAKE_BINARY_DIR}/test_runtime_api.json COPYONLY)
configure_file(tests/graphs/test_audio_engine.json ${CMAKE_BINARY_DIR}/test_audio_engine.json COPYONLY)
configure_file(tests/graphs/test_audio_robustness.json ${CMAKE_BINARY_DIR}/test_audio_robustness.json COPYONLY)
configure_file(tests/graphs/test_spread_broadcast.json ${CMAKE_BINARY_DIR}/test_spread_broadcast.json COPYONLY)
configure_file(tests/graphs/test_cross_domain_spread.json ${CMAKE_BINARY_DIR}/test_cross_domain_spread.json COPYONLY)
configure_file(tests/graphs/test_audio_spread_wire.json ${CMAKE_BINARY_DIR}/test_audio_spread_wire.json COPYONLY)
configure_file(tests/graphs/test_wavetable_position_env.json ${CMAKE_BINARY_DIR}/test_wavetable_position_env.json COPYONLY)

# Demo graph smoke test (loads every demo graph, builds scheduler, ticks 5 frames)
add_executable(test_demo_graphs
    tests/test_demo_graphs.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
    src/runtime/builtin_operators.cpp
)
target_include_directories(test_demo_graphs PRIVATE src)
target_link_libraries(test_demo_graphs PRIVATE
    vivid_operator_api yyjson miniaudio webgpu rtmidi)
add_dependencies(test_demo_graphs
    # control
    lfo clock math envelope midi_input fft_analysis
    note_pattern chord_progression arpeggiator
    logic gate random smooth sequencer
    euclidean pattern_seq stack alternate pat_transform
    modulated_gain
    # gpu (compiled plugins — WGSL filter presets are loaded from filters/)
    noise shape bars composite
    bloom feedback instance plexus movie_file_in
    # audio
    oscillator gain drum_kick drum_snare drum_hihat drum_clap
    drum_tom drum_cymbal wavetable_synth spread_adsr spread_lfo
    reverb delay bitcrush distortion plexus_synth stutter tape_stop
    beat_repeat reverse scratch stretch freq_shift glitch
    movie_file_audio_in
    webcam_in time_machine
)
add_test(NAME test_demo_graphs
    COMMAND test_demo_graphs ${CMAKE_BINARY_DIR}/graphs
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
