cmake_minimum_required(VERSION 3.20)
project(vivid VERSION 0.1.0 LANGUAGES C CXX)

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
target_include_directories(vivid_operator_api INTERFACE src deps/glfw/deps)

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

# --- Operator manifest (for standalone export builds) ---
define_property(GLOBAL PROPERTY VIVID_OPERATOR_MANIFEST
    BRIEF_DOCS "JSON fragments for operator_manifest.json"
    FULL_DOCS  "Accumulated per-operator metadata for standalone export builds")
set_property(GLOBAL PROPERTY VIVID_OPERATOR_MANIFEST "")

# --- Operator target list (for bundle plugin copying) ---
define_property(GLOBAL PROPERTY VIVID_OPERATOR_TARGETS
    BRIEF_DOCS "All operator plugin targets"
    FULL_DOCS  "Accumulated operator target names for bundle plugin copying")
set_property(GLOBAL PROPERTY VIVID_OPERATOR_TARGETS "")

# --- Operator plugin helper ---
function(add_vivid_operator name source)
    cmake_parse_arguments(ARG "" "FACTORY_PRESETS" "EXTRA_LIBS" ${ARGN})
    add_library(${name} MODULE ${source})
    set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
    target_link_libraries(${name} PRIVATE vivid_operator_api ${ARG_EXTRA_LIBS})
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS ${name})

    # Keep app bundle in sync when building individual operator targets
    if(APPLE)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${name}>
                $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
            COMMENT "Updating ${name} in Vivid.app bundle"
        )
    endif()

    # Copy factory presets JSON to build directory
    if(ARG_FACTORY_PRESETS)
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/factory_presets)
        configure_file(${ARG_FACTORY_PRESETS}
            ${CMAKE_BINARY_DIR}/factory_presets/${name}.json COPYONLY)
    endif()

    # Accumulate manifest entry
    # Filter out non-library targets from EXTRA_LIBS (keep only webgpu, rtmidi, etc.)
    set(_extra_libs "")
    foreach(_lib ${ARG_EXTRA_LIBS})
        # Skip interface/header-only targets that aren't real link deps
        if(NOT "${_lib}" STREQUAL "vivid_composable_ops")
            list(APPEND _extra_libs "${_lib}")
        endif()
    endforeach()
    string(REPLACE ";" "\", \"" _extra_json "${_extra_libs}")
    if(_extra_libs)
        set(_extra_json "\"${_extra_json}\"")
    endif()
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"${name}\": { \"sources\": [\"${source}\"], \"extra_libs\": [${_extra_json}] }")
endfunction()

# --- Control operator plugins ---
add_vivid_operator(lfo            operators/control/lfo/lfo.cpp
                   FACTORY_PRESETS operators/control/lfo/factory_presets.json)
add_vivid_operator(clock          operators/control/clock/clock.cpp)
add_vivid_operator(math           operators/control/math/math.cpp)
add_vivid_operator(envelope       operators/control/envelope/envelope.cpp
                   FACTORY_PRESETS operators/control/envelope/factory_presets.json)
add_vivid_operator(midi_input     operators/control/midi_input/midi_input.cpp     EXTRA_LIBS rtmidi)
add_vivid_operator(fft_analysis   operators/control/fft_analysis/fft_analysis.cpp)
add_vivid_operator(logic             operators/control/logic/logic.cpp)
add_vivid_operator(gate              operators/control/gate/gate.cpp)
add_vivid_operator(random            operators/control/random/random.cpp)
add_vivid_operator(smooth            operators/control/smooth/smooth.cpp)
add_vivid_operator(euclidean         operators/control/euclidean/euclidean.cpp)
add_vivid_operator(stack             operators/control/stack/stack.cpp)
add_vivid_operator(alternate         operators/control/alternate/alternate.cpp)
add_vivid_operator(pat_transform     operators/control/pat_transform/pat_transform.cpp)
add_vivid_operator(modulated_gain   operators/control/modulated_gain/modulated_gain.cpp
                                    EXTRA_LIBS vivid_composable_ops)
