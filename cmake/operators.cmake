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
target_link_libraries(vivid_composable_ops PUBLIC vivid_operator_api webgpu)

function(add_vivid_embeddable_composable_support name source)
    target_sources(vivid_composable_ops PRIVATE ${source})
endfunction()

add_vivid_embeddable_composable_support(Smooth   operators/control/smooth/smooth_composable.cpp)
add_vivid_embeddable_composable_support(Envelope operators/control/envelope/envelope_composable.cpp)
target_sources(vivid_composable_ops PRIVATE operators/control/clock/clock.cpp)
target_sources(vivid_composable_ops PRIVATE operators/control/euclidean/euclidean.cpp)
add_vivid_embeddable_composable_support(LFO      operators/control/lfo/lfo.cpp)

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
add_vivid_operator(lfo_fr         operators/control/lfo/lfo_fr.cpp
                   FACTORY_PRESETS operators/control/lfo/factory_presets.json
                   EXTRA_LIBS webgpu)
target_sources(lfo_fr PRIVATE operators/control/lfo/lfo.cpp)
add_vivid_operator(lfo_au         operators/control/lfo/lfo_au.cpp
                   EXTRA_LIBS webgpu)
target_sources(lfo_au PRIVATE operators/control/lfo/lfo.cpp)
add_vivid_operator(clock_fr       operators/control/clock/clock_fr.cpp       EXTRA_LIBS webgpu vivid_composable_ops)
add_vivid_operator(clock_au       operators/control/clock/clock_au.cpp       EXTRA_LIBS webgpu vivid_composable_ops)
add_vivid_operator(math           operators/control/math/math.cpp           EXTRA_LIBS webgpu)
add_vivid_operator(envelope_fr    operators/control/envelope/envelope_fr.cpp
                   FACTORY_PRESETS operators/control/envelope/factory_presets.json
                   EXTRA_LIBS webgpu vivid_composable_ops)
target_sources(envelope_fr PRIVATE operators/control/envelope/envelope.cpp)
add_vivid_operator(envelope_au    operators/control/envelope/envelope_au.cpp
                   EXTRA_LIBS webgpu vivid_composable_ops)
target_sources(envelope_au PRIVATE operators/control/envelope/envelope.cpp)
add_vivid_operator(midi_input     operators/control/midi_input/midi_input.cpp     EXTRA_LIBS rtmidi)
add_vivid_operator(fft_analysis   operators/control/fft_analysis/fft_analysis.cpp)
add_vivid_operator(logic             operators/control/logic/logic.cpp             EXTRA_LIBS webgpu)
add_vivid_operator(gate_fr           operators/control/gate/gate_fr.cpp           EXTRA_LIBS webgpu)
target_sources(gate_fr PRIVATE operators/control/gate/gate.cpp)
add_vivid_operator(gate_au           operators/control/gate/gate_au.cpp           EXTRA_LIBS webgpu)
target_sources(gate_au PRIVATE operators/control/gate/gate.cpp)
add_vivid_operator(smooth_fr          operators/control/smooth/smooth_fr.cpp          EXTRA_LIBS webgpu)
target_sources(smooth_fr PRIVATE operators/control/smooth/smooth.cpp)
add_vivid_operator(smooth_au          operators/control/smooth/smooth_au.cpp          EXTRA_LIBS webgpu)
add_vivid_operator(stack             operators/control/stack/stack.cpp)
add_vivid_operator(repeat            operators/control/repeat/repeat.cpp)
add_vivid_operator(tile              operators/control/tile/tile.cpp)
add_vivid_operator(select            operators/control/select/select.cpp)
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
add_vivid_operator(step_counter_fr   operators/control/step_counter/step_counter_fr.cpp)
add_vivid_operator(step_counter_au   operators/control/step_counter/step_counter_au.cpp)
add_vivid_operator(path_animate      operators/control/path_animate/path_animate.cpp)
add_vivid_operator(sample_hold_fr    operators/control/sample_hold/sample_hold_fr.cpp    EXTRA_LIBS webgpu)
target_sources(sample_hold_fr PRIVATE operators/control/sample_hold/sample_hold.cpp)
add_vivid_operator(sample_hold_au    operators/control/sample_hold/sample_hold_au.cpp    EXTRA_LIBS webgpu)
target_sources(sample_hold_au PRIVATE operators/control/sample_hold/sample_hold.cpp)
add_vivid_operator(quantizer_fr      operators/control/quantizer/quantizer_fr.cpp)
add_vivid_operator(quantizer_au      operators/control/quantizer/quantizer_au.cpp)
add_vivid_operator(macro             operators/control/macro/macro.cpp)
add_vivid_operator(mseg_fr           operators/control/mseg/mseg_fr.cpp
                   FACTORY_PRESETS operators/control/mseg/factory_presets.json
                   EXTRA_LIBS webgpu)
