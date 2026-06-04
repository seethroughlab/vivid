# --- Operator API (header-only interface library) ---
add_library(vivid_operator_api INTERFACE)
target_include_directories(vivid_operator_api INTERFACE src deps/glfw/deps ${CMAKE_SOURCE_DIR}/operators)
# Allow operator dylibs to call back into runtime symbols (e.g. vivid_register_port_type)
# that live in the main executable. -undefined dynamic_lookup defers resolution to load time.
if(APPLE)
    target_link_options(vivid_operator_api INTERFACE -undefined dynamic_lookup)
endif()

# --- Embeddable operator support (for ChildOp<T> consumers) ---
# Embeddable operators must either be fully header-defined or register a
# support file here when they have out-of-line virtuals, thumbnail
# hooks, or other concrete definitions that ChildOp<T> consumers must link.
add_library(vivid_embeddable_op_support STATIC)
target_include_directories(vivid_embeddable_op_support PUBLIC ${CMAKE_SOURCE_DIR}/operators)
target_link_libraries(vivid_embeddable_op_support PUBLIC vivid_operator_api webgpu)

function(add_vivid_embeddable_op_support name source)
    target_sources(vivid_embeddable_op_support PRIVATE ${source})
endfunction()

add_vivid_embeddable_op_support(Smooth   operators/control/smooth/smooth_embeddable.cpp)
add_vivid_embeddable_op_support(Envelope operators/control/envelope/envelope_embeddable.cpp)
add_vivid_embeddable_op_support(LFO      operators/control/lfo/lfo_embeddable.cpp)
## NOTE: clock.cpp and euclidean.cpp were historically in the embeddable support
## library but have no ChildOp consumers. They now live as single-compilation-unit
## operators in their own dylibs only.

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
#
# Pass TEST_FIXTURE for operators that only exist to back tests: they are
# kept out of VIVID_OPERATOR_TARGETS (so vivid's bundle-populate POST_BUILD
# does not touch them), skip the per-target auto-copy-into-bundle
# POST_BUILD (which would otherwise race with vivid's codesign and break
# the bundle signature), and are omitted from the operator manifest used
# by standalone exports. Tests that need a fixture dylib in the bundle
# (currently only test_ui_screenshot_smoke) stage them explicitly and
# call vivid_codesign_bundle() to re-seal afterwards.
function(add_vivid_operator name source)
    cmake_parse_arguments(ARG "TEST_FIXTURE;CODEGEN" "FACTORY_PRESETS" "EXTRA_LIBS;EXTRA_CODEGEN_SOURCES" ${ARGN})
    set(_operator_sources ${source})
    if(ARG_CODEGEN)
        get_filename_component(_source_abs "${source}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        get_filename_component(_source_dir "${_source_abs}" DIRECTORY)
        get_filename_component(_source_stem "${_source_abs}" NAME_WE)
        get_filename_component(_source_ext "${_source_abs}" EXT)
        # Match source extension so ObjC++ sources generate ObjC++ registration files
        if("${_source_ext}" STREQUAL ".mm")
            set(_generated_cpp "${CMAKE_CURRENT_BINARY_DIR}/${name}_generated_registration.mm")
        else()
            set(_generated_cpp "${CMAKE_CURRENT_BINARY_DIR}/${name}_generated_registration.cpp")
        endif()
        set(_generated_uniforms "${CMAKE_CURRENT_BINARY_DIR}/${name}_generated_uniforms.h")
        set(_codegen_depends ${_source_abs} operator_codegen)
        set(_codegen_command
            operator_codegen
            --input ${_source_abs}
            --output ${_generated_cpp}
            --uniform-output ${_generated_uniforms}
        )
        set(_wgsl_source "${_source_dir}/${_source_stem}.wgsl")
        if(EXISTS "${_wgsl_source}")
            list(APPEND _codegen_command --wgsl ${_wgsl_source})
            list(APPEND _codegen_depends ${_wgsl_source})
        endif()
        set(_extra_compile_sources "")
        foreach(_extra_src ${ARG_EXTRA_CODEGEN_SOURCES})
            get_filename_component(_extra_src_abs "${_extra_src}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
            list(APPEND _codegen_command --extra-source ${_extra_src_abs})
            list(APPEND _codegen_depends ${_extra_src_abs})
            list(APPEND _extra_compile_sources "${_extra_src_abs}")
        endforeach()
        add_custom_command(
            OUTPUT ${_generated_cpp} ${_generated_uniforms}
            COMMAND ${_codegen_command}
            DEPENDS ${_codegen_depends}
            COMMENT "Generating operator registration for ${name}"
            VERBATIM
        )
        set(_operator_sources ${_generated_cpp})
    endif()
    add_library(${name} MODULE ${_operator_sources})
    if(_extra_compile_sources)
        target_sources(${name} PRIVATE ${_extra_compile_sources})
    endif()
    set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
    target_link_libraries(${name} PRIVATE vivid_operator_api ${ARG_EXTRA_LIBS})
    if(ARG_CODEGEN)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    endif()
    if(NOT ARG_TEST_FIXTURE)
        set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS ${name})
    endif()

    # Keep app bundle in sync when building individual operator targets.
    # Skipped for test fixtures: they race with vivid's POST_BUILD codesign
    # step and would invalidate the bundle seal.
    if(APPLE AND NOT ARG_TEST_FIXTURE)
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

    # Accumulate manifest entry (test fixtures are not real operators and
    # are not emitted for standalone export builds).
    if(NOT ARG_TEST_FIXTURE)
        # Filter out non-library targets from EXTRA_LIBS (keep only webgpu, rtmidi, etc.)
        set(_extra_libs "")
        foreach(_lib ${ARG_EXTRA_LIBS})
            # Skip interface/header-only targets that aren't real link deps
            if(NOT "${_lib}" STREQUAL "vivid_embeddable_op_support")
                list(APPEND _extra_libs "${_lib}")
            endif()
        endforeach()
        string(REPLACE ";" "\", \"" _extra_json "${_extra_libs}")
        if(_extra_libs)
            set(_extra_json "\"${_extra_json}\"")
        endif()
        set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
            "  \"${name}\": { \"sources\": [\"${source}\"], \"extra_libs\": [${_extra_json}] }")
    endif()