add_vivid_operator(spread_noise    operators/control/spread_noise/spread_noise.cpp)

# --- GPU operator plugins (complex operators with custom pipelines) ---
add_vivid_operator(noise          operators/gpu/noise/noise.cpp          EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/noise/factory_presets.json)
add_vivid_operator(shape          operators/gpu/shape/shape.cpp          EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/shape/factory_presets.json)
add_vivid_operator(bars           operators/gpu/bars/bars.cpp            EXTRA_LIBS webgpu)
add_vivid_operator(composite      operators/gpu/composite/composite.cpp  EXTRA_LIBS webgpu)
add_vivid_operator(texture_analysis operators/gpu/texture_analysis/texture_analysis.cpp EXTRA_LIBS webgpu)
add_vivid_operator(bloom                operators/gpu/bloom/bloom.cpp                             EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/bloom/factory_presets.json)
add_vivid_operator(feedback             operators/gpu/feedback/feedback.cpp                       EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/feedback/factory_presets.json)
add_vivid_operator(instance             operators/gpu/instance/instance.cpp                       EXTRA_LIBS webgpu)
add_vivid_operator(time_machine          operators/gpu/time_machine/time_machine.cpp               EXTRA_LIBS webgpu)
add_vivid_operator(text                  operators/gpu/text/text.cpp                               EXTRA_LIBS webgpu stb_truetype)
# 3D operators are available via the vivid-3d package (see ../vivid-3d/)
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
if(APPLE)
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"movie_file_in\": { \"sources\": [\"operators/gpu/movie_file_in/movie_file_in.cpp\", \"operators/gpu/movie_file_in/avf_decoder.mm\"], \"extra_libs\": [\"webgpu\"], \"frameworks\": [\"AVFoundation\", \"CoreMedia\", \"CoreVideo\", \"Foundation\"], \"objc_arc\": [\"operators/gpu/movie_file_in/avf_decoder.mm\"], \"include_dirs\": [\"deps/stb\"] }")
else()
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"movie_file_in\": { \"sources\": [\"operators/gpu/movie_file_in/movie_file_in.cpp\"], \"extra_libs\": [\"webgpu\"], \"include_dirs\": [\"deps/stb\"] }")
endif()
set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS movie_file_in)

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
if(APPLE)
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"webcam_in\": { \"sources\": [\"operators/gpu/webcam_in/webcam_in.cpp\", \"operators/gpu/webcam_in/avf_capture.mm\"], \"extra_libs\": [\"webgpu\"], \"frameworks\": [\"AVFoundation\", \"CoreMedia\", \"CoreVideo\", \"Foundation\"], \"objc_arc\": [\"operators/gpu/webcam_in/avf_capture.mm\"] }")
else()
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"webcam_in\": { \"sources\": [\"operators/gpu/webcam_in/webcam_in.cpp\"], \"extra_libs\": [\"webgpu\"] }")
endif()
set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS webcam_in)

# --- Audio operator plugins ---
add_vivid_operator(oscillator     operators/audio/oscillator/oscillator.cpp)
add_vivid_operator(gain           operators/audio/gain/gain.cpp)
add_vivid_operator(spread_adsr    operators/audio/spread_adsr/spread_adsr.cpp)
add_vivid_operator(spread_lfo     operators/audio/spread_lfo/spread_lfo.cpp)
add_vivid_operator(reverb         operators/audio/reverb/reverb.cpp
                   FACTORY_PRESETS operators/audio/reverb/factory_presets.json)
add_vivid_operator(delay          operators/audio/delay/delay.cpp
                   FACTORY_PRESETS operators/audio/delay/factory_presets.json)
add_vivid_operator(bitcrush       operators/audio/bitcrush/bitcrush.cpp
                   FACTORY_PRESETS operators/audio/bitcrush/factory_presets.json)
add_vivid_operator(distortion     operators/audio/distortion/distortion.cpp
                   FACTORY_PRESETS operators/audio/distortion/factory_presets.json)

