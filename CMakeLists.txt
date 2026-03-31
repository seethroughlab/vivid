cmake_minimum_required(VERSION 3.20)
project(vivid VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(VIVID_APPCAST_URL "https://vivid.seethroughlab.com/appcast.xml" CACHE STRING
    "Sparkle appcast URL for core app updates")
set(VIVID_SPARKLE_PUBLIC_KEY "" CACHE STRING
    "Sparkle EdDSA public key (SUPublicEDKey)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Sanitizer instrumentation (opt-in) ---
option(VIVID_SANITIZE "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(VIVID_SANITIZE_THREAD "Enable ThreadSanitizer (mutually exclusive with VIVID_SANITIZE)" OFF)

if(VIVID_SANITIZE AND VIVID_SANITIZE_THREAD)
    message(FATAL_ERROR "Cannot enable both VIVID_SANITIZE and VIVID_SANITIZE_THREAD")
endif()

if(VIVID_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls)
    add_link_options(-fsanitize=address,undefined)
    set(VIVID_USE_WGPU_NATIVE_UPSTREAM OFF CACHE BOOL "" FORCE)
endif()

if(VIVID_SANITIZE_THREAD)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
    set(VIVID_USE_WGPU_NATIVE_UPSTREAM OFF CACHE BOOL "" FORCE)
endif()

# --- Dawn/WebGPU via eliemichel's distribution (pre-built binaries) ---
include(FetchContent)
FetchContent_Declare(
    webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(webgpu)

option(VIVID_USE_WGPU_NATIVE_UPSTREAM "Build upstream wgpu-native from source for Metal interop symbols" ON)
if(APPLE AND VIVID_USE_WGPU_NATIVE_UPSTREAM)
    FetchContent_Declare(
        wgpu_native_upstream
        GIT_REPOSITORY https://github.com/gfx-rs/wgpu-native
        GIT_TAG        1487faea32e3b7fc891fd671323582b487038aea
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_GetProperties(wgpu_native_upstream)
    if(NOT wgpu_native_upstream_POPULATED)
        FetchContent_Populate(wgpu_native_upstream)
    endif()

    find_program(VIVID_CARGO_EXECUTABLE cargo REQUIRED)
    set(VIVID_WGPU_NATIVE_UPSTREAM_LIB
        "${wgpu_native_upstream_SOURCE_DIR}/target/release/libwgpu_native.dylib")

    add_custom_target(wgpu_native_upstream_build ALL
        COMMAND ${VIVID_CARGO_EXECUTABLE} build --release
        WORKING_DIRECTORY ${wgpu_native_upstream_SOURCE_DIR}
        COMMENT "Building upstream wgpu-native from source (Metal interop symbols)"
        VERBATIM
    )

    if(TARGET webgpu)
        add_dependencies(webgpu wgpu_native_upstream_build)
        set_target_properties(webgpu PROPERTIES IMPORTED_LOCATION "${VIVID_WGPU_NATIVE_UPSTREAM_LIB}")
    endif()
    set(WEBGPU_RUNTIME_LIB "${VIVID_WGPU_NATIVE_UPSTREAM_LIB}" CACHE INTERNAL "Path to the WebGPU library binary" FORCE)
endif()

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

# snappy (used by HAP frame decoder)
FetchContent_Declare(
    snappy
    GIT_REPOSITORY https://github.com/google/snappy.git
    GIT_TAG        1.2.1
    GIT_SHALLOW    TRUE
)
set(SNAPPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SNAPPY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SNAPPY_FUZZING_BUILD OFF CACHE BOOL "" FORCE)
set(SNAPPY_REQUIRE_AVX OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(snappy)

# --- nlohmann/json (header-only JSON library) ---
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# --- GLFW (submodule) ---
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/glfw)

# --- glfw3webgpu bridge (submodule) ---
add_subdirectory(deps/glfw3webgpu)

# --- miniaudio (vendored, header-only) ---
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE deps/miniaudio)

# --- stb_truetype (vendored, header-only) ---
add_library(stb_truetype INTERFACE)
target_include_directories(stb_truetype INTERFACE deps/stb)

# --- NanoSVG (header-only SVG parser + rasterizer) ---
add_library(nanosvg INTERFACE)
target_include_directories(nanosvg INTERFACE deps/nanosvg)

# --- RtMidi (vendored, CoreMIDI backend on macOS) ---
add_library(rtmidi STATIC deps/rtmidi/RtMidi.cpp)
target_include_directories(rtmidi PUBLIC deps/rtmidi)
target_compile_definitions(rtmidi PUBLIC __MACOSX_CORE__)
target_link_libraries(rtmidi PUBLIC
    "-framework CoreMIDI"
    "-framework CoreAudio"
    "-framework CoreFoundation"
)

# --- oscpack (vendored OSC over UDP) ---
if(WIN32)
    set(VIVID_OSCPACK_IP_PATH deps/oscpack/ip/win32)
else()
    set(VIVID_OSCPACK_IP_PATH deps/oscpack/ip/posix)
endif()

add_library(oscpack STATIC
    deps/oscpack/ip/IpEndpointName.cpp
    ${VIVID_OSCPACK_IP_PATH}/NetworkingUtils.cpp
    ${VIVID_OSCPACK_IP_PATH}/UdpSocket.cpp
    deps/oscpack/osc/OscTypes.cpp
    deps/oscpack/osc/OscReceivedElements.cpp
    deps/oscpack/osc/OscPrintReceivedElements.cpp
    deps/oscpack/osc/OscOutboundPacketStream.cpp
)
target_include_directories(oscpack PUBLIC deps/oscpack)
if(WIN32)
    target_link_libraries(oscpack PUBLIC ws2_32 winmm)
endif()

# --- Syphon runtime (macOS) ---
if(APPLE)
    add_library(syphon_runtime SHARED
        deps/syphon/SyphonCFMessageReceiver.m
        deps/syphon/SyphonCFMessageSender.m
        deps/syphon/SyphonClientBase.m
        deps/syphon/SyphonClientConnectionManager.m
        deps/syphon/SyphonDispatch.c
        deps/syphon/SyphonMessageQueue.m
        deps/syphon/SyphonMessageReceiver.m
        deps/syphon/SyphonMessageSender.m
        deps/syphon/SyphonMessaging.m
        deps/syphon/SyphonMetalClient.m
        deps/syphon/SyphonMetalServer.m
        deps/syphon/SyphonPrivate.m
        deps/syphon/SyphonServerBase.m
        deps/syphon/SyphonServerConnectionManager.m
        deps/syphon/SyphonServerDirectory.m
        deps/syphon/SyphonServerRendererMetal.m
    )
    target_include_directories(syphon_runtime PUBLIC deps/syphon deps)
    target_link_libraries(syphon_runtime PUBLIC
        "-framework Foundation"
        "-framework AppKit"
        "-framework Metal"
        "-framework IOSurface"
        "-framework CoreVideo"
        "-framework QuartzCore"
        "-framework CoreFoundation"
    )
    target_compile_options(syphon_runtime PRIVATE "-include${CMAKE_SOURCE_DIR}/deps/syphon/Syphon_Prefix.pch")
    set_source_files_properties(
        deps/syphon/SyphonCFMessageReceiver.m
        deps/syphon/SyphonCFMessageSender.m
        deps/syphon/SyphonClientBase.m
        deps/syphon/SyphonClientConnectionManager.m
        deps/syphon/SyphonMessageQueue.m
        deps/syphon/SyphonMessageReceiver.m
        deps/syphon/SyphonMessageSender.m
        deps/syphon/SyphonMessaging.m
        deps/syphon/SyphonMetalClient.m
        deps/syphon/SyphonMetalServer.m
        deps/syphon/SyphonPrivate.m
        deps/syphon/SyphonServerBase.m
        deps/syphon/SyphonServerConnectionManager.m
        deps/syphon/SyphonServerDirectory.m
        deps/syphon/SyphonServerRendererMetal.m
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
endif()

# --- Operator API (header-only interface library) ---
add_library(vivid_operator_api INTERFACE)
target_include_directories(vivid_operator_api INTERFACE src deps/glfw/deps ${CMAKE_SOURCE_DIR}/operators)
# Allow operator dylibs to call back into runtime symbols (e.g. vivid_register_port_type)
# that live in the main executable. -undefined dynamic_lookup defers resolution to load time.
if(APPLE)
    target_link_options(vivid_operator_api INTERFACE -undefined dynamic_lookup)
endif()

# --- Composable operator support (for ChildOp<T> consumers) ---
# Embeddable operators must either be fully header-defined or register a
# composable support file here when they have out-of-line virtuals, thumbnail
# hooks, or other concrete definitions that ChildOp<T> consumers must link.
add_library(vivid_composable_ops STATIC)
target_include_directories(vivid_composable_ops PUBLIC ${CMAKE_SOURCE_DIR}/operators)
target_link_libraries(vivid_composable_ops PUBLIC vivid_operator_api)

function(add_vivid_embeddable_composable_support name source)
    target_sources(vivid_composable_ops PRIVATE ${source})
endfunction()

add_vivid_embeddable_composable_support(Smooth   operators/control/smooth/smooth_composable.cpp)
add_vivid_embeddable_composable_support(Envelope operators/control/envelope/envelope_composable.cpp)

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
add_vivid_operator(clock          operators/control/clock/clock.cpp          EXTRA_LIBS webgpu)
add_vivid_operator(math           operators/control/math/math.cpp)
add_vivid_operator(envelope       operators/control/envelope/envelope.cpp
                   FACTORY_PRESETS operators/control/envelope/factory_presets.json
                   EXTRA_LIBS webgpu)
add_vivid_operator(midi_input     operators/control/midi_input/midi_input.cpp     EXTRA_LIBS rtmidi)
add_vivid_operator(fft_analysis   operators/control/fft_analysis/fft_analysis.cpp)
add_vivid_operator(logic             operators/control/logic/logic.cpp)
add_vivid_operator(gate              operators/control/gate/gate.cpp)
add_vivid_operator(smooth            operators/control/smooth/smooth.cpp            EXTRA_LIBS webgpu)
add_vivid_operator(stack             operators/control/stack/stack.cpp)
add_vivid_operator(alternate         operators/control/alternate/alternate.cpp)
add_vivid_operator(modulated_gain   operators/control/modulated_gain/modulated_gain.cpp
                                    EXTRA_LIBS vivid_composable_ops)
add_vivid_operator(osc_in            operators/control/osc_in/osc_in.cpp      EXTRA_LIBS oscpack)
add_vivid_operator(osc_out           operators/control/osc_out/osc_out.cpp     EXTRA_LIBS oscpack)
add_vivid_operator(mouse             operators/control/mouse/mouse.cpp)
add_vivid_operator(keyboard          operators/control/keyboard/keyboard.cpp)
add_vivid_operator(folder_list       operators/control/folder_list/folder_list.cpp)
add_vivid_operator(string_select     operators/control/string_select/string_select.cpp)
add_vivid_operator(basename          operators/control/basename/basename.cpp)
add_vivid_operator(step_counter      operators/control/step_counter/step_counter.cpp)
add_vivid_operator(path_animate      operators/control/path_animate/path_animate.cpp)
add_vivid_operator(sample_hold       operators/control/sample_hold/sample_hold.cpp)
add_vivid_operator(quantizer         operators/control/quantizer/quantizer.cpp)
add_vivid_operator(macro             operators/control/macro/macro.cpp)
add_vivid_operator(mseg              operators/control/mseg/mseg.cpp
                   FACTORY_PRESETS operators/control/mseg/factory_presets.json)
add_vivid_operator(step_seq          operators/control/step_seq/step_seq.cpp
                   FACTORY_PRESETS operators/control/step_seq/factory_presets.json)

# --- Sequencer operators (from vivid-sequencers) ---
add_vivid_operator(sequencer          operators/control/sequencer/sequencer.cpp)
add_vivid_operator(drum_sequencer     operators/control/drum_sequencer/drum_sequencer.cpp     EXTRA_LIBS webgpu)
add_vivid_operator(pattern_seq        operators/control/pattern_seq/pattern_seq.cpp)
add_vivid_operator(note_pattern       operators/control/note_pattern/note_pattern.cpp       EXTRA_LIBS webgpu)
add_vivid_operator(note_duration      operators/control/note_duration/note_duration.cpp)
add_vivid_operator(arpeggiator        operators/control/arpeggiator/arpeggiator.cpp        EXTRA_LIBS webgpu)
add_vivid_operator(chord_progression  operators/control/chord_progression/chord_progression.cpp  EXTRA_LIBS webgpu)
add_vivid_operator(state_machine      operators/control/state_machine/state_machine.cpp)
add_vivid_operator(tracker            operators/control/tracker/tracker.cpp            EXTRA_LIBS webgpu)
add_vivid_operator(euclidean          operators/control/euclidean/euclidean.cpp)
add_vivid_operator(pat_transform      operators/control/pat_transform/pat_transform.cpp)
add_vivid_operator(phase_to_midi      operators/control/phase_to_midi/phase_to_midi.cpp)
add_vivid_operator(drum_kit            operators/control/drum_kit/drum_kit.cpp)
foreach(_seq_op sequencer drum_sequencer pattern_seq note_pattern note_duration
        arpeggiator chord_progression state_machine tracker euclidean pat_transform phase_to_midi drum_kit)
    target_include_directories(${_seq_op} PRIVATE ${CMAKE_SOURCE_DIR}/operators/shared/sequencer)
endforeach()

# --- GPU operator plugins (complex operators with custom pipelines) ---
add_vivid_operator(noise          operators/gpu/noise/noise.cpp          EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/noise/factory_presets.json)
add_vivid_operator(shape          operators/gpu/shape/shape.cpp          EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/shape/factory_presets.json)
add_vivid_operator(composite      operators/gpu/composite/composite.cpp  EXTRA_LIBS webgpu)
add_vivid_operator(texture_analysis operators/gpu/texture_analysis/texture_analysis.cpp EXTRA_LIBS webgpu)
add_vivid_operator(bloom                operators/gpu/bloom/bloom.cpp                             EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/bloom/factory_presets.json)
add_vivid_operator(feedback             operators/gpu/feedback/feedback.cpp                       EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/feedback/factory_presets.json)
add_vivid_operator(metaball              operators/gpu/metaball/metaball.cpp                       EXTRA_LIBS webgpu)
add_vivid_operator(particles             operators/gpu/particles/particles.cpp                     EXTRA_LIBS webgpu vivid_composable_ops)
add_vivid_operator(instanced_shapes      operators/gpu/instanced_shapes/instanced_shapes.cpp      EXTRA_LIBS webgpu)
add_vivid_operator(flocking              operators/gpu/flocking/flocking.cpp                       EXTRA_LIBS webgpu)
add_vivid_operator(trails                operators/gpu/trails/trails.cpp                           EXTRA_LIBS webgpu)
add_vivid_operator(reaction_diffusion    operators/gpu/reaction_diffusion/reaction_diffusion.cpp   EXTRA_LIBS webgpu)
add_vivid_operator(cellular_automata     operators/gpu/cellular_automata/cellular_automata.cpp     EXTRA_LIBS webgpu)
add_vivid_operator(fluid                 operators/gpu/fluid/fluid.cpp                             EXTRA_LIBS webgpu)
add_vivid_operator(time_machine          operators/gpu/time_machine/time_machine.cpp               EXTRA_LIBS webgpu)
add_vivid_operator(text                  operators/gpu/text/text.cpp                               EXTRA_LIBS webgpu stb_truetype)
add_vivid_operator(mesh_warp             operators/gpu/mesh_warp/mesh_warp.cpp                     EXTRA_LIBS webgpu)
add_vivid_operator(lut_apply             operators/gpu/lut_apply/lut_apply.cpp                     EXTRA_LIBS webgpu)
add_vivid_operator(scopes                operators/gpu/scopes/scopes.cpp                           EXTRA_LIBS webgpu)
add_vivid_operator(svg_render             operators/gpu/svg_render/svg_render.cpp                   EXTRA_LIBS webgpu nanosvg)
add_vivid_operator(rich_text              operators/gpu/rich_text/rich_text.cpp                     EXTRA_LIBS webgpu stb_truetype)
# --- TextureLoader (static image → GPU texture, no video machinery) ---
add_library(texture_loader MODULE
    operators/gpu/texture_loader/texture_loader.cpp
    operators/shared/movie_decode/texture_upload.cpp
)
target_link_libraries(texture_loader PRIVATE vivid_operator_api webgpu)
set_target_properties(texture_loader PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
target_include_directories(texture_loader PRIVATE ${CMAKE_SOURCE_DIR}/deps/stb)
if(APPLE)
    add_custom_command(TARGET texture_loader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:texture_loader>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        COMMENT "Updating texture_loader in Vivid.app bundle"
    )
endif()
set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
    "  \"texture_loader\": { \"sources\": [\"operators/gpu/texture_loader/texture_loader.cpp\", \"operators/shared/movie_decode/texture_upload.cpp\"], \"extra_libs\": [\"webgpu\"], \"include_dirs\": [\"deps/stb\"] }")
set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS texture_loader)
# 3D operators are available via the vivid-3d package (see ../vivid-3d/)
# Simple WGSL filters are loaded from filters/*.wgsl (no C++ needed)

# --- MovieFileIn (cadence-native video file input) ---
if(APPLE)
    add_library(movie_file_in MODULE
        operators/gpu/movie_file_in/movie_file_in.cpp
        operators/shared/movie_decode/decoder_factory.cpp
        operators/shared/movie_decode/texture_upload.cpp
        operators/shared/movie_decode/placeholder_frame.cpp
        operators/shared/movie_decode/avf_decoder.mm
        operators/shared/movie_decode/hap_decoder.mm
        operators/shared/movie_decode/codec_probe.mm
        deps/hap/hap.c
    )
    target_link_libraries(movie_file_in PRIVATE
        vivid_operator_api webgpu snappy
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
        "-framework Foundation" "-framework QuartzCore"
    )
    set_source_files_properties(
        operators/shared/movie_decode/avf_decoder.mm
        operators/shared/movie_decode/hap_decoder.mm
        operators/shared/movie_decode/codec_probe.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    add_library(movie_file_in MODULE operators/gpu/movie_file_in/movie_file_in.cpp)
    target_link_libraries(movie_file_in PRIVATE vivid_operator_api webgpu)
endif()
set_target_properties(movie_file_in PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
if(APPLE)
    add_custom_command(TARGET movie_file_in POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:movie_file_in>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        COMMENT "Updating movie_file_in in Vivid.app bundle"
    )
endif()
target_include_directories(movie_file_in PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/stb
    ${CMAKE_SOURCE_DIR}/deps/hap
)
if(APPLE)
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"movie_file_in\": { \"sources\": [\"operators/gpu/movie_file_in/movie_file_in.cpp\", \"operators/shared/movie_decode/decoder_factory.cpp\", \"operators/shared/movie_decode/texture_upload.cpp\", \"operators/shared/movie_decode/placeholder_frame.cpp\", \"operators/shared/movie_decode/avf_decoder.mm\", \"operators/shared/movie_decode/hap_decoder.mm\", \"operators/shared/movie_decode/codec_probe.mm\", \"deps/hap/hap.c\"], \"extra_libs\": [\"webgpu\", \"snappy\"], \"frameworks\": [\"AVFoundation\", \"CoreMedia\", \"CoreVideo\", \"Foundation\"], \"objc_arc\": [\"operators/shared/movie_decode/avf_decoder.mm\", \"operators/shared/movie_decode/hap_decoder.mm\", \"operators/shared/movie_decode/codec_probe.mm\"], \"include_dirs\": [\"deps/stb\", \"deps/hap\"] }")
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

# --- Syphon In (macOS Syphon client input) ---
if(APPLE)
    add_library(syphon_in MODULE
        operators/gpu/syphon_in/syphon_in.mm
    )
    target_link_libraries(syphon_in PRIVATE
        vivid_operator_api webgpu syphon_runtime
        "-framework Foundation" "-framework AppKit" "-framework Metal"
    )
    target_include_directories(syphon_in PRIVATE deps/syphon deps)
    set_source_files_properties(operators/gpu/syphon_in/syphon_in.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    add_library(syphon_in MODULE operators/gpu/syphon_in/syphon_in_stub.cpp)
    target_link_libraries(syphon_in PRIVATE vivid_operator_api webgpu)
endif()
set_target_properties(syphon_in PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
if(APPLE)
    add_custom_command(TARGET syphon_in POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:syphon_in>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        COMMENT "Updating syphon_in in Vivid.app bundle"
    )
    set_target_properties(syphon_in PROPERTIES
        BUILD_RPATH "@loader_path/../Frameworks"
        INSTALL_RPATH "@loader_path/../Frameworks")
endif()
if(APPLE)
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"syphon_in\": { \"sources\": [\"operators/gpu/syphon_in/syphon_in.mm\"], \"extra_libs\": [\"webgpu\"], \"frameworks\": [\"Foundation\", \"AppKit\", \"Metal\"], \"objc_arc\": [\"operators/gpu/syphon_in/syphon_in.mm\"], \"include_dirs\": [\"deps/syphon\", \"deps\"] }")
else()
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"syphon_in\": { \"sources\": [\"operators/gpu/syphon_in/syphon_in_stub.cpp\"], \"extra_libs\": [\"webgpu\"] }")
endif()
set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS syphon_in)

# --- SyphonOut operator (macOS only) ---
if(APPLE)
    add_library(syphon_out MODULE
        operators/gpu/syphon_out/syphon_out.mm
    )
    target_link_libraries(syphon_out PRIVATE
        vivid_operator_api webgpu syphon_runtime
    )
    target_include_directories(syphon_out PRIVATE deps/syphon deps)
    set_source_files_properties(operators/gpu/syphon_out/syphon_out.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    add_library(syphon_out MODULE operators/gpu/syphon_out/syphon_out_stub.cpp)
    target_link_libraries(syphon_out PRIVATE vivid_operator_api webgpu)
endif()
set_target_properties(syphon_out PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
if(APPLE)
    add_custom_command(TARGET syphon_out POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:syphon_out>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        COMMENT "Updating syphon_out in Vivid.app bundle"
    )
    set_target_properties(syphon_out PROPERTIES
        BUILD_RPATH "@loader_path/../Frameworks"
        INSTALL_RPATH "@loader_path/../Frameworks")
endif()
if(APPLE)
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"syphon_out\": { \"sources\": [\"operators/gpu/syphon_out/syphon_out.mm\"], \"extra_libs\": [\"webgpu\"], \"frameworks\": [\"Foundation\", \"AppKit\", \"Metal\"], \"objc_arc\": [\"operators/gpu/syphon_out/syphon_out.mm\"], \"include_dirs\": [\"deps/syphon\", \"deps\"] }")
else()
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"syphon_out\": { \"sources\": [\"operators/gpu/syphon_out/syphon_out_stub.cpp\"], \"extra_libs\": [\"webgpu\"] }")
endif()
set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS syphon_out)

# --- Audio operator plugins ---
add_vivid_operator(oscillator     operators/audio/oscillator/oscillator.cpp)
add_vivid_operator(gain           operators/audio/gain/gain.cpp)
add_vivid_operator(reverb         operators/audio/reverb/reverb.cpp
                   FACTORY_PRESETS operators/audio/reverb/factory_presets.json)
add_vivid_operator(delay          operators/audio/delay/delay.cpp
                   FACTORY_PRESETS operators/audio/delay/factory_presets.json)
add_vivid_operator(bitcrush       operators/audio/bitcrush/bitcrush.cpp
                   FACTORY_PRESETS operators/audio/bitcrush/factory_presets.json)
add_vivid_operator(distortion     operators/audio/distortion/distortion.cpp
                   FACTORY_PRESETS operators/audio/distortion/factory_presets.json)
add_vivid_operator(filter         operators/audio/filter/filter.cpp         EXTRA_LIBS webgpu)
target_sources(filter PRIVATE src/operator_api/filter_dsp.cpp)
add_vivid_operator(audio_noise    operators/audio/noise/noise.cpp    EXTRA_LIBS webgpu)
add_vivid_operator(mixer          operators/audio/mixer/mixer.cpp          EXTRA_LIBS webgpu)
add_vivid_operator(compressor     operators/audio/compressor/compressor.cpp)
add_vivid_operator(limiter        operators/audio/limiter/limiter.cpp)
add_vivid_operator(chorus         operators/audio/chorus/chorus.cpp
                   FACTORY_PRESETS operators/audio/chorus/factory_presets.json)
add_vivid_operator(phaser         operators/audio/phaser/phaser.cpp
                   FACTORY_PRESETS operators/audio/phaser/factory_presets.json)
add_vivid_operator(flanger        operators/audio/flanger/flanger.cpp
                   FACTORY_PRESETS operators/audio/flanger/factory_presets.json)
add_vivid_operator(stereo_pan_width operators/audio/stereo_pan_width/stereo_pan_width.cpp
                   FACTORY_PRESETS operators/audio/stereo_pan_width/factory_presets.json)
add_vivid_operator(ping_pong_delay  operators/audio/ping_pong_delay/ping_pong_delay.cpp
                   FACTORY_PRESETS operators/audio/ping_pong_delay/factory_presets.json)
add_vivid_operator(fm_synth         operators/audio/fm_synth/fm_synth.cpp
                   FACTORY_PRESETS  operators/audio/fm_synth/factory_presets.json)
add_vivid_operator(ring_mod         operators/audio/ring_mod/ring_mod.cpp
                   FACTORY_PRESETS  operators/audio/ring_mod/factory_presets.json)
add_vivid_operator(parametric_eq    operators/audio/parametric_eq/parametric_eq.cpp
                   FACTORY_PRESETS  operators/audio/parametric_eq/factory_presets.json)
add_vivid_operator(audio_analysis   operators/audio/audio_analysis/audio_analysis.cpp)
add_vivid_operator(mic_input        operators/audio/mic_input/mic_input.cpp EXTRA_LIBS miniaudio)

# --- Drum operators (from vivid-drums) ---
add_vivid_operator(drum_kick     operators/audio/drum_kick/drum_kick.cpp
                   FACTORY_PRESETS operators/audio/drum_kick/factory_presets.json)
add_vivid_operator(drum_snare    operators/audio/drum_snare/drum_snare.cpp
                   FACTORY_PRESETS operators/audio/drum_snare/factory_presets.json)
add_vivid_operator(drum_hihat    operators/audio/drum_hihat/drum_hihat.cpp
                   FACTORY_PRESETS operators/audio/drum_hihat/factory_presets.json)
add_vivid_operator(drum_clap     operators/audio/drum_clap/drum_clap.cpp
                   FACTORY_PRESETS operators/audio/drum_clap/factory_presets.json)
add_vivid_operator(drum_cymbal   operators/audio/drum_cymbal/drum_cymbal.cpp
                   FACTORY_PRESETS operators/audio/drum_cymbal/factory_presets.json)
add_vivid_operator(drum_tom      operators/audio/drum_tom/drum_tom.cpp
                   FACTORY_PRESETS operators/audio/drum_tom/factory_presets.json)

# --- Sampler operators (from vivid-sampler) ---
# Static libraries for miniaudio decoder support
add_library(sampler_miniaudio STATIC operators/shared/sampler_common/miniaudio_impl.c)
target_include_directories(sampler_miniaudio PRIVATE ${CMAKE_SOURCE_DIR}/deps/miniaudio)

add_vivid_operator(sp404            operators/audio/sp404/sp404.cpp
                   EXTRA_LIBS sampler_miniaudio nlohmann_json::nlohmann_json)
add_vivid_operator(sampler          operators/audio/sampler/sampler.cpp
                   EXTRA_LIBS sampler_miniaudio nlohmann_json::nlohmann_json)
add_vivid_operator(slicer           operators/audio/slicer/slicer.cpp
                   EXTRA_LIBS sampler_miniaudio nlohmann_json::nlohmann_json)
add_vivid_operator(granular_synth   operators/audio/granular_synth/granular_synth.cpp)
add_vivid_operator(vocoder          operators/audio/vocoder/vocoder.cpp)
add_vivid_operator(spectral_freeze  operators/audio/spectral_freeze/spectral_freeze.cpp)
foreach(_samp_op sp404 sampler slicer)
    target_include_directories(${_samp_op} PRIVATE
        ${CMAKE_SOURCE_DIR}/operators/shared/sampler_common
        ${CMAKE_SOURCE_DIR}/deps/miniaudio)
endforeach()

add_library(midi_file_player MODULE
    operators/audio/midi_file_player/midi_file_player.cpp
    src/common/midi_file.cpp
)
target_link_libraries(midi_file_player PRIVATE vivid_operator_api)
set_target_properties(midi_file_player PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
if(APPLE)
    add_custom_command(TARGET midi_file_player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:midi_file_player>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        COMMENT "Updating midi_file_player in Vivid.app bundle"
    )
endif()
set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
    "  \"midi_file_player\": { \"sources\": [\"operators/audio/midi_file_player/midi_file_player.cpp\", \"src/common/midi_file.cpp\"], \"extra_libs\": [] }")
set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS midi_file_player)

# --- MovieFileAudio (cadence-native audio operator for movie file playback) ---
if(APPLE)
    add_library(movie_file_audio MODULE
        operators/audio/movie_file_audio/movie_file_audio.cpp
        operators/shared/movie_audio/avf_audio_extractor.mm
    )
    target_link_libraries(movie_file_audio PRIVATE
        vivid_operator_api
        "-framework AVFoundation" "-framework AVFAudio" "-framework CoreMedia" "-framework Foundation"
    )
    set_source_files_properties(
        operators/shared/movie_audio/avf_audio_extractor.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_target_properties(movie_file_audio PROPERTIES
        PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
    add_custom_command(TARGET movie_file_audio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:movie_file_audio>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        COMMENT "Updating movie_file_audio in Vivid.app bundle"
    )
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"movie_file_audio\": { \"sources\": [\"operators/audio/movie_file_audio/movie_file_audio.cpp\", \"operators/shared/movie_audio/avf_audio_extractor.mm\"], \"extra_libs\": [], \"frameworks\": [\"AVFoundation\", \"AVFAudio\", \"CoreMedia\", \"Foundation\"], \"objc_arc\": [\"operators/shared/movie_audio/avf_audio_extractor.mm\"] }")
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS movie_file_audio)
endif()

# --- Operators meta-target (for package smoke CI: builds all operator dylibs without the app) ---
get_property(_vivid_op_targets GLOBAL PROPERTY VIVID_OPERATOR_TARGETS)
add_custom_target(operators DEPENDS ${_vivid_op_targets})

# --- UI library ---
add_library(vivid_ui STATIC
    src/ui/renderer_2d.cpp
    src/ui/thumbnail_renderer.cpp
    src/ui/thumbnail_cache.cpp
    src/ui/i18n.cpp
    src/ui/node_graph.cpp
    src/ui/node_graph_overlays.cpp
    src/ui/node_graph_draw.cpp
    src/ui/node_graph_overlay_draw.cpp
    src/ui/node_graph_input.cpp
    src/ui/node_graph_overlay_input.cpp
    src/ui/overlay_layouts.cpp
    src/ui/ui_style.cpp
    src/ui/theme_loader.cpp
)
if(APPLE)
    target_sources(vivid_ui PRIVATE src/ui/file_dialog.mm)
    set_source_files_properties(src/ui/file_dialog.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
target_include_directories(vivid_ui PRIVATE src)
target_link_libraries(vivid_ui PUBLIC vivid_operator_api webgpu)
target_link_libraries(vivid_ui PRIVATE stb_truetype glfw nlohmann_json::nlohmann_json)
target_compile_definitions(vivid_ui PRIVATE "VIVID_CORE_VERSION=\"${PROJECT_VERSION}\"")

# --- Vivid executable ---
add_executable(vivid
    src/runtime/main.cpp
    src/runtime/gpu_context.cpp
    src/runtime/fullscreen_blit.cpp
    src/runtime/output_window.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/audio_engine.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/audio_executor.cpp
    src/runtime/file_watcher.cpp
    src/runtime/hot_reload.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/runtime_api.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/package_manager.cpp
    src/runtime/undo_manager.cpp
    src/runtime/screenshot.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/control_server.cpp
    src/runtime/capture_coordinator.cpp
    src/runtime/output_analyzer.cpp
    src/runtime/file_drop_registry.cpp
    src/runtime/av_exporter.mm
    src/runtime/operator_creator.cpp
    src/runtime/system_midi.cpp
    src/runtime/platform.cpp
    src/runtime/settings.cpp
    src/runtime/editor_detect.cpp
    src/runtime/macos_frame_timer.cpp
    src/export/export_pipeline.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_scaffolder.cpp
    src/runtime/package_test_runner.cpp
    src/runtime/package_catalog.cpp
    src/runtime/app_update_manager.cpp
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
target_link_libraries(vivid PRIVATE vivid_ui webgpu glfw glfw3webgpu vivid_operator_api nlohmann_json::nlohmann_json miniaudio stb_truetype ixwebsocket rtmidi CLI11::CLI11)
if(APPLE)
    target_link_libraries(vivid PRIVATE syphon_runtime)
    # Ensure bundle-launched app resolves syphon runtime from Contents/Frameworks.
    set_target_properties(vivid PROPERTIES
        BUILD_RPATH "@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks")
endif()

if(APPLE)
    target_compile_definitions(vivid PRIVATE
        "VIVID_CORE_VERSION=\"${PROJECT_VERSION}\""
        "VIVID_BUILD_DIR=\"${CMAKE_BINARY_DIR}\""
        "VIVID_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
else()
    target_compile_definitions(vivid PRIVATE
        "VIVID_CORE_VERSION=\"${PROJECT_VERSION}\"")
endif()

# macOS native menu bar
if(APPLE)
    target_sources(vivid PRIVATE src/runtime/macos_menu.mm src/runtime/metal_interop.mm src/runtime/syphon_output.mm src/runtime/sparkle_bridge.mm)
    set_source_files_properties(src/runtime/macos_menu.mm src/runtime/metal_interop.mm src/runtime/syphon_output.mm src/runtime/sparkle_bridge.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

# ObjC ARC for AVExporter
set_source_files_properties(src/runtime/av_exporter.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

# Copy default startup graph
configure_file(graphs/intro/demo.json ${CMAKE_BINARY_DIR}/graph.json COPYONLY)

# Copy all graphs recursively into build/graphs
file(GLOB_RECURSE DEMO_GRAPHS CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR}/graphs graphs/*.json)
foreach(g ${DEMO_GRAPHS})
    configure_file(${CMAKE_SOURCE_DIR}/graphs/${g} ${CMAKE_BINARY_DIR}/graphs/${g} COPYONLY)
endforeach()

set(VIVID_DEMO_ASSETS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/assets")

# Copy all demo assets recursively into build/assets
file(GLOB_RECURSE DEMO_ASSETS RELATIVE ${VIVID_DEMO_ASSETS_SOURCE_DIR} ${VIVID_DEMO_ASSETS_SOURCE_DIR}/*)
list(FILTER DEMO_ASSETS EXCLUDE REGEX "^\\.")
list(FILTER DEMO_ASSETS EXCLUDE REGEX "/\\.")
foreach(m ${DEMO_ASSETS})
    configure_file(${VIVID_DEMO_ASSETS_SOURCE_DIR}/${m} ${CMAKE_BINARY_DIR}/assets/${m} COPYONLY)
endforeach()

# Copy font to build directory
configure_file(fonts/JetBrainsMono-Regular.ttf ${CMAKE_BINARY_DIR}/JetBrainsMono-Regular.ttf COPYONLY)

# Copy default graph template to build directory
configure_file(resources/default_graph.json ${CMAKE_BINARY_DIR}/default_graph.json COPYONLY)

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

    # Demo graphs → Resources/graphs/(recursive)
    file(GLOB_RECURSE _BUNDLE_GRAPHS CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR}/graphs graphs/*.json)
    foreach(g ${_BUNDLE_GRAPHS})
        set(src_path ${CMAKE_SOURCE_DIR}/graphs/${g})
        get_filename_component(rel_dir ${g} DIRECTORY)
        if(rel_dir STREQUAL "")
            set(dst_loc Resources/graphs)
        else()
            set(dst_loc Resources/graphs/${rel_dir})
        endif()
        target_sources(vivid PRIVATE ${src_path})
        set_source_files_properties(${src_path}
            PROPERTIES MACOSX_PACKAGE_LOCATION ${dst_loc})
    endforeach()

    # Demo assets → Resources/assets/(recursive)
    file(GLOB_RECURSE _BUNDLE_ASSETS RELATIVE ${VIVID_DEMO_ASSETS_SOURCE_DIR} ${VIVID_DEMO_ASSETS_SOURCE_DIR}/*)
    list(FILTER _BUNDLE_ASSETS EXCLUDE REGEX "^\\.")
    list(FILTER _BUNDLE_ASSETS EXCLUDE REGEX "/\\.")
    foreach(m ${_BUNDLE_ASSETS})
        set(src_path ${VIVID_DEMO_ASSETS_SOURCE_DIR}/${m})
        get_filename_component(rel_dir ${m} DIRECTORY)
        if(rel_dir STREQUAL "")
            set(dst_loc Resources/assets)
        else()
            set(dst_loc Resources/assets/${rel_dir})
        endif()
        target_sources(vivid PRIVATE ${src_path})
        set_source_files_properties(${src_path}
            PROPERTIES MACOSX_PACKAGE_LOCATION ${dst_loc})
    endforeach()

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

    # Default graph template → Resources/
    target_sources(vivid PRIVATE resources/default_graph.json)
    set_source_files_properties(resources/default_graph.json
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

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
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/Frameworks
    )

    if(TARGET syphon_runtime)
        add_custom_command(TARGET vivid POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:syphon_runtime>
                $<TARGET_BUNDLE_CONTENT_DIR:vivid>/Frameworks/
        )
    endif()

    # Keep WebGPU runtime available in Frameworks for package plugins that link
    # libwgpu_native via @rpath.
    add_custom_command(TARGET vivid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/MacOS/libwgpu_native.dylib
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/Frameworks/libwgpu_native.dylib
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
add_vivid_operator(test_op_incompatible_port tests/operators/test_op_incompatible_port.cpp)
add_vivid_operator(test_op_null_desc tests/operators/test_op_null_desc.cpp)
add_vivid_operator(control_thumb_op tests/operators/control_thumb_op.cpp EXTRA_LIBS webgpu)
add_vivid_operator(test_op_bad_custom_type tests/operators/test_op_bad_custom_type.cpp)
add_vivid_operator(audio_reload_v1 tests/operators/audio_reload_v1.cpp)
add_vivid_operator(audio_reload_v2 tests/operators/audio_reload_v2.cpp)
add_vivid_operator(audio_reload_v3 tests/operators/audio_reload_v3.cpp)
add_vivid_operator(audio_reload_incompatible tests/operators/audio_reload_incompatible.cpp)
add_vivid_operator(export_custom_port_op tests/operators/export_custom_port_op.cpp)
add_vivid_operator(test_state_carry_op tests/operators/test_state_carry_op.cpp)
add_vivid_operator(string_source_op tests/operators/string_source_op.cpp)
add_vivid_operator(string_sink_op tests/operators/string_sink_op.cpp)
add_vivid_operator(file_drop_test_op tests/operators/file_drop_test_op.cpp)
add_vivid_operator(file_drop_test_op_alt tests/operators/file_drop_test_op_alt.cpp)
add_vivid_operator(file_drop_bad_param_op tests/operators/file_drop_bad_param_op.cpp)
add_vivid_operator(semantic_ms_source_op tests/operators/semantic_ms_source_op.cpp)
add_vivid_operator(semantic_s_dest_op    tests/operators/semantic_s_dest_op.cpp)
add_vivid_operator(semantic_unknown_source_op tests/operators/semantic_unknown_source_op.cpp)
add_vivid_operator(untagged_dest_op          tests/operators/untagged_dest_op.cpp)



# Hot-reload integration test (no GPU, no audio, no window)
add_executable(test_hot_reload
    tests/test_hot_reload.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
)
target_include_directories(test_hot_reload PRIVATE src)
target_link_libraries(test_hot_reload PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_hot_reload test_op_v1 test_op_v2 test_op_incompatible_port)
add_test(NAME test_hot_reload COMMAND test_hot_reload WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_hot_reload PROPERTIES TIMEOUT 15)

add_executable(test_audio_hot_reload
    tests/test_audio_hot_reload.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
)
target_include_directories(test_audio_hot_reload PRIVATE src)
target_link_libraries(test_audio_hot_reload PRIVATE vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_audio_hot_reload audio_reload_v1 audio_reload_v2 audio_reload_incompatible)
add_test(NAME test_audio_hot_reload COMMAND test_audio_hot_reload WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_audio_hot_reload PROPERTIES TIMEOUT 15)

add_executable(test_export_pipeline
    tests/test_export_pipeline.cpp
    src/export/export_pipeline.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/tool_discovery.cpp
)
target_include_directories(test_export_pipeline PRIVATE src)
target_link_libraries(test_export_pipeline PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_export_pipeline export_custom_port_op)
add_test(NAME test_export_pipeline COMMAND test_export_pipeline WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# RuntimeAPI integration test (no GPU, no audio, no window)
add_executable(test_runtime_api
    tests/test_runtime_api.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/runtime_api.cpp
    src/runtime/system_midi.cpp
)
target_include_directories(test_runtime_api PRIVATE src)
target_link_libraries(test_runtime_api PRIVATE vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_runtime_api test_op_v1 test_state_carry_op)
add_test(NAME test_runtime_api COMMAND test_runtime_api WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Latency validation test (PRD responsiveness claims)
add_executable(test_latency_validation
    tests/test_latency_validation.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/runtime_api.cpp
    src/runtime/system_midi.cpp
)
target_include_directories(test_latency_validation PRIVATE src)
target_link_libraries(test_latency_validation PRIVATE vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_latency_validation test_op_v1 audio_reload_v1 audio_reload_v2)
add_test(NAME test_latency_validation COMMAND test_latency_validation ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_latency_validation PROPERTIES TIMEOUT 30)

# ControlServer integration test (HTTP endpoints, no GPU, no audio, no window)
add_executable(test_control_server
    tests/test_control_server.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/runtime_api.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/package_manager.cpp
    src/runtime/undo_manager.cpp
    src/runtime/control_server.cpp
    src/runtime/capture_coordinator.cpp
    src/runtime/output_analyzer.cpp
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
    src/runtime/app_update_manager.cpp
)
target_include_directories(test_control_server PRIVATE src)
target_link_libraries(test_control_server PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio ixwebsocket webgpu rtmidi stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
add_dependencies(test_control_server test_op_v1 semantic_ms_source_op semantic_s_dest_op
    semantic_unknown_source_op untagged_dest_op export_custom_port_op test_op_bad_custom_type)
add_test(NAME test_control_server COMMAND test_control_server ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Perception introspection matrix test (domain snapshots + deterministic health)
add_executable(test_perception_introspection
    tests/test_perception_introspection.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/runtime_api.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/package_manager.cpp
    src/runtime/undo_manager.cpp
    src/runtime/control_server.cpp
    src/runtime/capture_coordinator.cpp
    src/runtime/output_analyzer.cpp
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
    src/runtime/app_update_manager.cpp
)
target_include_directories(test_perception_introspection PRIVATE src)
target_link_libraries(test_perception_introspection PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio ixwebsocket webgpu rtmidi stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
add_dependencies(test_perception_introspection test_op_v1)
add_test(NAME test_perception_introspection COMMAND test_perception_introspection WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Test operator plugins
add_vivid_operator(audio_test_op      tests/operators/audio_test_op.cpp)
add_vivid_operator(audio_float_cv_op  tests/operators/audio_float_cv_op.cpp)
add_vivid_operator(audio_throwing_op  tests/operators/audio_throwing_op.cpp)
add_vivid_operator(control_pass_op    tests/operators/control_pass_op.cpp)
add_vivid_operator(spread_source_op   tests/operators/spread_source_op.cpp)
add_vivid_operator(spread_sink_op     tests/operators/spread_sink_op.cpp)
add_vivid_operator(lane_metadata_op       tests/operators/lane_metadata_op.cpp)
add_vivid_operator(lane_metadata_audio_op tests/operators/lane_metadata_audio_op.cpp)
add_vivid_operator(lane_smooth_op         tests/operators/lane_smooth_op.cpp)
add_vivid_operator(audio_spread_op    tests/operators/audio_spread_op.cpp)

# AudioEngine integration test (null audio backend, no GPU, no window)
add_executable(test_audio_engine
    tests/test_audio_engine.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/runtime_api.cpp
    src/runtime/system_midi.cpp
)
target_include_directories(test_audio_engine PRIVATE src)
target_link_libraries(test_audio_engine PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_audio_engine test_op_v1 audio_test_op)
add_test(NAME test_audio_engine COMMAND test_audio_engine WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_audio_float_snapshot
    tests/test_audio_float_snapshot.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
)
target_include_directories(test_audio_float_snapshot PRIVATE src)
target_link_libraries(test_audio_float_snapshot PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_audio_float_snapshot test_op_v1 audio_float_cv_op)
add_test(NAME test_audio_float_snapshot COMMAND test_audio_float_snapshot
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Audio thread robustness test (exception handling in audio callback)
add_executable(test_audio_robustness
    tests/test_audio_robustness.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
)
target_include_directories(test_audio_robustness PRIVATE src)
target_link_libraries(test_audio_robustness PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_audio_robustness audio_test_op audio_throwing_op)
add_test(NAME test_audio_robustness COMMAND test_audio_robustness WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Scheduler unit test (no GPU, no audio playback, no window)
add_executable(test_runtime_core
    tests/test_runtime_core.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
)
target_include_directories(test_runtime_core PRIVATE src)
target_link_libraries(test_runtime_core PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_runtime_core test_op_v1 control_pass_op spread_source_op audio_test_op)
add_test(NAME test_runtime_core COMMAND test_runtime_core WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Graph data-model unit test (no operators, no runtime, no GPU)
add_executable(test_graph
    tests/test_graph.cpp
    src/runtime/graph.cpp
)
target_include_directories(test_graph PRIVATE src)
target_link_libraries(test_graph PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME test_graph COMMAND test_graph WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# JSON migration round-trip regression test
add_executable(test_json_migration
    tests/test_json_migration.cpp
    src/runtime/graph.cpp
)
target_include_directories(test_json_migration PRIVATE src)
target_link_libraries(test_json_migration PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(test_json_migration PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_json_migration COMMAND test_json_migration WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

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
target_link_libraries(test_operator_creator PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_operator_creator COMMAND test_operator_creator WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Settings round-trip unit test
add_executable(test_settings
    tests/test_settings.cpp
    src/runtime/settings.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_settings PRIVATE src tests)
target_link_libraries(test_settings PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME test_settings COMMAND test_settings WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Operator destination policy precedence unit test
add_executable(test_operator_destination_policy
    tests/test_operator_destination_policy.cpp
)
target_include_directories(test_operator_destination_policy PRIVATE src tests)
add_test(NAME test_operator_destination_policy
    COMMAND test_operator_destination_policy
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Theme loader unit test
add_executable(test_theme_loader
    tests/test_theme_loader.cpp
    src/ui/theme_loader.cpp
    src/ui/ui_style.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_theme_loader PRIVATE src tests)
target_link_libraries(test_theme_loader PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME test_theme_loader COMMAND test_theme_loader WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Overlay layout/hit-test regressions
add_executable(test_overlay_layouts
    tests/test_overlay_layouts.cpp
    src/ui/overlay_layouts.cpp
)
target_include_directories(test_overlay_layouts PRIVATE src tests)
add_test(NAME test_overlay_layouts COMMAND test_overlay_layouts WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_graph_snapshot_contract
    tests/test_graph_snapshot_contract.cpp
)
target_include_directories(test_graph_snapshot_contract PRIVATE src tests)
add_test(NAME test_graph_snapshot_contract COMMAND test_graph_snapshot_contract WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Inspector layout normalization tests
add_executable(test_inspector_layout
    tests/test_inspector_layout.cpp
)
target_include_directories(test_inspector_layout PRIVATE src tests)
add_test(NAME test_inspector_layout COMMAND test_inspector_layout WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# UI overlay interaction regressions (example/package/meta overlay callbacks)
add_executable(test_ui_overlay_interactions
    tests/test_ui_overlay_interactions.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_ui_overlay_interactions PRIVATE src tests)
target_link_libraries(test_ui_overlay_interactions PRIVATE vivid_ui webgpu glfw nlohmann_json::nlohmann_json stb_truetype)
if(APPLE)
    target_link_libraries(test_ui_overlay_interactions PRIVATE
        "-framework Cocoa" "-framework Foundation")
endif()
add_test(NAME test_ui_overlay_interactions COMMAND test_ui_overlay_interactions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_overlay_interactions PROPERTIES LABELS "UI_SMOKE")

add_executable(test_ui_editor_interactions
    tests/test_ui_editor_interactions.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_ui_editor_interactions PRIVATE src tests)
target_link_libraries(test_ui_editor_interactions PRIVATE vivid_ui webgpu glfw nlohmann_json::nlohmann_json stb_truetype)
if(APPLE)
    target_link_libraries(test_ui_editor_interactions PRIVATE
        "-framework Cocoa" "-framework Foundation")
endif()
add_test(NAME test_ui_editor_interactions COMMAND test_ui_editor_interactions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_editor_interactions PROPERTIES LABELS "UI_SMOKE")

add_executable(test_ui_widget_interactions
    tests/test_ui_widget_interactions.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_ui_widget_interactions PRIVATE src tests)
target_link_libraries(test_ui_widget_interactions PRIVATE vivid_ui webgpu glfw nlohmann_json::nlohmann_json stb_truetype)
if(APPLE)
    target_link_libraries(test_ui_widget_interactions PRIVATE
        "-framework Cocoa" "-framework Foundation")
endif()
add_test(NAME test_ui_widget_interactions COMMAND test_ui_widget_interactions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_widget_interactions PROPERTIES LABELS "UI_SMOKE")

# Windowed GUI smoke for inspector and editor-drop regressions.
add_executable(test_ui_screenshot_smoke
    tests/test_ui_screenshot_smoke.cpp
)
target_include_directories(test_ui_screenshot_smoke PRIVATE src tests deps/stb)
target_link_libraries(test_ui_screenshot_smoke PRIVATE nlohmann_json::nlohmann_json)
add_dependencies(test_ui_screenshot_smoke vivid file_drop_test_op file_drop_test_op_alt)
if(APPLE)
    add_custom_command(TARGET test_ui_screenshot_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:file_drop_test_op>
            $<TARGET_FILE:file_drop_test_op_alt>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        COMMENT "Staging GUI smoke file-drop fixtures into Vivid.app bundle"
    )
endif()
add_test(NAME test_ui_screenshot_smoke
    COMMAND test_ui_screenshot_smoke ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_screenshot_smoke PROPERTIES
    LABELS "GUI_SMOKE"
    TIMEOUT 180
    ENVIRONMENT
        "VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1;VIVID_UI_SMOKE_LANE=gui_smoke;HOME=${CMAKE_BINARY_DIR}/.test_ui_screenshot_smoke/gui_smoke/home")

add_test(NAME test_ui_screenshot_smoke_env
    COMMAND test_ui_screenshot_smoke ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_screenshot_smoke_env PROPERTIES
    LABELS "GUI_ENV"
    TIMEOUT 180
    ENVIRONMENT
        "VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1;VIVID_ENABLE_GUI_ENV_SMOKE=1;VIVID_UI_SMOKE_LANE=gui_env;HOME=${CMAKE_BINARY_DIR}/.test_ui_screenshot_smoke/gui_env/home")

add_test(NAME test_ui_screenshot_smoke_harness
    COMMAND test_ui_screenshot_smoke ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_screenshot_smoke_harness PROPERTIES
    LABELS "UI_SMOKE"
    TIMEOUT 30
    ENVIRONMENT
        "VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1;VIVID_UI_SMOKE_HARNESS_SELFTEST=1;VIVID_UI_SMOKE_LANE=harness_selftest;HOME=${CMAKE_BINARY_DIR}/.test_ui_screenshot_smoke/harness_selftest/home")

# Architecture guard: UI layer must not directly include runtime package catalog
add_executable(test_ui_arch_guard tests/test_ui_arch_guard.cpp)
target_include_directories(test_ui_arch_guard PRIVATE src tests)
add_test(NAME test_ui_arch_guard COMMAND test_ui_arch_guard ${CMAKE_SOURCE_DIR} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# WGSL header parser unit test
add_executable(test_wgsl_header
    tests/test_wgsl_header.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_wgsl_header PRIVATE src)
target_link_libraries(test_wgsl_header PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME test_wgsl_header COMMAND test_wgsl_header WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# WGSL include preprocessor diagnostics (missing include/cycle/include chain)
add_executable(test_wgsl_preprocessor
    tests/test_wgsl_preprocessor.cpp
)
target_include_directories(test_wgsl_preprocessor PRIVATE src tests)
add_test(NAME test_wgsl_preprocessor COMMAND test_wgsl_preprocessor WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# String utility unit test (header-only, no dependencies)
add_executable(test_string_util tests/test_string_util.cpp)
target_include_directories(test_string_util PRIVATE src tests)
add_test(NAME test_string_util COMMAND test_string_util WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Text editing unit test (header-only, no dependencies)
add_executable(test_text_edit tests/test_text_edit.cpp)
target_include_directories(test_text_edit PRIVATE src)
add_test(NAME test_text_edit COMMAND test_text_edit WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# audio_dsp operator API compatibility test
add_executable(test_audio_dsp_api tests/test_audio_dsp_api.cpp)
target_include_directories(test_audio_dsp_api PRIVATE src)
add_test(NAME test_audio_dsp_api COMMAND test_audio_dsp_api WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# OperatorLoader + OperatorRegistry unit tests (no GPU, no runtime, no graph)
add_executable(test_operator_loader
    tests/test_operator_loader.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_operator_loader PRIVATE src)
target_link_libraries(test_operator_loader PRIVATE vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_operator_loader test_op_v1 test_op_v2 test_op_incompatible_port test_op_null_desc test_op_bad_custom_type control_pass_op audio_test_op export_custom_port_op control_thumb_op file_drop_test_op particles)
add_test(NAME test_operator_loader COMMAND test_operator_loader WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_operator_loader PROPERTIES LABELS "LOADER")

add_executable(test_file_drop_registry
    tests/test_file_drop_registry.cpp
    src/runtime/file_drop_registry.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_file_drop_registry PRIVATE src)
target_link_libraries(test_file_drop_registry PRIVATE vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_file_drop_registry file_drop_test_op file_drop_test_op_alt file_drop_bad_param_op midi_file_player)
add_test(NAME test_file_drop_registry COMMAND test_file_drop_registry WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_midi_file_parser
    tests/test_midi_file_parser.cpp
    src/common/midi_file.cpp
)
target_include_directories(test_midi_file_parser PRIVATE src tests)
target_link_libraries(test_midi_file_parser PRIVATE vivid_operator_api)
add_test(NAME test_midi_file_parser COMMAND test_midi_file_parser WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_midi_file_player
    tests/test_midi_file_player.cpp
    src/common/midi_file.cpp
)
target_include_directories(test_midi_file_player PRIVATE src tests operators)
target_link_libraries(test_midi_file_player PRIVATE vivid_operator_api)
add_test(NAME test_midi_file_player COMMAND test_midi_file_player WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_runtime_stress
    tests/test_runtime_stress.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/runtime_api.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_runtime_stress PRIVATE src)
target_link_libraries(test_runtime_stress PRIVATE vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_runtime_stress test_op_v1 test_state_carry_op)
add_test(NAME test_runtime_stress COMMAND test_runtime_stress ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_runtime_stress PROPERTIES LABELS "STABILITY" TIMEOUT 30)

add_executable(test_hot_reload_stress
    tests/test_hot_reload_stress.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_hot_reload_stress PRIVATE src)
target_link_libraries(test_hot_reload_stress PRIVATE vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_hot_reload_stress audio_reload_v1 audio_reload_v2 audio_reload_v3 audio_reload_incompatible)
add_test(NAME test_hot_reload_stress COMMAND test_hot_reload_stress ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_hot_reload_stress PROPERTIES LABELS "STABILITY" TIMEOUT 30)

add_executable(test_package_stress
    tests/test_package_stress.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/runtime_api.cpp
    src/runtime/package_compiler.cpp
    src/runtime/package_manager.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/platform.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_package_stress PRIVATE src)
target_link_libraries(test_package_stress PRIVATE vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_test(NAME test_package_stress COMMAND test_package_stress ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_stress PROPERTIES LABELS "STABILITY" TIMEOUT 30)

add_executable(test_mixed_runtime_stability
    tests/test_mixed_runtime_stability.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_mixed_runtime_stability PRIVATE src)
target_link_libraries(test_mixed_runtime_stability PRIVATE vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_mixed_runtime_stability gpu_fill_op lfo oscillator)
add_test(NAME test_mixed_runtime_stability COMMAND test_mixed_runtime_stability ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_mixed_runtime_stability PROPERTIES LABELS "STABILITY" TIMEOUT 30)

# Modulation operator tests (Flanger, Chorus, Phaser)
add_executable(test_modulation_ops
    tests/test_modulation_ops.cpp
    src/runtime/operator_loader.cpp
)
target_include_directories(test_modulation_ops PRIVATE src)
target_link_libraries(test_modulation_ops PRIVATE vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_modulation_ops flanger chorus phaser)
add_test(NAME test_modulation_ops COMMAND test_modulation_ops WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Spatial operator tests (StereoPanWidth, PingPongDelay)
add_executable(test_spatial_ops
    tests/test_spatial_ops.cpp
    src/runtime/operator_loader.cpp
)
target_include_directories(test_spatial_ops PRIVATE src)
target_link_libraries(test_spatial_ops PRIVATE vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_spatial_ops stereo_pan_width ping_pong_delay)
add_test(NAME test_spatial_ops COMMAND test_spatial_ops WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Synthesis & transformation operator tests (FmSynth, RingMod, ParametricEQ)
add_executable(test_synth_transform_ops
    tests/test_synth_transform_ops.cpp
    src/runtime/operator_loader.cpp
)
target_include_directories(test_synth_transform_ops PRIVATE src)
target_link_libraries(test_synth_transform_ops PRIVATE vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_synth_transform_ops fm_synth ring_mod parametric_eq)
add_test(NAME test_synth_transform_ops COMMAND test_synth_transform_ops WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Audio output correctness tests (spectral/amplitude property assertions)
add_executable(test_audio_correctness
    tests/test_audio_correctness.cpp
    src/runtime/operator_loader.cpp
    src/runtime/output_analyzer.cpp
)
target_include_directories(test_audio_correctness PRIVATE src)
target_link_libraries(test_audio_correctness PRIVATE vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_audio_correctness audio_noise fm_synth filter gain)
add_test(NAME test_audio_correctness COMMAND test_audio_correctness WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# GPU operator plugin for testing (solid color fill)
add_vivid_operator(gpu_fill_op      tests/operators/gpu_fill_op.cpp      EXTRA_LIBS webgpu)

# GPU operator integration tests (headless WebGPU, no window)
add_executable(test_gpu_operators
    tests/test_gpu_operators.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
)
target_include_directories(test_gpu_operators PRIVATE src)
target_link_libraries(test_gpu_operators PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_gpu_operators gpu_fill_op shape control_thumb_op)
add_test(NAME test_gpu_operators COMMAND test_gpu_operators WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# GPU output correctness tests (brightness, contrast, motion property assertions)
add_executable(test_gpu_correctness
    tests/test_gpu_correctness.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/output_analyzer.cpp
)
target_include_directories(test_gpu_correctness PRIVATE src)
target_link_libraries(test_gpu_correctness PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_gpu_correctness gpu_fill_op shape noise)
add_test(NAME test_gpu_correctness COMMAND test_gpu_correctness WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Per-operator smoke sweep — discovers all dylibs and validates load/desc/process/boundary
add_executable(test_operator_sweep
    tests/test_operator_sweep.cpp
    src/runtime/operator_loader.cpp
)
target_include_directories(test_operator_sweep PRIVATE src)
target_link_libraries(test_operator_sweep PRIVATE vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_test(NAME test_operator_sweep COMMAND test_operator_sweep WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_operator_sweep PROPERTIES LABELS "HEADLESS_SMOKE" TIMEOUT 120)

# --- Sequencer operator tests (from vivid-sequencers) ---
add_executable(test_arpeggiator_patterns tests/test_arpeggiator_patterns.cpp)
target_include_directories(test_arpeggiator_patterns PRIVATE
    src ${CMAKE_SOURCE_DIR}/operators/shared/sequencer)
target_link_libraries(test_arpeggiator_patterns PRIVATE vivid_operator_api)
add_test(NAME test_arpeggiator_patterns COMMAND test_arpeggiator_patterns)

add_executable(test_state_machine tests/test_state_machine.cpp)
target_include_directories(test_state_machine PRIVATE
    src ${CMAKE_SOURCE_DIR}/operators/shared/sequencer)
target_link_libraries(test_state_machine PRIVATE vivid_operator_api)
add_test(NAME test_state_machine COMMAND test_state_machine)

# 3D tests are in the vivid-3d package (see ../vivid-3d/)

# Cross-cadence spread integration test
add_executable(test_cross_cadence_spread
    tests/test_cross_cadence_spread.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
)
target_include_directories(test_cross_cadence_spread PRIVATE src)
target_link_libraries(test_cross_cadence_spread PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_cross_cadence_spread spread_source_op audio_spread_op)
add_test(NAME test_cross_cadence_spread COMMAND test_cross_cadence_spread
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Spread broadcast/wrap test
add_executable(test_spread_broadcast
    tests/test_spread_broadcast.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
)
target_include_directories(test_spread_broadcast PRIVATE src)
target_link_libraries(test_spread_broadcast PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_spread_broadcast spread_source_op spread_sink_op lane_smooth_op)
add_test(NAME test_spread_broadcast COMMAND test_spread_broadcast WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Lane metadata runtime test
configure_file(tests/graphs/test_lane_metadata.json ${CMAKE_BINARY_DIR}/test_lane_metadata.json COPYONLY)
add_executable(test_lane_metadata
    tests/test_lane_metadata.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
)
target_include_directories(test_lane_metadata PRIVATE src)
target_link_libraries(test_lane_metadata PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_lane_metadata spread_source_op lane_metadata_op)
add_test(NAME test_lane_metadata COMMAND test_lane_metadata WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# ChildOp<T> composition unit test (no plugins, no runtime, logic only).
# Non-header-only embeddables have separate loader regressions in
# test_operator_loader; this test uses stubs so it stays focused on ChildOp
# call semantics and persistent state behavior.
add_executable(test_child_op tests/test_child_op.cpp tests/stubs/smooth_stubs.cpp)
target_include_directories(test_child_op PRIVATE src tests ${CMAKE_SOURCE_DIR}/operators)
target_link_libraries(test_child_op PRIVATE vivid_operator_api)
add_test(NAME test_child_op COMMAND test_child_op WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# SIGNAL port tests (type compat, per-sample LFO/Envelope, auto-extraction, wire routing)
add_executable(test_signal_port
    tests/test_signal_port.cpp
    tests/stubs/envelope_stubs.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/runtime_api.cpp
    src/runtime/system_midi.cpp
)
target_include_directories(test_signal_port PRIVATE src tests ${CMAKE_SOURCE_DIR}/operators)
target_link_libraries(test_signal_port PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_signal_port lfo audio_float_cv_op test_op_v1)
add_test(NAME test_signal_port COMMAND test_signal_port WORKING_DIRECTORY ${CMAKE_BINARY_DIR})



# Copy test graphs to build directory
configure_file(tests/graphs/test_reload.json ${CMAKE_BINARY_DIR}/test_reload.json COPYONLY)
configure_file(tests/graphs/test_runtime_api.json ${CMAKE_BINARY_DIR}/test_runtime_api.json COPYONLY)
configure_file(tests/graphs/test_audio_engine.json ${CMAKE_BINARY_DIR}/test_audio_engine.json COPYONLY)
configure_file(tests/graphs/test_audio_robustness.json ${CMAKE_BINARY_DIR}/test_audio_robustness.json COPYONLY)
configure_file(tests/graphs/test_spread_broadcast.json ${CMAKE_BINARY_DIR}/test_spread_broadcast.json COPYONLY)
configure_file(tests/graphs/test_cross_cadence_spread.json ${CMAKE_BINARY_DIR}/test_cross_cadence_spread.json COPYONLY)
configure_file(tests/graphs/test_mixed_runtime_stability.json ${CMAKE_BINARY_DIR}/test_mixed_runtime_stability.json COPYONLY)
configure_file(tests/graphs/test_package_stress.json ${CMAKE_BINARY_DIR}/test_package_stress.json COPYONLY)

add_custom_target(phase6_stress
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R
            "test_runtime_stress|test_hot_reload_stress|test_package_stress|test_mixed_runtime_stability"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS test_runtime_stress test_hot_reload_stress test_package_stress test_mixed_runtime_stability
)

add_custom_target(phase6_soak
    COMMAND $<TARGET_FILE:test_mixed_runtime_stability> ${CMAKE_BINARY_DIR} soak
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS test_mixed_runtime_stability
)

# Demo graph smoke test (loads every demo graph, builds runtime, ticks 5 frames)
add_executable(test_demo_graphs
    tests/test_demo_graphs.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/builtin_operators.cpp
)
target_include_directories(test_demo_graphs PRIVATE src)
target_link_libraries(test_demo_graphs PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_demo_graphs
    # control
    lfo clock math envelope midi_input fft_analysis
    logic gate smooth
    stack alternate
    modulated_gain
    # gpu (compiled plugins — WGSL filter presets are loaded from filters/)
    noise shape composite
    bloom feedback movie_file_in texture_analysis
    # audio
    oscillator gain
    reverb delay bitcrush distortion filter audio_noise mixer
    movie_file_audio
    webcam_in time_machine
)
add_test(NAME test_demo_graphs
    COMMAND test_demo_graphs ${CMAKE_BINARY_DIR}/graphs
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_demo_graphs PROPERTIES LABELS "HEADLESS_SMOKE" TIMEOUT 300)

# Media-headless integration test — MovieLoaded/AVFoundation chains
# with bounded timeout to prevent CI hangs. See docs/test_stabilization.md step 4.
add_executable(test_media_headless
    tests/test_media_headless.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/builtin_operators.cpp
)
target_include_directories(test_media_headless PRIVATE src)
target_link_libraries(test_media_headless PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_media_headless
    movie_file_in movie_file_audio
    lfo noise composite bloom feedback
)
add_test(NAME test_media_headless
    COMMAND test_media_headless ${CMAKE_BINARY_DIR}/graphs
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_media_headless PROPERTIES
    LABELS "MEDIA_HEADLESS"
    TIMEOUT 60)

# Audio-domain sequencer integration test
add_executable(test_audio_cadence_sequencer
    tests/test_audio_cadence_sequencer.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/builtin_operators.cpp
)
target_include_directories(test_audio_cadence_sequencer PRIVATE src)
target_link_libraries(test_audio_cadence_sequencer PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_audio_cadence_sequencer clock oscillator gain)
add_test(NAME test_audio_cadence_sequencer
    COMMAND test_audio_cadence_sequencer ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_audio_cadence_sequencer PROPERTIES LABELS "STABILITY")

# Cadence inference integration test
add_executable(test_cadence_inference
    tests/test_cadence_inference.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/runtime_api.cpp
    src/runtime/package_manager.cpp
    src/runtime/operator_creator.cpp
    src/runtime/capture_coordinator.cpp
)
target_include_directories(test_cadence_inference PRIVATE src)
target_link_libraries(test_cadence_inference PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_cadence_inference clock oscillator lfo gain audio_test_op)
add_test(NAME test_cadence_inference
    COMMAND test_cadence_inference ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_cadence_inference PROPERTIES LABELS "STABILITY")

# PackageCompiler unit test (compiles a mock package operator)
add_executable(test_package_compiler
    tests/test_package_compiler.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_compiler PRIVATE src)
target_link_libraries(test_package_compiler PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_compiler
    COMMAND test_package_compiler ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_compiler PROPERTIES LABELS "PACKAGE")

# PackageCatalog unit test (JSON parsing, cache, merge)
add_executable(test_package_catalog
    tests/test_package_catalog.cpp
    src/runtime/package_catalog.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_catalog PRIVATE src)
target_link_libraries(test_package_catalog PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_catalog
    COMMAND test_package_catalog
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_catalog PROPERTIES LABELS "PACKAGE")

# PackageManager lifecycle test (install/list/uninstall with local path)
add_executable(test_package_manager
    tests/test_package_manager.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_manager PRIVATE src)
target_link_libraries(test_package_manager PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_manager
    COMMAND test_package_manager ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_manager PROPERTIES LABELS "PACKAGE")

# Package scope resolver test (deterministic multi-scope precedence/conflicts)
add_executable(test_package_scope_resolver
    tests/test_package_scope_resolver.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_scope_resolver PRIVATE src)
target_link_libraries(test_package_scope_resolver PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_scope_resolver
    COMMAND test_package_scope_resolver ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_scope_resolver PROPERTIES LABELS "PACKAGE")

# Package scope + registry integration test (winner package only)
add_executable(test_package_scope_registry
    tests/test_package_scope_registry.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_scope_registry PRIVATE src)
target_link_libraries(test_package_scope_registry PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_scope_registry
    COMMAND test_package_scope_registry ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_scope_registry PROPERTIES LABELS "PACKAGE")

# Package scaffolder unit test (template copy + placeholder replacement)
add_executable(test_package_scaffolder
    tests/test_package_scaffolder.cpp
    src/runtime/package_scaffolder.cpp
)
target_include_directories(test_package_scaffolder PRIVATE src)
add_test(NAME test_package_scaffolder
    COMMAND test_package_scaffolder ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_scaffolder PROPERTIES LABELS "PACKAGE")

# PackageManager update metadata logic test (semantic version + compatibility)
add_executable(test_package_update_logic
    tests/test_package_update_logic.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_update_logic PRIVATE src)
target_link_libraries(test_package_update_logic PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_update_logic
    COMMAND test_package_update_logic
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_update_logic PROPERTIES LABELS "PACKAGE")

# Core app update metadata parsing test (appcast + semver compare)
add_executable(test_app_update_manager
    tests/test_app_update_manager.cpp
    src/runtime/app_update_manager.cpp
)
target_include_directories(test_app_update_manager PRIVATE src)
target_link_libraries(test_app_update_manager PRIVATE vivid_operator_api webgpu)
add_test(NAME test_app_update_manager
    COMMAND test_app_update_manager
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_app_update_manager PROPERTIES LABELS "PACKAGE")

# PackageTestRunner test (graph + C++ test execution for packages)
add_executable(test_package_test_runner
    tests/test_package_test_runner.cpp
    src/runtime/package_test_runner.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_test_runner PRIVATE src)
target_link_libraries(test_package_test_runner PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_package_test_runner control_pass_op)
add_test(NAME test_package_test_runner
    COMMAND test_package_test_runner ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_test_runner PROPERTIES LABELS "PACKAGE")

add_executable(test_package_contract_ecosystem
    tests/test_package_contract_ecosystem.cpp
    src/runtime/package_test_runner.cpp
    src/runtime/package_manager.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/platform.cpp
)
target_include_directories(test_package_contract_ecosystem PRIVATE src)
target_link_libraries(test_package_contract_ecosystem PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_package_contract_ecosystem control_pass_op)
add_test(NAME test_package_contract_ecosystem
    COMMAND test_package_contract_ecosystem ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_contract_ecosystem PROPERTIES LABELS "PACKAGE")

# CaptureCoordinator unit test (non-GPU paths only)
add_executable(test_capture_coordinator
    tests/test_capture_coordinator.cpp
    src/runtime/capture_coordinator.cpp
    src/runtime/output_analyzer.cpp
    src/runtime/av_exporter.mm
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/screenshot.cpp
    src/runtime/runtime_api.cpp
    src/runtime/undo_manager.cpp
)
target_include_directories(test_capture_coordinator PRIVATE src)
target_link_libraries(test_capture_coordinator PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu miniaudio stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
add_test(NAME test_capture_coordinator COMMAND test_capture_coordinator
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# OutputAnalyzer unit test (pure analysis functions, no GPU/audio)
add_executable(test_output_analyzer
    tests/test_output_analyzer.cpp
    src/runtime/output_analyzer.cpp
)
target_include_directories(test_output_analyzer PRIVATE src)
target_link_libraries(test_output_analyzer PRIVATE vivid_operator_api)
add_test(NAME test_output_analyzer COMMAND test_output_analyzer
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# SystemMidiListener unit test (no hardware needed)
add_executable(test_midi
    tests/test_midi.cpp
    src/runtime/system_midi.cpp
)
target_include_directories(test_midi PRIVATE src)
target_link_libraries(test_midi PRIVATE rtmidi)
add_test(NAME test_midi COMMAND test_midi WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# FileWatcher unit tests (kqueue, no runtime, no GPU)
add_executable(test_file_watcher
    tests/test_file_watcher.cpp
    src/runtime/file_watcher.cpp
)
target_include_directories(test_file_watcher PRIVATE src tests)
add_test(NAME test_file_watcher COMMAND test_file_watcher WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# HotReloader queueing unit tests (no compiler invocation; package callback path)
add_executable(test_hot_reloader_queue
    tests/test_hot_reloader_queue.cpp
    src/runtime/hot_reload.cpp
    src/runtime/tool_discovery.cpp
)
target_include_directories(test_hot_reloader_queue PRIVATE src tests)
add_test(NAME test_hot_reloader_queue COMMAND test_hot_reloader_queue WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# path_util unit tests (header-only, no extra deps)
add_executable(test_path_util
    tests/test_path_util.cpp
)
target_include_directories(test_path_util PRIVATE src tests)
add_test(NAME test_path_util COMMAND test_path_util WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# HAP helper unit tests (FourCC and format mapping)
add_executable(test_hap_codec
    tests/test_hap_codec.cpp
)
target_include_directories(test_hap_codec PRIVATE src tests ${CMAKE_SOURCE_DIR})
add_test(NAME test_hap_codec COMMAND test_hap_codec WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_movie_decode_upload
    tests/test_movie_decode_upload.cpp
    operators/shared/movie_decode/texture_upload.cpp
)
target_include_directories(test_movie_decode_upload PRIVATE src tests ${CMAKE_SOURCE_DIR})
target_link_libraries(test_movie_decode_upload PRIVATE webgpu)
add_test(NAME test_movie_decode_upload COMMAND test_movie_decode_upload WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_movie_load_generation
    tests/test_movie_load_generation.cpp
)
target_include_directories(test_movie_load_generation PRIVATE src tests ${CMAKE_SOURCE_DIR})
add_test(NAME test_movie_load_generation COMMAND test_movie_load_generation WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_string_ports
    tests/test_string_ports.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
)
target_include_directories(test_string_ports PRIVATE src tests)
target_link_libraries(test_string_ports PRIVATE vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_string_ports test_op_v1 string_source_op string_sink_op)
add_test(NAME test_string_ports COMMAND test_string_ports WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_movie_load_async
    tests/test_movie_load_async.cpp
)
target_include_directories(test_movie_load_async PRIVATE src tests ${CMAKE_SOURCE_DIR})
add_test(NAME test_movie_load_async COMMAND test_movie_load_async WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_movie_av_sync
    tests/test_movie_av_sync.cpp
)
target_include_directories(test_movie_av_sync PRIVATE src tests ${CMAKE_SOURCE_DIR})
add_test(NAME test_movie_av_sync COMMAND test_movie_av_sync WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_port_type_registry
    tests/test_port_type_registry.cpp
    src/runtime/port_type_registry.cpp
)
target_include_directories(test_port_type_registry PRIVATE src tests ${CMAKE_SOURCE_DIR})
add_test(NAME test_port_type_registry
    COMMAND test_port_type_registry
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if(APPLE)
    add_executable(test_movie_decode_route
        tests/test_movie_decode_route.cpp
        operators/shared/movie_decode/decoder_factory.cpp
        operators/shared/movie_decode/codec_probe.mm
        operators/shared/movie_decode/avf_decoder.mm
        operators/shared/movie_decode/hap_decoder.mm
        deps/hap/hap.c
    )
    set_source_files_properties(
        operators/shared/movie_decode/codec_probe.mm
        operators/shared/movie_decode/avf_decoder.mm
        operators/shared/movie_decode/hap_decoder.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_include_directories(test_movie_decode_route PRIVATE src tests ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/deps/hap)
    target_link_libraries(test_movie_decode_route PRIVATE snappy
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
        "-framework Foundation" "-framework QuartzCore")
    add_test(NAME test_movie_decode_route
        COMMAND test_movie_decode_route ${CMAKE_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

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

add_executable(test_tool_discovery
    tests/test_tool_discovery.cpp
    src/runtime/tool_discovery.cpp
)
target_include_directories(test_tool_discovery PRIVATE src tests)
add_test(NAME test_tool_discovery COMMAND test_tool_discovery WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_graph_compiler_init
    tests/test_graph_compiler_init.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/port_type_registry.cpp
)
target_include_directories(test_graph_compiler_init PRIVATE src)
target_link_libraries(test_graph_compiler_init PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_graph_compiler_init COMMAND test_graph_compiler_init WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_frame_executor_queries
    tests/test_frame_executor_queries.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
)
target_include_directories(test_frame_executor_queries PRIVATE src)
target_link_libraries(test_frame_executor_queries PRIVATE vivid_operator_api webgpu)
add_test(NAME test_frame_executor_queries COMMAND test_frame_executor_queries WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_cadence_bridge
    tests/test_cadence_bridge.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/port_type_registry.cpp
)
target_include_directories(test_cadence_bridge PRIVATE src)
target_link_libraries(test_cadence_bridge PRIVATE vivid_operator_api webgpu)
add_test(NAME test_cadence_bridge COMMAND test_cadence_bridge WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_subgraph_module
    tests/test_subgraph_module.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph.cpp
)
target_include_directories(test_subgraph_module PRIVATE src)
target_link_libraries(test_subgraph_module PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_subgraph_module COMMAND test_subgraph_module WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_graph_compiler
    tests/test_graph_compiler.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/port_type_registry.cpp
)
target_include_directories(test_graph_compiler PRIVATE src)
target_link_libraries(test_graph_compiler PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_graph_compiler lfo gain spread_source_op lane_metadata_audio_op)
add_test(NAME test_graph_compiler
    COMMAND test_graph_compiler ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_propagation
    tests/test_lane_propagation.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/port_type_registry.cpp
)
target_include_directories(test_lane_propagation PRIVATE src)
target_link_libraries(test_lane_propagation PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_lane_propagation COMMAND test_lane_propagation WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_state tests/test_lane_state.cpp)
target_include_directories(test_lane_state PRIVATE src)
target_link_libraries(test_lane_state PRIVATE vivid_operator_api)
add_test(NAME test_lane_state COMMAND test_lane_state WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Undo coverage across roadmap mutation types via RuntimeCommandSink + RuntimeAPI
add_executable(test_undo_mutation_types
    tests/test_undo_mutation_types.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/runtime_api.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/package_manager.cpp
    src/runtime/undo_manager.cpp
    src/runtime/operator_creator.cpp
    src/runtime/settings.cpp
    src/runtime/hot_reload.cpp
    src/runtime/capture_coordinator.cpp
    src/runtime/output_analyzer.cpp
    src/runtime/screenshot.cpp
    src/runtime/av_exporter.mm
    src/runtime/platform.cpp
    src/runtime/system_midi.cpp
)
target_include_directories(test_undo_mutation_types PRIVATE src tests)
target_link_libraries(test_undo_mutation_types PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
add_dependencies(test_undo_mutation_types test_op_v1)
add_test(NAME test_undo_mutation_types
    COMMAND test_undo_mutation_types ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})


# Team workflow regression: project package isolation (Cases A/C: unit; Case B: dylib staging)
add_executable(test_team_workflow_regression
    tests/test_team_workflow_regression.cpp
    src/runtime/operator_loader.cpp
    src/runtime/operator_registry.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/graph.cpp
    src/runtime/runtime_core.cpp
    src/runtime/subgraph_module.cpp
    src/runtime/graph_compiler.cpp
    src/runtime/cadence_bridge.cpp
    src/runtime/frame_executor.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/audio_engine.cpp
    src/runtime/audio_executor.cpp
    src/runtime/runtime_api.cpp
    src/runtime/package_compiler.cpp
    src/runtime/tool_discovery.cpp
    src/runtime/package_manager.cpp
    src/runtime/undo_manager.cpp
    src/runtime/operator_creator.cpp
    src/runtime/settings.cpp
    src/runtime/hot_reload.cpp
    src/runtime/capture_coordinator.cpp
    src/runtime/output_analyzer.cpp
    src/runtime/screenshot.cpp
    src/runtime/av_exporter.mm
    src/runtime/platform.cpp
    src/runtime/system_midi.cpp
)
target_include_directories(test_team_workflow_regression PRIVATE src tests)
target_link_libraries(test_team_workflow_regression PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
add_dependencies(test_team_workflow_regression test_op_v1)
add_test(NAME test_team_workflow_regression
    COMMAND test_team_workflow_regression ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# builtin_operators unit tests (no dylib, no GPU)
add_executable(test_builtin_operators
    tests/test_builtin_operators.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/port_type_registry.cpp
    src/runtime/operator_registry.cpp
    src/runtime/operator_loader.cpp
    src/runtime/wgsl_header_parser.cpp
)
target_include_directories(test_builtin_operators PRIVATE src tests)
target_link_libraries(test_builtin_operators PRIVATE vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_test(NAME test_builtin_operators COMMAND test_builtin_operators WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# OperatorInfoCache unit tests (uses builtin operators as fixtures, no dylib)
add_executable(test_operator_info_cache
    tests/test_operator_info_cache.cpp
    src/runtime/operator_registry.cpp
    src/runtime/operator_loader.cpp
    src/runtime/builtin_operators.cpp
    src/runtime/wgsl_header_parser.cpp
    src/runtime/port_type_registry.cpp
)
target_include_directories(test_operator_info_cache PRIVATE src tests)
target_link_libraries(test_operator_info_cache PRIVATE vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_test(NAME test_operator_info_cache COMMAND test_operator_info_cache WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Path Animate unit tests (pure math, no runtime dependencies)
add_executable(test_path_animate tests/test_path_animate.cpp)
target_include_directories(test_path_animate PRIVATE src)
add_test(NAME test_path_animate COMMAND test_path_animate WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# i18n module tests (load, get, get_plural, fallbacks)
add_executable(test_i18n
    tests/test_i18n.cpp
    src/ui/i18n.cpp
)
target_include_directories(test_i18n PRIVATE src)
target_link_libraries(test_i18n PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME test_i18n COMMAND test_i18n WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Semantic tag vocabulary validation (script-only, no binary)
add_test(NAME test_semantic_tags
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/validate_semantic_tags.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# --- reset target: wipe user data directories (opt-in) ---
if(APPLE)
    set(VIVID_CONFIG_DIR "$ENV{HOME}/Library/Application Support/Vivid")
elseif(WIN32)
    set(VIVID_CONFIG_DIR "$ENV{APPDATA}/Vivid")
else()
    if(DEFINED ENV{XDG_CONFIG_HOME})
        set(VIVID_CONFIG_DIR "$ENV{XDG_CONFIG_HOME}/vivid")
    else()
        set(VIVID_CONFIG_DIR "$ENV{HOME}/.config/vivid")
    endif()
endif()
set(VIVID_WORKSPACE_DIR "$ENV{HOME}/Documents/Vivid")

add_custom_target(reset
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${VIVID_CONFIG_DIR}" "${VIVID_WORKSPACE_DIR}"
    COMMENT "Removing Vivid user data: ${VIVID_CONFIG_DIR} and ${VIVID_WORKSPACE_DIR}"
    VERBATIM
)