target_sources(mseg_fr PRIVATE operators/control/mseg/mseg.cpp)
add_vivid_operator(mseg_au           operators/control/mseg/mseg_au.cpp
                   EXTRA_LIBS webgpu)
target_sources(mseg_au PRIVATE operators/control/mseg/mseg.cpp)
add_vivid_operator(step_seq_fr       operators/control/step_seq/step_seq_fr.cpp
                   FACTORY_PRESETS operators/control/step_seq/factory_presets.json)
target_sources(step_seq_fr PRIVATE operators/control/step_seq/step_seq.cpp)
add_vivid_operator(step_seq_au       operators/control/step_seq/step_seq_au.cpp)
target_sources(step_seq_au PRIVATE operators/control/step_seq/step_seq.cpp)

# --- Sequencer operators (from vivid-sequencers) ---
add_vivid_operator(sequencer_fr       operators/control/sequencer/sequencer_fr.cpp)
add_vivid_operator(sequencer_au       operators/control/sequencer/sequencer_au.cpp)
add_vivid_operator(drum_sequencer_fr  operators/control/drum_sequencer/drum_sequencer_fr.cpp  EXTRA_LIBS webgpu)
add_vivid_operator(drum_sequencer_au  operators/control/drum_sequencer/drum_sequencer_au.cpp  EXTRA_LIBS webgpu)
target_sources(drum_sequencer_fr PRIVATE
    operators/control/drum_sequencer/drum_sequencer_core.cpp
    operators/control/drum_sequencer/drum_sequencer_inspector.cpp)
target_sources(drum_sequencer_au PRIVATE
    operators/control/drum_sequencer/drum_sequencer_core.cpp
    operators/control/drum_sequencer/drum_sequencer_inspector.cpp)
add_vivid_operator(pattern_seq_fr     operators/control/pattern_seq/pattern_seq_fr.cpp)
add_vivid_operator(pattern_seq_au     operators/control/pattern_seq/pattern_seq_au.cpp)
add_vivid_operator(note_pattern_fr    operators/control/note_pattern/note_pattern_fr.cpp    EXTRA_LIBS webgpu)
add_vivid_operator(note_pattern_au    operators/control/note_pattern/note_pattern_au.cpp    EXTRA_LIBS webgpu)
add_vivid_operator(note_duration      operators/control/note_duration/note_duration.cpp)
add_vivid_operator(arpeggiator_fr     operators/control/arpeggiator/arpeggiator_fr.cpp     EXTRA_LIBS webgpu)
add_vivid_operator(arpeggiator_au     operators/control/arpeggiator/arpeggiator_au.cpp     EXTRA_LIBS webgpu)
add_vivid_operator(chord_progression_fr operators/control/chord_progression/chord_progression_fr.cpp EXTRA_LIBS webgpu)
add_vivid_operator(chord_progression_au operators/control/chord_progression/chord_progression_au.cpp EXTRA_LIBS webgpu)
add_vivid_operator(state_machine      operators/control/state_machine/state_machine.cpp)
add_vivid_operator(tracker_fr         operators/control/tracker/tracker_fr.cpp         EXTRA_LIBS webgpu)
add_vivid_operator(tracker_au         operators/control/tracker/tracker_au.cpp         EXTRA_LIBS webgpu)
target_sources(tracker_fr PRIVATE
    operators/control/tracker/tracker_core.cpp
    operators/control/tracker/tracker_inspector.cpp)