endfunction()

# Helper for operators that use add_library directly (cannot use add_vivid_operator).
# Sets VIVID_CODEGEN_OUTPUT_<name> and creates the operator_codegen custom command.
# Call before add_library(), then list ${VIVID_CODEGEN_OUTPUT_<name>} as a source.
macro(vivid_codegen_for name source)
    get_filename_component(_vcf_abs "${source}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    get_filename_component(_vcf_ext "${_vcf_abs}" EXT)
    if("${_vcf_ext}" STREQUAL ".mm")
        set(VIVID_CODEGEN_OUTPUT_${name} "${CMAKE_CURRENT_BINARY_DIR}/${name}_generated_registration.mm")
    else()
        set(VIVID_CODEGEN_OUTPUT_${name} "${CMAKE_CURRENT_BINARY_DIR}/${name}_generated_registration.cpp")
    endif()
    set(_vcf_uniforms "${CMAKE_CURRENT_BINARY_DIR}/${name}_generated_uniforms.h")
    add_custom_command(
        OUTPUT "${VIVID_CODEGEN_OUTPUT_${name}}" "${_vcf_uniforms}"
        COMMAND operator_codegen
            --input "${_vcf_abs}"
            --output "${VIVID_CODEGEN_OUTPUT_${name}}"
            --uniform-output "${_vcf_uniforms}"
        DEPENDS "${_vcf_abs}" operator_codegen
        COMMENT "Generating operator registration for ${name}"
        VERBATIM
    )
endmacro()

function(vivid_enable_audio_kernels name)
    if(VIVID_ENABLE_HIGHWAY)
        target_link_libraries(${name} PRIVATE hwy)
        target_compile_definitions(${name} PRIVATE VIVID_HAS_HIGHWAY=1)
    endif()
    if(APPLE AND VIVID_ENABLE_ACCELERATE)
        target_link_libraries(${name} PRIVATE "-framework Accelerate")
        target_compile_definitions(${name} PRIVATE VIVID_HAS_ACCELERATE=1)
    endif()
endfunction()

# --- Control operator plugins ---
add_vivid_operator(lfo            operators/control/lfo/lfo.cpp CODEGEN
                   FACTORY_PRESETS operators/control/lfo/factory_presets.json
                   EXTRA_LIBS webgpu vivid_embeddable_op_support)
add_vivid_operator(clock          operators/control/clock/clock.cpp CODEGEN          EXTRA_LIBS webgpu vivid_embeddable_op_support)
add_vivid_operator(math           operators/control/math/math.cpp CODEGEN           EXTRA_LIBS webgpu)
add_vivid_operator(colormap       operators/control/colormap/colormap.cpp CODEGEN   EXTRA_LIBS webgpu)
add_vivid_operator(envelope       operators/control/envelope/envelope.cpp CODEGEN
                   FACTORY_PRESETS operators/control/envelope/factory_presets.json
                   EXTRA_LIBS webgpu vivid_embeddable_op_support)
add_vivid_operator(midi_input     operators/control/midi_input/midi_input.cpp CODEGEN     EXTRA_LIBS rtmidi)
add_vivid_operator(fft_analysis   operators/control/fft_analysis/fft_analysis.cpp CODEGEN)
add_vivid_operator(logic             operators/control/logic/logic.cpp CODEGEN             EXTRA_LIBS webgpu)
add_vivid_operator(gate              operators/control/gate/gate.cpp CODEGEN           EXTRA_LIBS webgpu)
add_vivid_operator(smooth            operators/control/smooth/smooth.cpp CODEGEN       EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/control/smooth/factory_presets.json)
add_vivid_operator(stack             operators/control/stack/stack.cpp CODEGEN)
add_vivid_operator(repeat            operators/control/repeat/repeat.cpp             CODEGEN)
add_vivid_operator(spread_noise      operators/control/spread_noise/spread_noise.cpp CODEGEN      EXTRA_LIBS webgpu)

# --- 2D drawable-pipeline operators (Phase E) ---
add_vivid_operator(drawable_merge    operators/gpu/drawable_merge/drawable_merge.cpp CODEGEN    EXTRA_LIBS webgpu)
add_vivid_operator(shape_2d          operators/gpu/shape_2d/shape_2d.cpp CODEGEN                EXTRA_LIBS webgpu)
add_vivid_operator(sprite_2d         operators/gpu/sprite_2d/sprite_2d.cpp CODEGEN              EXTRA_LIBS webgpu)
add_vivid_operator(render_2d         operators/gpu/render_2d/render_2d.cpp CODEGEN              EXTRA_LIBS webgpu)
add_vivid_operator(instance_grid_2d  operators/gpu/instance_grid_2d/instance_grid_2d.cpp CODEGEN EXTRA_LIBS webgpu)
add_vivid_operator(instancer_2d      operators/gpu/instancer_2d/instancer_2d.cpp CODEGEN        EXTRA_LIBS webgpu)
add_vivid_operator(transform_2d      operators/gpu/transform_2d/transform_2d.cpp CODEGEN        EXTRA_LIBS webgpu)
add_vivid_operator(particles_2d      operators/gpu/particles_2d/particles_2d.cpp CODEGEN        EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/particles_2d/factory_presets.json)
add_vivid_operator(flocking_2d       operators/gpu/flocking_2d/flocking_2d.cpp CODEGEN          EXTRA_LIBS webgpu)
add_vivid_operator(instance_noise_2d operators/gpu/instance_noise_2d/instance_noise_2d.cpp CODEGEN EXTRA_LIBS webgpu)
add_vivid_operator(instances_from_lanes_2d operators/gpu/instances_from_lanes_2d/instances_from_lanes_2d.cpp CODEGEN EXTRA_LIBS webgpu)
add_vivid_operator(text_2d             operators/gpu/text_2d/text_2d.cpp CODEGEN                 EXTRA_LIBS webgpu stb_truetype)
add_vivid_operator(tile              operators/control/tile/tile.cpp CODEGEN)
add_vivid_operator(select            operators/control/select/select.cpp              CODEGEN)
add_vivid_operator(alternate         operators/control/alternate/alternate.cpp CODEGEN)
add_vivid_operator(modulated_gain   operators/control/modulated_gain/modulated_gain.cpp CODEGEN
                                    EXTRA_LIBS vivid_embeddable_op_support)
add_vivid_operator(osc_in            operators/control/osc_in/osc_in.cpp      CODEGEN EXTRA_LIBS oscpack)
add_vivid_operator(osc_out           operators/control/osc_out/osc_out.cpp CODEGEN     EXTRA_LIBS oscpack)
add_vivid_operator(mouse             operators/control/mouse/mouse.cpp CODEGEN)
add_vivid_operator(keyboard          operators/control/keyboard/keyboard.cpp CODEGEN          EXTRA_LIBS webgpu)
add_vivid_operator(folder_list       operators/control/folder_list/folder_list.cpp CODEGEN    EXTRA_LIBS webgpu)
add_vivid_operator(string_select     operators/control/string_select/string_select.cpp CODEGEN EXTRA_LIBS webgpu)
add_vivid_operator(basename          operators/control/basename/basename.cpp CODEGEN          EXTRA_LIBS webgpu)
add_vivid_operator(step_counter      operators/control/step_counter/step_counter.cpp  CODEGEN)
add_vivid_operator(phrase_pulse      operators/control/phrase_pulse/phrase_pulse.cpp  CODEGEN)
add_vivid_operator(color_bands       operators/gpu/color_bands/color_bands.cpp CODEGEN           EXTRA_LIBS webgpu)
add_vivid_operator(path_animate      operators/control/path_animate/path_animate.cpp  CODEGEN)
add_vivid_operator(sample_hold       operators/control/sample_hold/sample_hold.cpp CODEGEN    EXTRA_LIBS webgpu)
add_vivid_operator(quantizer         operators/control/quantizer/quantizer.cpp        CODEGEN)
add_vivid_operator(macro             operators/control/macro/macro.cpp CODEGEN)
add_vivid_operator(mseg              operators/control/mseg/mseg.cpp CODEGEN
                   FACTORY_PRESETS operators/control/mseg/factory_presets.json
                   EXTRA_LIBS webgpu
                   EXTRA_CODEGEN_SOURCES operators/control/mseg/mseg_editor.cpp)
target_sources(mseg PRIVATE
    operators/control/mseg/mseg_editor_shared.cpp)

# --- Sequencer operators ---
add_vivid_operator(sequencer         operators/control/sequencer/sequencer.cpp CODEGEN
                   FACTORY_PRESETS operators/control/sequencer/factory_presets.json
                   EXTRA_CODEGEN_SOURCES operators/control/sequencer/sequencer_editor.cpp)
target_sources(sequencer PRIVATE
    operators/control/sequencer/sequencer_editor_shared.cpp)
add_vivid_operator(drum_sequencer    operators/control/drum_sequencer/drum_sequencer.cpp CODEGEN
                   EXTRA_LIBS webgpu
                   EXTRA_CODEGEN_SOURCES operators/control/drum_sequencer/drum_sequencer_core.cpp
                                         operators/control/drum_sequencer/drum_sequencer_editor.cpp)
target_sources(drum_sequencer PRIVATE
    operators/control/drum_sequencer/drum_sequencer_editor_shared.cpp)
add_vivid_operator(pattern_seq       operators/control/pattern_seq/pattern_seq.cpp CODEGEN
                   EXTRA_CODEGEN_SOURCES operators/control/pattern_seq/pattern_seq_editor.cpp)
target_sources(pattern_seq PRIVATE
    operators/control/pattern_seq/pattern_seq_editor_shared.cpp)
add_vivid_operator(note_pattern      operators/control/note_pattern/note_pattern.cpp CODEGEN    EXTRA_LIBS webgpu)
add_vivid_operator(midi_clip         operators/control/midi_clip/midi_clip.cpp CODEGEN
                   EXTRA_LIBS webgpu nlohmann_json::nlohmann_json midifile
                   EXTRA_CODEGEN_SOURCES operators/control/midi_clip/midi_clip_editor.cpp)
target_sources(midi_clip PRIVATE
    operators/control/midi_clip/midi_clip_editor_shared.cpp
    src/common/midi_file.cpp)
add_vivid_operator(note_duration     operators/control/note_duration/note_duration.cpp CODEGEN)
add_vivid_operator(arpeggiator       operators/control/arpeggiator/arpeggiator.cpp CODEGEN     EXTRA_LIBS webgpu
                   EXTRA_CODEGEN_SOURCES operators/control/arpeggiator/arpeggiator_editor.cpp)
target_sources(arpeggiator PRIVATE
    operators/control/arpeggiator/arpeggiator_editor_shared.cpp)
add_vivid_operator(chord_progression operators/control/chord_progression/chord_progression.cpp CODEGEN EXTRA_LIBS webgpu)
add_vivid_operator(state_machine     operators/control/state_machine/state_machine.cpp CODEGEN)
add_vivid_operator(tracker           operators/control/tracker/tracker.cpp CODEGEN
                   EXTRA_LIBS webgpu nlohmann_json::nlohmann_json
                   EXTRA_CODEGEN_SOURCES operators/control/tracker/tracker_core.cpp
                                         operators/control/tracker/tracker_editor.cpp)
target_sources(tracker PRIVATE
    operators/control/tracker/tracker_editor_shared.cpp)
add_vivid_operator(euclidean         operators/control/euclidean/euclidean.cpp CODEGEN       EXTRA_LIBS webgpu vivid_embeddable_op_support
                   EXTRA_CODEGEN_SOURCES operators/control/euclidean/euclidean_editor.cpp)
target_sources(euclidean PRIVATE
    operators/control/euclidean/euclidean_editor_shared.cpp)
add_vivid_operator(pat_transform     operators/control/pat_transform/pat_transform.cpp CODEGEN)
add_vivid_operator(phase_to_midi     operators/control/phase_to_midi/phase_to_midi.cpp CODEGEN)
add_vivid_operator(drum_kit          operators/control/drum_kit/drum_kit.cpp CODEGEN)
add_vivid_operator(note_breakout     operators/control/note_breakout/note_breakout.cpp CODEGEN)
add_vivid_operator(note_modulator    operators/control/note_modulator/note_modulator.cpp CODEGEN)
foreach(_seq_op sequencer drum_sequencer
        pattern_seq note_pattern midi_clip note_duration
        arpeggiator chord_progression
        state_machine tracker euclidean pat_transform
        phase_to_midi drum_kit midi_input note_breakout note_modulator)
    target_include_directories(${_seq_op} PRIVATE ${CMAKE_SOURCE_DIR}/operators/shared/sequencer)
endforeach()

# --- GPU operator plugins (complex operators with custom pipelines) ---
add_vivid_operator(noise          operators/gpu/noise/noise.cpp          CODEGEN EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/noise/factory_presets.json)
add_vivid_operator(composite      operators/gpu/composite/composite.cpp CODEGEN  EXTRA_LIBS webgpu)
add_vivid_operator(texture_analysis operators/gpu/texture_analysis/texture_analysis.cpp CODEGEN EXTRA_LIBS webgpu)
add_vivid_operator(bloom                operators/gpu/bloom/bloom.cpp CODEGEN                             EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/bloom/factory_presets.json)
add_vivid_operator(feedback             operators/gpu/feedback/feedback.cpp CODEGEN                       EXTRA_LIBS webgpu
                   FACTORY_PRESETS operators/gpu/feedback/factory_presets.json)
add_vivid_operator(motion                operators/gpu/motion/motion.cpp CODEGEN                           EXTRA_LIBS webgpu)
add_vivid_operator(metaball              operators/gpu/metaball/metaball.cpp CODEGEN                       EXTRA_LIBS webgpu)
add_vivid_operator(shape_field           operators/gpu/shape_field/shape_field.cpp CODEGEN                EXTRA_LIBS webgpu vivid_embeddable_op_support)
add_vivid_operator(trails                operators/gpu/trails/trails.cpp CODEGEN                           EXTRA_LIBS webgpu vivid_embeddable_op_support)
add_vivid_operator(reaction_diffusion    operators/gpu/reaction_diffusion/reaction_diffusion.cpp CODEGEN   EXTRA_LIBS webgpu vivid_embeddable_op_support)
add_vivid_operator(cellular_automata     operators/gpu/cellular_automata/cellular_automata.cpp CODEGEN     EXTRA_LIBS webgpu vivid_embeddable_op_support)
add_vivid_operator(fluid                 operators/gpu/fluid/fluid.cpp CODEGEN                             EXTRA_LIBS webgpu vivid_embeddable_op_support)
add_vivid_operator(time_machine          operators/gpu/time_machine/time_machine.cpp CODEGEN               EXTRA_LIBS webgpu)
add_vivid_operator(mesh_warp             operators/gpu/mesh_warp/mesh_warp.cpp CODEGEN                     EXTRA_LIBS webgpu)
add_vivid_operator(lut_apply             operators/gpu/lut_apply/lut_apply.cpp CODEGEN                     EXTRA_LIBS webgpu)
add_vivid_operator(scopes                operators/gpu/scopes/scopes.cpp CODEGEN                           EXTRA_LIBS webgpu)
add_vivid_operator(svg_render             operators/gpu/svg_render/svg_render.cpp CODEGEN                   EXTRA_LIBS webgpu nanosvg)
# RichText retired Phase E.7 — animation modes subsumed into Text2D.
add_vivid_operator(metronome_viz         operators/gpu/metronome_viz/metronome_viz.cpp CODEGEN            EXTRA_LIBS webgpu)
# --- TextureLoader (static image → GPU texture, no video machinery) ---
vivid_codegen_for(texture_loader operators/gpu/texture_loader/texture_loader.cpp)
add_library(texture_loader MODULE
    ${VIVID_CODEGEN_OUTPUT_texture_loader}
    operators/shared/movie_decode/texture_upload.cpp
)
target_link_libraries(texture_loader PRIVATE vivid_operator_api webgpu)
set_target_properties(texture_loader PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
target_include_directories(texture_loader PRIVATE ${CMAKE_SOURCE_DIR}/deps/stb ${CMAKE_CURRENT_BINARY_DIR})
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

# --- Movie Session Support Library (shared by MovieFile internals and tests) ---
if(APPLE)
    add_library(movie_session SHARED
        operators/shared/movie_session/movie_transport.cpp
        operators/shared/movie_session/playback_session.cpp
        operators/shared/movie_session/session_registry.cpp
        operators/shared/movie_session/decoded_frame_queue.cpp
        operators/shared/movie_session/video_decode_worker.cpp
    )
    target_include_directories(movie_session PUBLIC
        ${CMAKE_SOURCE_DIR}/operators/shared/movie_session)
    target_link_libraries(movie_session PRIVATE "-framework CoreVideo")
    set_target_properties(movie_session PROPERTIES
        OUTPUT_NAME "movie_session"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    add_custom_command(TARGET movie_session POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/Frameworks
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:movie_session>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/Frameworks/
        COMMENT "Updating movie_session in Vivid.app bundle"
    )
endif()

# --- MovieFile (single-node audio/video file playback) ---
vivid_codegen_for(movie_file operators/gpu/movie_file/movie_file.cpp)
if(APPLE)
    add_library(movie_file MODULE
        ${VIVID_CODEGEN_OUTPUT_movie_file}
        operators/shared/movie_audio/avf_audio_extractor.mm
        operators/shared/movie_decode/decoder_factory.cpp
        operators/shared/movie_decode/texture_upload.cpp
        operators/shared/movie_decode/metal_frame_upload.mm
        operators/shared/movie_decode/placeholder_frame.cpp
        operators/shared/movie_decode/avf_decoder.mm
        operators/shared/movie_decode/hap_decoder.mm
        operators/shared/movie_decode/codec_probe.mm
        deps/hap/hap.c
    )
    target_link_libraries(movie_file PRIVATE
        vivid_operator_api webgpu snappy movie_session
        "-framework AVFoundation" "-framework AVFAudio" "-framework CoreMedia" "-framework CoreVideo"
        "-framework Foundation" "-framework QuartzCore" "-framework Metal" "-framework IOSurface"
    )
    set_source_files_properties(
        operators/shared/movie_audio/avf_audio_extractor.mm
        operators/shared/movie_decode/avf_decoder.mm
        operators/shared/movie_decode/metal_frame_upload.mm
        operators/shared/movie_decode/hap_decoder.mm
        operators/shared/movie_decode/codec_probe.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    add_library(movie_file MODULE ${VIVID_CODEGEN_OUTPUT_movie_file})
    target_link_libraries(movie_file PRIVATE vivid_operator_api webgpu)
endif()
set_target_properties(movie_file PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}"
    BUILD_RPATH "@loader_path/../Frameworks"
    INSTALL_RPATH "@loader_path/../Frameworks")
target_include_directories(movie_file PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/stb
    ${CMAKE_SOURCE_DIR}/deps/hap
    ${CMAKE_CURRENT_BINARY_DIR}
)
if(APPLE)
    add_custom_command(TARGET movie_file POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:movie_file>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        COMMENT "Updating movie_file in Vivid.app bundle"
    )
endif()
if(APPLE)
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"movie_file\": { \"sources\": [\"operators/gpu/movie_file/movie_file.cpp\", \"operators/shared/movie_audio/avf_audio_extractor.mm\", \"operators/shared/movie_decode/decoder_factory.cpp\", \"operators/shared/movie_decode/texture_upload.cpp\", \"operators/shared/movie_decode/metal_frame_upload.mm\", \"operators/shared/movie_decode/placeholder_frame.cpp\", \"operators/shared/movie_decode/avf_decoder.mm\", \"operators/shared/movie_decode/hap_decoder.mm\", \"operators/shared/movie_decode/codec_probe.mm\", \"deps/hap/hap.c\"], \"extra_libs\": [\"webgpu\", \"snappy\"], \"frameworks\": [\"AVFoundation\", \"AVFAudio\", \"CoreMedia\", \"CoreVideo\", \"Foundation\", \"QuartzCore\", \"Metal\", \"IOSurface\"], \"objc_arc\": [\"operators/shared/movie_audio/avf_audio_extractor.mm\", \"operators/shared/movie_decode/avf_decoder.mm\", \"operators/shared/movie_decode/metal_frame_upload.mm\", \"operators/shared/movie_decode/hap_decoder.mm\", \"operators/shared/movie_decode/codec_probe.mm\"], \"include_dirs\": [\"deps/stb\", \"deps/hap\"] }")
else()
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"movie_file\": { \"sources\": [\"operators/gpu/movie_file/movie_file.cpp\"], \"extra_libs\": [\"webgpu\"], \"include_dirs\": [\"deps/stb\"] }")
endif()
set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS movie_file)