# --- Movie File Audio In (audio extraction from video files) ---
if(APPLE)
    add_library(movie_file_audio_in MODULE
        operators/audio/movie_file_audio_in/movie_file_audio_in.cpp
        operators/audio/movie_file_audio_in/avf_audio_extractor.mm
    )
    target_link_libraries(movie_file_audio_in PRIVATE
        vivid_operator_api
        "-framework AVFoundation" "-framework AVFAudio" "-framework CoreMedia" "-framework Foundation"
    )
    set_source_files_properties(
        operators/audio/movie_file_audio_in/avf_audio_extractor.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_target_properties(movie_file_audio_in PROPERTIES
        PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"movie_file_audio_in\": { \"sources\": [\"operators/audio/movie_file_audio_in/movie_file_audio_in.cpp\", \"operators/audio/movie_file_audio_in/avf_audio_extractor.mm\"], \"extra_libs\": [], \"frameworks\": [\"AVFoundation\", \"AVFAudio\", \"CoreMedia\", \"Foundation\"], \"objc_arc\": [\"operators/audio/movie_file_audio_in/avf_audio_extractor.mm\"] }")
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS movie_file_audio_in)
endif()

# --- Operators meta-target (for package smoke CI: builds all operator dylibs without the app) ---
get_property(_vivid_op_targets GLOBAL PROPERTY VIVID_OPERATOR_TARGETS)
add_custom_target(operators DEPENDS ${_vivid_op_targets})

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
    src/runtime/undo_manager.cpp
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
    src/export/export_pipeline.cpp
    src/runtime/package_compiler.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_test_runner.cpp
    src/runtime/package_catalog.cpp
)

if(APPLE)
    set_target_properties(vivid PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Vivid"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.vivid.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/macos/Info.plist.in"
    )
endif()

target_include_directories(vivid PRIVATE src)
target_link_libraries(vivid PRIVATE vivid_ui webgpu glfw glfw3webgpu vivid_operator_api yyjson miniaudio stb_truetype ixwebsocket rtmidi CLI11::CLI11)