target_sources(tracker_au PRIVATE
    operators/control/tracker/tracker_core.cpp
    operators/control/tracker/tracker_inspector.cpp)
add_vivid_operator(euclidean_fr       operators/control/euclidean/euclidean_fr.cpp       EXTRA_LIBS webgpu vivid_composable_ops)
add_vivid_operator(euclidean_au       operators/control/euclidean/euclidean_au.cpp       EXTRA_LIBS webgpu vivid_composable_ops)
add_vivid_operator(pat_transform      operators/control/pat_transform/pat_transform.cpp)
add_vivid_operator(phase_to_midi_fr   operators/control/phase_to_midi/phase_to_midi_fr.cpp)
add_vivid_operator(phase_to_midi_au   operators/control/phase_to_midi/phase_to_midi_au.cpp)
add_vivid_operator(drum_kit_fr        operators/control/drum_kit/drum_kit_fr.cpp)
add_vivid_operator(drum_kit_au        operators/control/drum_kit/drum_kit_au.cpp)
foreach(_seq_op sequencer_fr sequencer_au drum_sequencer_fr drum_sequencer_au
        pattern_seq_fr pattern_seq_au note_pattern_fr note_pattern_au note_duration
        arpeggiator_fr arpeggiator_au chord_progression_fr chord_progression_au
        state_machine tracker_fr tracker_au euclidean_fr euclidean_au pat_transform
        phase_to_midi_fr phase_to_midi_au drum_kit_fr drum_kit_au)
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
add_vivid_operator(instanced_shapes      operators/gpu/instanced_shapes/instanced_shapes.cpp      EXTRA_LIBS webgpu vivid_composable_ops)
add_vivid_operator(flocking              operators/gpu/flocking/flocking.cpp                       EXTRA_LIBS webgpu vivid_composable_ops)
add_vivid_operator(trails                operators/gpu/trails/trails.cpp                           EXTRA_LIBS webgpu vivid_composable_ops)
add_vivid_operator(reaction_diffusion    operators/gpu/reaction_diffusion/reaction_diffusion.cpp   EXTRA_LIBS webgpu vivid_composable_ops)
add_vivid_operator(cellular_automata     operators/gpu/cellular_automata/cellular_automata.cpp     EXTRA_LIBS webgpu vivid_composable_ops)
add_vivid_operator(fluid                 operators/gpu/fluid/fluid.cpp                             EXTRA_LIBS webgpu vivid_composable_ops)
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
add_vivid_operator(oscillator     operators/audio/oscillator/oscillator.cpp     EXTRA_LIBS webgpu)
add_vivid_operator(gain           operators/audio/gain/gain.cpp           EXTRA_LIBS webgpu)
add_vivid_operator(reverb         operators/audio/reverb/reverb.cpp
                   FACTORY_PRESETS operators/audio/reverb/factory_presets.json)
add_vivid_operator(delay          operators/audio/delay/delay.cpp
                   FACTORY_PRESETS operators/audio/delay/factory_presets.json)
add_vivid_operator(bitcrush       operators/audio/bitcrush/bitcrush.cpp
                   FACTORY_PRESETS operators/audio/bitcrush/factory_presets.json)
add_vivid_operator(distortion     operators/audio/distortion/distortion.cpp
                   FACTORY_PRESETS operators/audio/distortion/factory_presets.json)
add_vivid_operator(filter         operators/audio/filter/filter.cpp         EXTRA_LIBS webgpu)
target_sources(filter PRIVATE operators/shared/filter_dsp/filter_dsp.cpp)
add_vivid_operator(dual_filter    operators/audio/dual_filter/dual_filter.cpp)
target_sources(dual_filter PRIVATE operators/shared/filter_dsp/filter_dsp.cpp)
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