# --- Video Sampler (bank of clips, instant index switch) ---
vivid_codegen_for(video_sampler operators/gpu/video_sampler/video_sampler.cpp)
if(APPLE)
    add_library(video_sampler MODULE
        ${VIVID_CODEGEN_OUTPUT_video_sampler}
        operators/shared/movie_decode/decoder_factory.cpp
        operators/shared/movie_decode/texture_upload.cpp
        operators/shared/movie_decode/metal_frame_upload.mm
        operators/shared/movie_decode/placeholder_frame.cpp
        operators/shared/movie_decode/avf_decoder.mm
        operators/shared/movie_decode/hap_decoder.mm
        operators/shared/movie_decode/codec_probe.mm
        deps/hap/hap.c
    )
    target_link_libraries(video_sampler PRIVATE
        vivid_operator_api webgpu snappy movie_session
        "-framework AVFoundation" "-framework AVFAudio" "-framework CoreMedia" "-framework CoreVideo"
        "-framework Foundation" "-framework QuartzCore" "-framework Metal" "-framework IOSurface"
    )
    set_source_files_properties(
        operators/shared/movie_decode/avf_decoder.mm
        operators/shared/movie_decode/metal_frame_upload.mm
        operators/shared/movie_decode/hap_decoder.mm
        operators/shared/movie_decode/codec_probe.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    add_library(video_sampler MODULE ${VIVID_CODEGEN_OUTPUT_video_sampler})
    target_link_libraries(video_sampler PRIVATE vivid_operator_api webgpu)
endif()
set_target_properties(video_sampler PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}"
    BUILD_RPATH "@loader_path/../Frameworks"
    INSTALL_RPATH "@loader_path/../Frameworks")
