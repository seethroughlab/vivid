# OperatorLoader + OperatorRegistry unit tests (no GPU, no runtime, no graph)
add_executable(test_operator_loader
    tests/ops/test_operator_loader.cpp
)
target_include_directories(test_operator_loader PRIVATE src tests)
target_link_libraries(test_operator_loader PRIVATE vivid_runtime_testlib vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_operator_loader test_op_v1 test_op_v2 test_op_abi_v4 test_op_incompatible_port test_op_null_desc test_op_bad_custom_type control_pass_op audio_test_op export_custom_port_op control_thumb_op file_drop_test_op prepare_assets_test_op prepare_assets_legacy_op particles)
add_test(NAME test_operator_loader COMMAND test_operator_loader WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_operator_loader PROPERTIES LABELS "LOADER")

add_executable(test_operator_source_docs
    tests/ops/test_operator_source_docs.cpp
)
target_include_directories(test_operator_source_docs PRIVATE src tests)
target_link_libraries(test_operator_source_docs PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_test(NAME test_operator_source_docs COMMAND test_operator_source_docs WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_source_index
    tests/core/test_source_index.cpp
)
target_include_directories(test_source_index PRIVATE src tests)
target_link_libraries(test_source_index PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_test(NAME test_source_index COMMAND test_source_index WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_file_drop_registry
    tests/core/test_file_drop_registry.cpp
)
target_include_directories(test_file_drop_registry PRIVATE src tests)
target_link_libraries(test_file_drop_registry PRIVATE vivid_runtime_testlib vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_file_drop_registry file_drop_test_op file_drop_test_op_alt file_drop_bad_param_op midi_file_player)
add_test(NAME test_file_drop_registry COMMAND test_file_drop_registry WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_midi_file_parser
    tests/audio/test_midi_file_parser.cpp
    src/common/midi_file.cpp
)
target_include_directories(test_midi_file_parser PRIVATE src tests)
target_link_libraries(test_midi_file_parser PRIVATE vivid_runtime_testlib vivid_operator_api)
add_test(NAME test_midi_file_parser COMMAND test_midi_file_parser WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_midi_file_player
    tests/audio/test_midi_file_player.cpp
    src/common/midi_file.cpp
)
target_include_directories(test_midi_file_player PRIVATE src tests operators)
target_link_libraries(test_midi_file_player PRIVATE vivid_runtime_testlib vivid_operator_api)
add_test(NAME test_midi_file_player COMMAND test_midi_file_player WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_runtime_stress
    tests/integration/test_runtime_stress.cpp
)
target_include_directories(test_runtime_stress PRIVATE src tests)
target_link_libraries(test_runtime_stress PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_runtime_stress test_op_v1 test_state_carry_op)
add_test(NAME test_runtime_stress COMMAND test_runtime_stress ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_runtime_stress PROPERTIES LABELS "STABILITY" TIMEOUT 30)

add_executable(test_hot_reload_stress
    tests/core/test_hot_reload_stress.cpp
)
target_include_directories(test_hot_reload_stress PRIVATE src tests)
target_link_libraries(test_hot_reload_stress PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_hot_reload_stress audio_reload_v1 audio_reload_v2 audio_reload_v3 audio_reload_incompatible)
add_test(NAME test_hot_reload_stress COMMAND test_hot_reload_stress ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_hot_reload_stress PROPERTIES LABELS "STABILITY" TIMEOUT 30)

add_executable(test_package_stress
    tests/packages/test_package_stress.cpp
)
target_include_directories(test_package_stress PRIVATE src tests)
target_link_libraries(test_package_stress PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_test(NAME test_package_stress COMMAND test_package_stress ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_stress PROPERTIES LABELS "STABILITY" TIMEOUT 30)

add_executable(test_mixed_runtime_stability
    tests/integration/test_mixed_runtime_stability.cpp
)
target_include_directories(test_mixed_runtime_stability PRIVATE src tests)
target_link_libraries(test_mixed_runtime_stability PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_mixed_runtime_stability gpu_fill_op lfo_fr oscillator)
add_test(NAME test_mixed_runtime_stability COMMAND test_mixed_runtime_stability ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_mixed_runtime_stability PROPERTIES LABELS "STABILITY" TIMEOUT 30)

# Modulation operator tests (Flanger, Chorus, Phaser)
add_executable(test_modulation_ops
    tests/ops/test_modulation_ops.cpp
)
target_include_directories(test_modulation_ops PRIVATE src tests)
target_link_libraries(test_modulation_ops PRIVATE vivid_runtime_testlib vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_modulation_ops flanger chorus phaser)
add_test(NAME test_modulation_ops COMMAND test_modulation_ops WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Spatial operator tests (StereoPanWidth, PingPongDelay)
add_executable(test_spatial_ops
    tests/ops/test_spatial_ops.cpp
)
target_include_directories(test_spatial_ops PRIVATE src tests)
target_link_libraries(test_spatial_ops PRIVATE vivid_runtime_testlib vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_spatial_ops stereo_pan_width ping_pong_delay)
add_test(NAME test_spatial_ops COMMAND test_spatial_ops WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Synthesis & transformation operator tests (FmSynth, RingMod, ParametricEQ)
add_executable(test_synth_transform_ops
    tests/ops/test_synth_transform_ops.cpp
)
target_include_directories(test_synth_transform_ops PRIVATE src tests)
target_link_libraries(test_synth_transform_ops PRIVATE vivid_runtime_testlib vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_synth_transform_ops fm_synth ring_mod parametric_eq)
add_test(NAME test_synth_transform_ops COMMAND test_synth_transform_ops WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Audio output correctness tests (spectral/amplitude property assertions)
add_executable(test_audio_correctness
    tests/audio/test_audio_correctness.cpp
)
target_include_directories(test_audio_correctness PRIVATE src tests)
target_link_libraries(test_audio_correctness PRIVATE vivid_runtime_testlib vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_audio_correctness audio_noise fm_synth filter gain)
add_test(NAME test_audio_correctness COMMAND test_audio_correctness WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# DualFilter operator correctness tests
add_executable(test_dual_filter
    tests/audio/test_dual_filter.cpp
)
target_include_directories(test_dual_filter PRIVATE src tests)
target_link_libraries(test_dual_filter PRIVATE vivid_runtime_testlib vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_dependencies(test_dual_filter dual_filter filter)
add_test(NAME test_dual_filter COMMAND test_dual_filter WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# MidiInput per-note expression and MPE tests
add_executable(test_midi_input_expression
    tests/audio/test_midi_input_expression.cpp
)
target_include_directories(test_midi_input_expression PRIVATE src tests deps/rtmidi)
target_link_libraries(test_midi_input_expression PRIVATE vivid_operator_api rtmidi nlohmann_json::nlohmann_json)
if(APPLE)
    target_link_libraries(test_midi_input_expression PRIVATE "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
endif()
add_test(NAME test_midi_input_expression COMMAND test_midi_input_expression WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# GPU operator plugin for testing (solid color fill)

# GPU operator integration tests (headless WebGPU, no window)
add_executable(test_gpu_operators
    tests/gpu/test_gpu_operators.cpp
    src/ui/rendering/renderer_2d.cpp
)
target_include_directories(test_gpu_operators PRIVATE src deps/stb tests)
target_link_libraries(test_gpu_operators PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_gpu_operators gpu_fill_op gpu_metronome_probe_op shape control_thumb_op lfo_fr envelope_fr gain)
add_test(NAME test_gpu_operators COMMAND test_gpu_operators WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# GPU output correctness tests (brightness, contrast, motion property assertions)
add_executable(test_gpu_correctness
    tests/gpu/test_gpu_correctness.cpp
)
target_include_directories(test_gpu_correctness PRIVATE src tests)
target_link_libraries(test_gpu_correctness PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_gpu_correctness gpu_fill_op shape noise)
add_test(NAME test_gpu_correctness COMMAND test_gpu_correctness WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Per-operator smoke sweep — discovers all dylibs and validates load/desc/process/boundary
add_executable(test_operator_sweep
    tests/ops/test_operator_sweep.cpp
)
target_include_directories(test_operator_sweep PRIVATE src tests)
target_link_libraries(test_operator_sweep PRIVATE vivid_runtime_testlib vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_test(NAME test_operator_sweep COMMAND test_operator_sweep WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_operator_sweep PROPERTIES LABELS "HEADLESS_SMOKE" TIMEOUT 120)

# --- Sequencer operator tests (from vivid-sequencers) ---
add_executable(test_arpeggiator_patterns tests/ops/test_arpeggiator_patterns.cpp)
target_include_directories(test_arpeggiator_patterns PRIVATE
    src ${CMAKE_SOURCE_DIR}/operators/shared/sequencer tests)
target_link_libraries(test_arpeggiator_patterns PRIVATE vivid_runtime_testlib vivid_operator_api)
add_test(NAME test_arpeggiator_patterns COMMAND test_arpeggiator_patterns)

add_executable(test_state_machine tests/lanes/test_state_machine.cpp)
target_include_directories(test_state_machine PRIVATE
    src ${CMAKE_SOURCE_DIR}/operators/shared/sequencer tests)
target_link_libraries(test_state_machine PRIVATE vivid_runtime_testlib vivid_operator_api)
add_test(NAME test_state_machine COMMAND test_state_machine)

# 3D tests are in the vivid-3d package (see ../vivid-3d/)

# Audio-frame bridge lane snapshot integration test
add_executable(test_lane_bridge_snapshot
    tests/lanes/test_lane_bridge_snapshot.cpp
)
target_include_directories(test_lane_bridge_snapshot PRIVATE src tests)
target_link_libraries(test_lane_bridge_snapshot PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_lane_bridge_snapshot lane_source_op audio_lane_op)
add_test(NAME test_lane_bridge_snapshot COMMAND test_lane_bridge_snapshot
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Lane broadcast/wrap test
add_executable(test_lane_broadcast
    tests/lanes/test_lane_broadcast.cpp
)
target_include_directories(test_lane_broadcast PRIVATE src tests)
target_link_libraries(test_lane_broadcast PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_lane_broadcast lane_source_op lane_sink_op lane_smooth_op)
add_test(NAME test_lane_broadcast COMMAND test_lane_broadcast WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Scalar-to-lane-array broadcast test
add_executable(test_scalar_lane_broadcast
    tests/lanes/test_scalar_lane_broadcast.cpp
)
target_include_directories(test_scalar_lane_broadcast PRIVATE src tests)
target_link_libraries(test_scalar_lane_broadcast PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_scalar_lane_broadcast lane_source_op scalar_source_op dual_lane_sink_op)
add_test(NAME test_scalar_lane_broadcast COMMAND test_scalar_lane_broadcast WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_reshape
    tests/lanes/test_lane_reshape.cpp
)
target_include_directories(test_lane_reshape PRIVATE src tests)
target_link_libraries(test_lane_reshape PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_lane_reshape lane_source_op lane_sink_op repeat tile select math)
add_test(NAME test_lane_reshape COMMAND test_lane_reshape WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_frame_lane_lifting
    tests/lanes/test_frame_lane_lifting.cpp
)
target_include_directories(test_frame_lane_lifting PRIVATE src tests)
target_link_libraries(test_frame_lane_lifting PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_frame_lane_lifting lane_source_op lane_sink_op lane_frame_op identity_lane_source_op repeat envelope_fr lfo_fr)
add_test(NAME test_frame_lane_lifting COMMAND test_frame_lane_lifting WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_breadth
    tests/lanes/test_lane_breadth.cpp
)
target_include_directories(test_lane_breadth PRIVATE src tests)
target_link_libraries(test_lane_breadth PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_lane_breadth lane_source_op lane_sink_op lane_frame_op fft_analysis repeat)
add_test(NAME test_lane_breadth COMMAND test_lane_breadth WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_compute_lane_equivalence tests/lanes/test_compute_lane_equivalence.cpp)
target_include_directories(test_compute_lane_equivalence PRIVATE src tests)
target_link_libraries(test_compute_lane_equivalence PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_test(NAME test_compute_lane_equivalence COMMAND test_compute_lane_equivalence WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Lane metadata runtime test
configure_file(tests/graphs/test_lane_metadata.json ${CMAKE_BINARY_DIR}/test_lane_metadata.json COPYONLY)
add_executable(test_lane_metadata
    tests/lanes/test_lane_metadata.cpp
)
target_include_directories(test_lane_metadata PRIVATE src tests)
target_link_libraries(test_lane_metadata PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_lane_metadata lane_source_op lane_metadata_op)
add_test(NAME test_lane_metadata COMMAND test_lane_metadata WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# ChildOp<T> composition unit test (no plugins, no runtime, logic only).
# Non-header-only embeddables have separate loader regressions in
# test_operator_loader; this test uses stubs so it stays focused on ChildOp
# call semantics and persistent state behavior.
add_executable(test_child_op tests/ops/test_child_op.cpp tests/stubs/smooth_stubs.cpp tests/stubs/lfo_stubs.cpp)
target_include_directories(test_child_op PRIVATE src tests ${CMAKE_SOURCE_DIR}/operators)
target_link_libraries(test_child_op PRIVATE vivid_runtime_testlib vivid_operator_api)
add_test(NAME test_child_op COMMAND test_child_op WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_metronome_sync tests/ops/test_metronome_sync.cpp)
target_include_directories(test_metronome_sync PRIVATE src tests)
target_link_libraries(test_metronome_sync PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_test(NAME test_metronome_sync COMMAND test_metronome_sync WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# SCALAR port tests (type compatibility and audio-rate scalar routing)
add_executable(test_scalar_port
    tests/lanes/test_scalar_port.cpp
)
target_include_directories(test_scalar_port PRIVATE src tests)
target_link_libraries(test_scalar_port PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_scalar_port lfo_au audio_scalar_probe_op)
add_test(NAME test_scalar_port COMMAND test_scalar_port WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_audio_control_timing
    tests/audio/test_audio_control_timing.cpp
)
target_include_directories(test_audio_control_timing PRIVATE src tests)
target_link_libraries(test_audio_control_timing PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_dependencies(test_audio_control_timing
    gate_au sample_hold_au step_counter_au phase_to_midi_au step_seq_au euclidean_au)
add_test(NAME test_audio_control_timing COMMAND test_audio_control_timing WORKING_DIRECTORY ${CMAKE_BINARY_DIR})


# Copy test graphs to build directory
configure_file(tests/graphs/test_reload.json ${CMAKE_BINARY_DIR}/test_reload.json COPYONLY)
configure_file(tests/graphs/test_runtime_api.json ${CMAKE_BINARY_DIR}/test_runtime_api.json COPYONLY)
configure_file(tests/graphs/test_audio_engine.json ${CMAKE_BINARY_DIR}/test_audio_engine.json COPYONLY)
configure_file(tests/graphs/test_audio_robustness.json ${CMAKE_BINARY_DIR}/test_audio_robustness.json COPYONLY)
configure_file(tests/graphs/test_lane_broadcast.json ${CMAKE_BINARY_DIR}/test_lane_broadcast.json COPYONLY)
configure_file(tests/graphs/test_scalar_lane_broadcast.json ${CMAKE_BINARY_DIR}/test_scalar_lane_broadcast.json COPYONLY)
configure_file(tests/graphs/test_lane_bridge_snapshot.json ${CMAKE_BINARY_DIR}/test_lane_bridge_snapshot.json COPYONLY)
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