if(APPLE)
    target_compile_definitions(vivid PRIVATE
        "VIVID_BUILD_DIR=\"${CMAKE_BINARY_DIR}\""
        "VIVID_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
endif()

# macOS native menu bar
if(APPLE)
    target_sources(vivid PRIVATE src/runtime/macos_menu.mm)
    set_source_files_properties(src/runtime/macos_menu.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

# ObjC ARC for AVExporter
set_source_files_properties(src/runtime/av_exporter.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

# Copy demo graphs to build directory
configure_file(graphs/demo.json ${CMAKE_BINARY_DIR}/graph.json COPYONLY)
configure_file(graphs/audio_demo.json ${CMAKE_BINARY_DIR}/audio_demo.json COPYONLY)
configure_file(graphs/midi_demo.json ${CMAKE_BINARY_DIR}/midi_demo.json COPYONLY)
configure_file(graphs/fft_bars_demo.json ${CMAKE_BINARY_DIR}/fft_bars_demo.json COPYONLY)
configure_file(graphs/displace_demo.json ${CMAKE_BINARY_DIR}/displace_demo.json COPYONLY)
configure_file(graphs/movie_file_in_demo.json ${CMAKE_BINARY_DIR}/movie_file_in_demo.json COPYONLY)
configure_file(graphs/mfi_halftone_demo.json ${CMAKE_BINARY_DIR}/mfi_halftone_demo.json COPYONLY)
configure_file(graphs/mfi_posterize_demo.json ${CMAKE_BINARY_DIR}/mfi_posterize_demo.json COPYONLY)
configure_file(graphs/mfi_blur_demo.json ${CMAKE_BINARY_DIR}/mfi_blur_demo.json COPYONLY)
configure_file(graphs/mfi_displace_demo.json ${CMAKE_BINARY_DIR}/mfi_displace_demo.json COPYONLY)
configure_file(graphs/mfi_composite_demo.json ${CMAKE_BINARY_DIR}/mfi_composite_demo.json COPYONLY)
configure_file(graphs/mfi_kitchen_sink_demo.json ${CMAKE_BINARY_DIR}/mfi_kitchen_sink_demo.json COPYONLY)
configure_file(graphs/mfi_av_sync_demo.json ${CMAKE_BINARY_DIR}/mfi_av_sync_demo.json COPYONLY)
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
configure_file(graphs/wgsl_filters_demo.json ${CMAKE_BINARY_DIR}/wgsl_filters_demo.json COPYONLY)
configure_file(graphs/webcam_timemachine_demo.json ${CMAKE_BINARY_DIR}/webcam_timemachine_demo.json COPYONLY)

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

# --- Bundle resources (font, graphs, WGSL filters → Resources/) ---
if(APPLE)
    # Font → Resources/
    target_sources(vivid PRIVATE fonts/JetBrainsMono-Regular.ttf)
    set_source_files_properties(fonts/JetBrainsMono-Regular.ttf
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Demo graphs → Resources/
    file(GLOB _BUNDLE_GRAPHS graphs/*.json)
    target_sources(vivid PRIVATE ${_BUNDLE_GRAPHS})
    set_source_files_properties(${_BUNDLE_GRAPHS}
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Icon → Resources/
    target_sources(vivid PRIVATE platform/macos/Vivid.icns)
    set_source_files_properties(platform/macos/Vivid.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(vivid PROPERTIES
        MACOSX_BUNDLE_ICON_FILE Vivid.icns)

    # WGSL filter presets → Resources/filters/
    file(GLOB _BUNDLE_FILTERS filters/*.wgsl)
    foreach(f ${_BUNDLE_FILTERS})
        target_sources(vivid PRIVATE ${f})
        set_source_files_properties(${f}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/filters)
    endforeach()

    # Factory presets → Resources/factory_presets/
    file(GLOB _BUNDLE_FACTORY_PRESETS ${CMAKE_BINARY_DIR}/factory_presets/*.json)
    foreach(f ${_BUNDLE_FACTORY_PRESETS})
        target_sources(vivid PRIVATE ${f})
        set_source_files_properties(${f}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/factory_presets)
    endforeach()

    # operator_api headers → Resources/sdk/src/operator_api/
    file(GLOB _BUNDLE_API_HEADERS src/operator_api/*.h)
    foreach(f ${_BUNDLE_API_HEADERS})
        target_sources(vivid PRIVATE ${f})
        set_source_files_properties(${f}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/sdk/src/operator_api)
    endforeach()

    # WebGPU header → Resources/sdk/include/webgpu/
    # Derive wgpu root from WEBGPU_RUNTIME_LIB (cache variable set by FetchContent)
    get_filename_component(_wgpu_lib_dir "${WEBGPU_RUNTIME_LIB}" DIRECTORY)
    get_filename_component(_wgpu_root "${_wgpu_lib_dir}" DIRECTORY)
    set(_wgpu_header "${_wgpu_root}/include/webgpu/webgpu.h")
    if(EXISTS "${_wgpu_header}")
        target_sources(vivid PRIVATE "${_wgpu_header}")
        set_source_files_properties("${_wgpu_header}"
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/sdk/include/webgpu)
    endif()
endif()

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

# --- Generate operator manifest for standalone export ---
get_property(_manifest_entries GLOBAL PROPERTY VIVID_OPERATOR_MANIFEST)
list(JOIN _manifest_entries ",\n" _manifest_body)
file(WRITE "${CMAKE_BINARY_DIR}/operator_manifest.json" "{\n${_manifest_body}\n}\n")

# --- Copy operator plugins into bundle ---
if(APPLE)
    get_property(_op_targets GLOBAL PROPERTY VIVID_OPERATOR_TARGETS)

    # Ensure operator plugins are built before vivid's POST_BUILD copies them
    add_dependencies(vivid ${_op_targets})

    # Create PlugIns directory
    add_custom_command(TARGET vivid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
    )

    foreach(op_target IN LISTS _op_targets)
        add_custom_command(TARGET vivid POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${op_target}>
                $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        )
    endforeach()

    # Ad-hoc sign the bundle so Info.plist is bound to the code signature.
    # Without this, macOS TCC won't read NSCameraUsageDescription and camera
    # authorization throws an uncaught NSException.
    # Remove runtime artifacts that confuse codesign's bundle validation.
    add_custom_command(TARGET vivid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_BUNDLE_CONTENT_DIR:vivid>/MacOS/.hot_reload
        COMMAND codesign -s - --force $<TARGET_BUNDLE_DIR:vivid>
        COMMENT "Ad-hoc signing Vivid.app bundle"
    )

    # Convenience symlink so ./build/vivid still works (canonical() resolves it)
    add_custom_command(TARGET vivid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_BINARY_DIR}/vivid
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/MacOS/vivid
            ${CMAKE_BINARY_DIR}/vivid
        COMMENT "Creating convenience symlink build/vivid"
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
    src/runtime/undo_manager.cpp
    src/runtime/control_server.cpp
    src/runtime/capture_coordinator.cpp
    src/runtime/screenshot.cpp
    src/runtime/av_exporter.mm
    src/runtime/operator_creator.cpp
    src/runtime/hot_reload.cpp
    src/runtime/system_midi.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_compiler.cpp
    src/runtime/package_test_runner.cpp
    src/runtime/package_catalog.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_control_server PRIVATE src)
target_link_libraries(test_control_server PRIVATE
    vivid_operator_api yyjson miniaudio ixwebsocket webgpu rtmidi stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
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

# audio_dsp operator API compatibility test
add_executable(test_audio_dsp_api tests/test_audio_dsp_api.cpp)
target_include_directories(test_audio_dsp_api PRIVATE src)
add_test(NAME test_audio_dsp_api COMMAND test_audio_dsp_api WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

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

# 3D tests are in the vivid-3d package (see ../vivid-3d/)

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
    logic gate random smooth
    euclidean stack alternate pat_transform
    modulated_gain spread_noise
    # gpu (compiled plugins — WGSL filter presets are loaded from filters/)
    noise shape bars composite
    bloom feedback instance movie_file_in texture_analysis
    # audio
    oscillator gain spread_adsr spread_lfo
    reverb delay bitcrush distortion
    movie_file_audio_in
    webcam_in time_machine
)
add_test(NAME test_demo_graphs
    COMMAND test_demo_graphs ${CMAKE_BINARY_DIR}/graphs
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_demo_graphs PROPERTIES LABELS "SMOKE")

# PackageCompiler unit test (compiles a mock package operator)
add_executable(test_package_compiler
    tests/test_package_compiler.cpp
    src/runtime/package_compiler.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_compiler PRIVATE src)
target_link_libraries(test_package_compiler PRIVATE vivid_operator_api yyjson webgpu)
add_test(NAME test_package_compiler
    COMMAND test_package_compiler ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# PackageCatalog unit test (JSON parsing, cache, merge)
add_executable(test_package_catalog
    tests/test_package_catalog.cpp
    src/runtime/package_catalog.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_compiler.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_catalog PRIVATE src)
target_link_libraries(test_package_catalog PRIVATE vivid_operator_api yyjson webgpu)
add_test(NAME test_package_catalog
    COMMAND test_package_catalog
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# PackageManager lifecycle test (install/list/uninstall with local path)
add_executable(test_package_manager
    tests/test_package_manager.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_compiler.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_manager PRIVATE src)
target_link_libraries(test_package_manager PRIVATE vivid_operator_api yyjson webgpu)
add_test(NAME test_package_manager
    COMMAND test_package_manager ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# PackageTestRunner test (graph + C++ test execution for packages)
add_executable(test_package_test_runner
    tests/test_package_test_runner.cpp
    src/runtime/package_test_runner.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_compiler.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_test_runner PRIVATE src)
target_link_libraries(test_package_test_runner PRIVATE
    vivid_operator_api yyjson webgpu)
add_dependencies(test_package_test_runner control_pass_op)
add_test(NAME test_package_test_runner
    COMMAND test_package_test_runner ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# CaptureCoordinator unit test (non-GPU paths only)
add_executable(test_capture_coordinator
    tests/test_capture_coordinator.cpp
    src/runtime/capture_coordinator.cpp
    src/runtime/av_exporter.mm
    src/runtime/audio_engine.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/screenshot.cpp
)
target_include_directories(test_capture_coordinator PRIVATE src)
target_link_libraries(test_capture_coordinator PRIVATE
    vivid_operator_api yyjson webgpu miniaudio stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
add_test(NAME test_capture_coordinator COMMAND test_capture_coordinator
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# SystemMidiListener unit test (no hardware needed)
add_executable(test_midi
    tests/test_midi.cpp
    src/runtime/system_midi.cpp
)
target_include_directories(test_midi PRIVATE src)
target_link_libraries(test_midi PRIVATE rtmidi)
add_test(NAME test_midi COMMAND test_midi WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# FileWatcher unit tests (kqueue, no scheduler, no GPU)
add_executable(test_file_watcher
    tests/test_file_watcher.cpp
    src/runtime/file_watcher.cpp
)
target_include_directories(test_file_watcher PRIVATE src tests)
add_test(NAME test_file_watcher COMMAND test_file_watcher WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# path_util unit tests (header-only, no extra deps)
add_executable(test_path_util
    tests/test_path_util.cpp
)
target_include_directories(test_path_util PRIVATE src tests)
add_test(NAME test_path_util COMMAND test_path_util WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# UndoManager unit tests (snapshot history behavior)
add_executable(test_undo_manager
    tests/test_undo_manager.cpp
    src/runtime/undo_manager.cpp
)
target_include_directories(test_undo_manager PRIVATE src tests)
add_test(NAME test_undo_manager COMMAND test_undo_manager WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# editor_detect unit tests (filesystem scan, no display)
add_executable(test_editor_detect
    tests/test_editor_detect.cpp
    src/runtime/editor_detect.cpp
)
target_include_directories(test_editor_detect PRIVATE src tests)
add_test(NAME test_editor_detect COMMAND test_editor_detect WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Undo coverage across roadmap mutation types via RuntimeCommandSink + RuntimeAPI
add_executable(test_undo_mutation_types
    tests/test_undo_mutation_types.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/scheduler.cpp
    src/runtime/audio_engine.cpp
    src/runtime/runtime_api.cpp
    src/runtime/undo_manager.cpp
    src/runtime/operator_creator.cpp
    src/runtime/settings.cpp
    src/runtime/hot_reload.cpp
    src/runtime/capture_coordinator.cpp
    src/runtime/screenshot.cpp
    src/runtime/av_exporter.mm
    src/runtime/platform.cpp
    src/runtime/system_midi.cpp
)
target_include_directories(test_undo_mutation_types PRIVATE src tests)
target_link_libraries(test_undo_mutation_types PRIVATE
    vivid_operator_api yyjson miniaudio webgpu rtmidi stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
add_dependencies(test_undo_mutation_types test_op_v1)
add_test(NAME test_undo_mutation_types
    COMMAND test_undo_mutation_types ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# builtin_operators unit tests (no dylib, no GPU)
add_executable(test_builtin_operators
    tests/test_builtin_operators.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/operator_registry.cpp
    src/runtime/operator_loader.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_builtin_operators PRIVATE src tests)
target_link_libraries(test_builtin_operators PRIVATE vivid_operator_api webgpu yyjson)
add_test(NAME test_builtin_operators COMMAND test_builtin_operators WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# OperatorInfoCache unit tests (uses builtin operators as fixtures, no dylib)
add_executable(test_operator_info_cache
    tests/test_operator_info_cache.cpp
    src/runtime/operator_registry.cpp
    src/runtime/operator_loader.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_operator_info_cache PRIVATE src tests)
target_link_libraries(test_operator_info_cache PRIVATE vivid_operator_api webgpu yyjson)
add_test(NAME test_operator_info_cache COMMAND test_operator_info_cache WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