target_include_directories(video_sampler PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/hap
    ${CMAKE_CURRENT_BINARY_DIR}
)
if(APPLE)
    add_custom_command(TARGET video_sampler POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:video_sampler>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        COMMENT "Updating video_sampler in Vivid.app bundle"
    )
endif()
if(APPLE)
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"video_sampler\": { \"sources\": [\"operators/gpu/video_sampler/video_sampler.cpp\", \"operators/shared/movie_decode/decoder_factory.cpp\", \"operators/shared/movie_decode/texture_upload.cpp\", \"operators/shared/movie_decode/metal_frame_upload.mm\", \"operators/shared/movie_decode/placeholder_frame.cpp\", \"operators/shared/movie_decode/avf_decoder.mm\", \"operators/shared/movie_decode/hap_decoder.mm\", \"operators/shared/movie_decode/codec_probe.mm\", \"deps/hap/hap.c\"], \"extra_libs\": [\"webgpu\", \"snappy\"], \"frameworks\": [\"AVFoundation\", \"AVFAudio\", \"CoreMedia\", \"CoreVideo\", \"Foundation\", \"QuartzCore\", \"Metal\", \"IOSurface\"], \"objc_arc\": [\"operators/shared/movie_decode/avf_decoder.mm\", \"operators/shared/movie_decode/metal_frame_upload.mm\", \"operators/shared/movie_decode/hap_decoder.mm\", \"operators/shared/movie_decode/codec_probe.mm\"], \"include_dirs\": [\"deps/hap\"] }")
else()
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"video_sampler\": { \"sources\": [\"operators/gpu/video_sampler/video_sampler.cpp\"], \"extra_libs\": [\"webgpu\"] }")
endif()
set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS video_sampler)

# --- Webcam In (live camera capture) ---
vivid_codegen_for(webcam_in operators/gpu/webcam_in/webcam_in.cpp)
if(APPLE)
    add_library(webcam_in MODULE
        ${VIVID_CODEGEN_OUTPUT_webcam_in}
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
    add_library(webcam_in MODULE ${VIVID_CODEGEN_OUTPUT_webcam_in})
    target_link_libraries(webcam_in PRIVATE vivid_operator_api webgpu)
endif()
set_target_properties(webcam_in PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
target_include_directories(webcam_in PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
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
    vivid_codegen_for(syphon_in operators/gpu/syphon_in/syphon_in.mm)
    add_library(syphon_in MODULE ${VIVID_CODEGEN_OUTPUT_syphon_in})
    target_link_libraries(syphon_in PRIVATE
        vivid_operator_api webgpu syphon_runtime
        "-framework Foundation" "-framework AppKit" "-framework Metal"
    )
    target_include_directories(syphon_in PRIVATE deps/syphon deps ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_options(syphon_in PRIVATE "-fobjc-arc")
else()
    vivid_codegen_for(syphon_in operators/gpu/syphon_in/syphon_in_stub.cpp)
    add_library(syphon_in MODULE ${VIVID_CODEGEN_OUTPUT_syphon_in})
    target_link_libraries(syphon_in PRIVATE vivid_operator_api webgpu)
    target_include_directories(syphon_in PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
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
    vivid_codegen_for(syphon_out operators/gpu/syphon_out/syphon_out.mm)
    add_library(syphon_out MODULE ${VIVID_CODEGEN_OUTPUT_syphon_out})
    target_link_libraries(syphon_out PRIVATE vivid_operator_api webgpu syphon_runtime)
    target_include_directories(syphon_out PRIVATE deps/syphon deps ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_options(syphon_out PRIVATE "-fobjc-arc")
else()
    vivid_codegen_for(syphon_out operators/gpu/syphon_out/syphon_out_stub.cpp)
    add_library(syphon_out MODULE ${VIVID_CODEGEN_OUTPUT_syphon_out})
    target_link_libraries(syphon_out PRIVATE vivid_operator_api webgpu)
    target_include_directories(syphon_out PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
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
add_vivid_operator(oscillator     operators/audio/oscillator/oscillator.cpp CODEGEN     EXTRA_LIBS webgpu)
add_vivid_operator(gain           operators/audio/gain/gain.cpp CODEGEN           EXTRA_LIBS webgpu)
vivid_enable_audio_kernels(gain)
add_vivid_operator(reverb         operators/audio/reverb/reverb.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/reverb/factory_presets.json)
target_sources(reverb PRIVATE operators/shared/reverb_dsp/reverb_dsp.cpp)
add_vivid_operator(delay          operators/audio/delay/delay.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/delay/factory_presets.json)
add_vivid_operator(bitcrush       operators/audio/bitcrush/bitcrush.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/bitcrush/factory_presets.json)
add_vivid_operator(distortion     operators/audio/distortion/distortion.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/distortion/factory_presets.json)
add_vivid_operator(tape           operators/audio/tape/tape.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/tape/factory_presets.json)
add_vivid_operator(filter         operators/audio/filter/filter.cpp CODEGEN         EXTRA_LIBS webgpu)
target_sources(filter PRIVATE operators/shared/filter_dsp/filter_dsp.cpp)
add_vivid_operator(dual_filter    operators/audio/dual_filter/dual_filter.cpp CODEGEN)
target_sources(dual_filter PRIVATE operators/shared/filter_dsp/filter_dsp.cpp)
add_vivid_operator(audio_noise    operators/audio/noise/noise.cpp CODEGEN    EXTRA_LIBS webgpu)
add_vivid_operator(mixer          operators/audio/mixer/mixer.cpp CODEGEN          EXTRA_LIBS webgpu)
vivid_enable_audio_kernels(mixer)
add_vivid_operator(compressor     operators/audio/compressor/compressor.cpp CODEGEN)
add_vivid_operator(limiter        operators/audio/limiter/limiter.cpp CODEGEN)
add_vivid_operator(chorus         operators/audio/chorus/chorus.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/chorus/factory_presets.json)
add_vivid_operator(phaser         operators/audio/phaser/phaser.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/phaser/factory_presets.json)
add_vivid_operator(flanger        operators/audio/flanger/flanger.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/flanger/factory_presets.json)
add_vivid_operator(stereo_pan_width operators/audio/stereo_pan_width/stereo_pan_width.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/stereo_pan_width/factory_presets.json)
vivid_enable_audio_kernels(stereo_pan_width)
add_vivid_operator(ping_pong_delay  operators/audio/ping_pong_delay/ping_pong_delay.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/ping_pong_delay/factory_presets.json)
add_vivid_operator(fm_synth         operators/audio/fm_synth/fm_synth.cpp CODEGEN
                   FACTORY_PRESETS  operators/audio/fm_synth/factory_presets.json)
target_include_directories(fm_synth PRIVATE ${CMAKE_SOURCE_DIR}/operators/shared/sequencer)
add_vivid_operator(ring_mod         operators/audio/ring_mod/ring_mod.cpp CODEGEN
                   FACTORY_PRESETS  operators/audio/ring_mod/factory_presets.json)
add_vivid_operator(parametric_eq    operators/audio/parametric_eq/parametric_eq.cpp CODEGEN
                   FACTORY_PRESETS  operators/audio/parametric_eq/factory_presets.json)
target_sources(parametric_eq PRIVATE
    operators/audio/parametric_eq/parametric_eq_editor.cpp
    operators/audio/parametric_eq/parametric_eq_editor_shared.cpp)
add_vivid_operator(audio_analysis   operators/audio/audio_analysis/audio_analysis.cpp CODEGEN)
add_vivid_operator(mic_input        operators/audio/mic_input/mic_input.cpp CODEGEN EXTRA_LIBS miniaudio)

# --- Drum operators (from vivid-drums) ---
add_vivid_operator(drum_kick     operators/audio/drum_kick/drum_kick.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/drum_kick/factory_presets.json)
add_vivid_operator(drum_snare    operators/audio/drum_snare/drum_snare.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/drum_snare/factory_presets.json)
add_vivid_operator(drum_hihat    operators/audio/drum_hihat/drum_hihat.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/drum_hihat/factory_presets.json)
add_vivid_operator(drum_clap     operators/audio/drum_clap/drum_clap.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/drum_clap/factory_presets.json)
add_vivid_operator(drum_cymbal   operators/audio/drum_cymbal/drum_cymbal.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/drum_cymbal/factory_presets.json)
add_vivid_operator(drum_tom      operators/audio/drum_tom/drum_tom.cpp CODEGEN
                   FACTORY_PRESETS operators/audio/drum_tom/factory_presets.json)

# --- Sampler operators (from vivid-sampler) ---
# Static libraries for miniaudio decoder support
add_library(sampler_miniaudio STATIC operators/shared/sampler_common/miniaudio_impl.c)
target_include_directories(sampler_miniaudio PRIVATE ${CMAKE_SOURCE_DIR}/deps/miniaudio)

add_vivid_operator(sp404            operators/audio/sp404/sp404.cpp CODEGEN
                   EXTRA_LIBS sampler_miniaudio nlohmann_json::nlohmann_json)
add_vivid_operator(sampler          operators/audio/sampler/sampler.cpp CODEGEN
                   EXTRA_LIBS sampler_miniaudio nlohmann_json::nlohmann_json)
add_vivid_operator(slicer           operators/audio/slicer/slicer.cpp CODEGEN
                   EXTRA_LIBS sampler_miniaudio nlohmann_json::nlohmann_json)
add_vivid_operator(granular_synth   operators/audio/granular_synth/granular_synth.cpp CODEGEN)
target_sources(granular_synth PRIVATE operators/shared/granular_dsp/granular_dsp.cpp)
add_vivid_operator(clap_instrument  operators/audio/clap_instrument/clap_instrument.cpp CODEGEN
                   EXTRA_LIBS clap_headers)
if(APPLE)
    target_sources(clap_instrument PRIVATE operators/shared/clap_host/clap_plugin_window.mm)
    target_link_libraries(clap_instrument PRIVATE "-framework AppKit")
    set_source_files_properties(operators/shared/clap_host/clap_plugin_window.mm
                                PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
add_vivid_operator(clap_effect      operators/audio/clap_effect/clap_effect.cpp CODEGEN
                   EXTRA_LIBS clap_headers)
if(APPLE)
    target_sources(clap_effect PRIVATE operators/shared/clap_host/clap_plugin_window.mm)
    target_link_libraries(clap_effect PRIVATE "-framework AppKit")
    set_source_files_properties(operators/shared/clap_host/clap_plugin_window.mm
                                PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
if(APPLE)
    add_vivid_operator(au_instrument operators/audio/au_instrument/au_instrument.cpp CODEGEN)
    target_link_libraries(au_instrument PRIVATE
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework CoreFoundation")
endif()

# --- VST3 Instrument (macOS only) ---
if(APPLE)
    add_library(vst3_iids STATIC
        ${vst3sdk_SOURCE_DIR}/pluginterfaces/base/coreiids.cpp
        ${vst3sdk_SOURCE_DIR}/base/source/baseiids.cpp
        operators/shared/vst3_host/vst3_vstiids.cpp
    )
    target_link_libraries(vst3_iids PUBLIC vst3_headers)

    vivid_codegen_for(vst3_instrument operators/audio/vst3_instrument/vst3_instrument.cpp)
    add_library(vst3_instrument MODULE
        ${VIVID_CODEGEN_OUTPUT_vst3_instrument}
        operators/shared/vst3_host/vst3_plugin_window.mm
    )
    set_target_properties(vst3_instrument PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
    target_include_directories(vst3_instrument PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(vst3_instrument PRIVATE vivid_operator_api vst3_iids z nlohmann_json::nlohmann_json "-framework AppKit")
    add_custom_command(TARGET vst3_instrument POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vst3_instrument>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        COMMENT "Updating vst3_instrument in Vivid.app bundle"
    )
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS vst3_instrument)
    set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_MANIFEST
        "  \"vst3_instrument\": { \"sources\": [\"operators/audio/vst3_instrument/vst3_instrument.cpp\", \"operators/shared/vst3_host/vst3_plugin_window.mm\"], \"extra_libs\": [\"vst3_iids\", \"nlohmann_json::nlohmann_json\"], \"frameworks\": [\"AppKit\"] }")
endif()

add_vivid_operator(midi_out       operators/audio/midi_out/midi_out.cpp       CODEGEN EXTRA_LIBS rtmidi)
add_vivid_operator(midi_clock_out operators/audio/midi_clock_out/midi_clock_out.cpp CODEGEN EXTRA_LIBS rtmidi)

add_vivid_operator(vocoder          operators/audio/vocoder/vocoder.cpp CODEGEN)
target_sources(vocoder PRIVATE operators/shared/vocoder_dsp/vocoder_dsp.cpp)
add_vivid_operator(spectral_freeze  operators/audio/spectral_freeze/spectral_freeze.cpp CODEGEN)
target_sources(spectral_freeze PRIVATE
    operators/shared/spectral_freeze_dsp/spectral_freeze_dsp.cpp
    src/runtime/simd/fft.cpp)
vivid_enable_audio_kernels(spectral_freeze)
add_vivid_operator(convolution_reverb operators/audio/convolution_reverb/convolution_reverb.cpp CODEGEN
                   EXTRA_LIBS sampler_miniaudio)
target_sources(convolution_reverb PRIVATE
    operators/shared/convolution_reverb_dsp/convolution_reverb_dsp.cpp
    src/runtime/simd/fft.cpp)
target_include_directories(convolution_reverb PRIVATE ${CMAKE_SOURCE_DIR}/deps/miniaudio)
vivid_enable_audio_kernels(convolution_reverb)
foreach(_samp_op sp404 sampler slicer)
    target_include_directories(${_samp_op} PRIVATE
        ${CMAKE_SOURCE_DIR}/operators/shared/sampler_common
        ${CMAKE_SOURCE_DIR}/deps/miniaudio
        ${CMAKE_SOURCE_DIR}/operators/shared/sequencer)
endforeach()

add_vivid_operator(audio_clip operators/audio/audio_clip/audio_clip.cpp CODEGEN
                   EXTRA_LIBS sampler_miniaudio nlohmann_json::nlohmann_json)
target_sources(audio_clip PRIVATE
    operators/audio/audio_clip/audio_clip_editor.cpp
    operators/audio/audio_clip/audio_clip_editor_shared.cpp)
target_include_directories(audio_clip PRIVATE
    ${CMAKE_SOURCE_DIR}/operators/shared/sampler_common
    ${CMAKE_SOURCE_DIR}/deps/miniaudio
    ${signalsmith_stretch_SOURCE_DIR})


# --- Operators meta-target (for package smoke CI: builds all operator dylibs without the app) ---
get_property(_vivid_op_targets GLOBAL PROPERTY VIVID_OPERATOR_TARGETS)
add_custom_target(operators DEPENDS ${_vivid_op_targets})
