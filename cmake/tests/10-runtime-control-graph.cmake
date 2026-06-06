# Shared SIMD FFT helper — scalar vs Accelerate parity, round-trip, known-answer.
add_executable(test_simd_fft
    tests/common/test_simd_fft.cpp
    src/runtime/simd/fft.cpp
)
target_include_directories(test_simd_fft PRIVATE src tests)
target_link_libraries(test_simd_fft PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_test(NAME test_simd_fft COMMAND test_simd_fft WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_simd_fft PROPERTIES TIMEOUT 15)

# Voice allocator unit tests — header-only, no runtime needed.
add_executable(test_voice_table
    tests/shared/test_voice_table.cpp
)
target_include_directories(test_voice_table PRIVATE src tests)
target_link_libraries(test_voice_table PRIVATE vivid_operator_api)
add_test(NAME test_voice_table COMMAND test_voice_table WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_voice_table PROPERTIES TIMEOUT 5)

# Value model (lane-value clean-break, Phase 1) — operator-API side, header-only.
add_executable(test_value_model
    tests/core/test_value_model.cpp
)
target_include_directories(test_value_model PRIVATE src tests)
target_link_libraries(test_value_model PRIVATE vivid_operator_api)
add_test(NAME test_value_model COMMAND test_value_model WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_value_model PROPERTIES TIMEOUT 5)

# Native note transport helpers — header-only, no runtime needed.
add_executable(test_note_helpers
    tests/shared/test_note_helpers.cpp
)
target_include_directories(test_note_helpers PRIVATE
    src tests
    operators/shared/sequencer)
target_link_libraries(test_note_helpers PRIVATE vivid_operator_api)
add_test(NAME test_note_helpers COMMAND test_note_helpers WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_note_helpers PROPERTIES TIMEOUT 5)

# audio_smoke.h relaxed-correctness helper — used across optimization passes.
add_executable(test_audio_smoke
    tests/common/test_audio_smoke.cpp
)
target_include_directories(test_audio_smoke PRIVATE src tests tests/audio)
target_link_libraries(test_audio_smoke PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_test(NAME test_audio_smoke COMMAND test_audio_smoke WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_audio_smoke PROPERTIES TIMEOUT 15)

# Crash recovery unit tests (pure filesystem + JSON; no GPU, no audio, no window)
add_executable(test_crash_recovery
    tests/core/test_crash_recovery.cpp
)
target_include_directories(test_crash_recovery PRIVATE src tests)
target_link_libraries(test_crash_recovery PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_crash_recovery COMMAND test_crash_recovery WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_crash_recovery PROPERTIES TIMEOUT 15)

# Operator alias resolver — pure unordered_map lookups, no runtime needed.
add_executable(test_operator_aliases
    tests/graph/test_operator_aliases.cpp
)
target_include_directories(test_operator_aliases PRIVATE src tests)
target_link_libraries(test_operator_aliases PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_test(NAME test_operator_aliases COMMAND test_operator_aliases WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_operator_aliases PROPERTIES TIMEOUT 5)

# Safe-mode (crash recovery Phase 2) compiler tests — empty registry, no fixtures
add_executable(test_graph_compiler_safe_mode
    tests/graph/test_graph_compiler_safe_mode.cpp
)
target_include_directories(test_graph_compiler_safe_mode PRIVATE src tests)
target_link_libraries(test_graph_compiler_safe_mode PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_graph_compiler_safe_mode COMMAND test_graph_compiler_safe_mode WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_graph_compiler_safe_mode PROPERTIES TIMEOUT 15)

# Quarantine scan tests (crash recovery Phase 4) — pure filesystem + JSON
add_executable(test_quarantine
    tests/core/test_quarantine.cpp
)
target_include_directories(test_quarantine PRIVATE src tests)
target_link_libraries(test_quarantine PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_quarantine COMMAND test_quarantine WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_quarantine PROPERTIES TIMEOUT 15)

# Hot-reload integration test (no GPU, no audio, no window)
add_executable(test_hot_reload
    tests/core/test_hot_reload.cpp
)
target_include_directories(test_hot_reload PRIVATE src tests)
target_link_libraries(test_hot_reload PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_hot_reload test_op_v1 test_op_v2 test_op_incompatible_port)
add_test(NAME test_hot_reload COMMAND test_hot_reload WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_hot_reload PROPERTIES TIMEOUT 15)

add_executable(test_hot_reload_classify
    tests/core/test_hot_reload_classify.cpp
)
target_include_directories(test_hot_reload_classify PRIVATE src tests)
target_link_libraries(test_hot_reload_classify PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_hot_reload_classify COMMAND test_hot_reload_classify WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_audio_hot_reload
    tests/audio/test_audio_hot_reload.cpp
)
target_include_directories(test_audio_hot_reload PRIVATE src tests)
target_link_libraries(test_audio_hot_reload PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_audio_hot_reload audio_reload_v1 audio_reload_v2 audio_reload_incompatible)
add_test(NAME test_audio_hot_reload COMMAND test_audio_hot_reload WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_audio_hot_reload PROPERTIES TIMEOUT 15)

add_executable(test_export_pipeline
    tests/media/test_export_pipeline.cpp
    src/export/export_pipeline.cpp
)
target_include_directories(test_export_pipeline PRIVATE src tests)
target_link_libraries(test_export_pipeline PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_export_pipeline export_custom_port_op)
add_test(NAME test_export_pipeline COMMAND test_export_pipeline WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# RuntimeAPI integration test (no GPU, no audio, no window)
add_executable(test_runtime_api
    tests/control/test_runtime_api.cpp
)
target_include_directories(test_runtime_api PRIVATE src tests)
target_link_libraries(test_runtime_api PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_runtime_api test_op_v1 test_state_carry_op)
add_test(NAME test_runtime_api COMMAND test_runtime_api WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Bypass feature integration test (no GPU, no audio, no window)
add_executable(test_bypass
    tests/control/test_bypass.cpp
)
target_include_directories(test_bypass PRIVATE src tests)
target_link_libraries(test_bypass PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_bypass test_op_v1 control_pass_op)
add_test(NAME test_bypass COMMAND test_bypass ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_bypass PROPERTIES TIMEOUT 15)

# Cross-cadence bridge auto-inference in RuntimeAPI::connect
add_executable(test_connect_bridge_inference
    tests/control/test_connect_bridge_inference.cpp
)
target_include_directories(test_connect_bridge_inference PRIVATE src tests)
target_link_libraries(test_connect_bridge_inference PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_connect_bridge_inference
    test_op_v1 control_pass_op audio_test_op identity_lane_source_op)
add_test(NAME test_connect_bridge_inference
    COMMAND test_connect_bridge_inference ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_connect_bridge_inference PROPERTIES TIMEOUT 15)

# Latency validation test (PRD responsiveness claims)
add_executable(test_latency_validation
    tests/lanes/test_latency_validation.cpp
)
target_include_directories(test_latency_validation PRIVATE src tests)
target_link_libraries(test_latency_validation PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_latency_validation test_op_v1 audio_reload_v1 audio_reload_v2)
add_test(NAME test_latency_validation COMMAND test_latency_validation ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_latency_validation PROPERTIES TIMEOUT 30)

# ControlServer integration test (HTTP endpoints, no GPU, no audio, no window)
add_executable(test_control_server
    tests/control/test_control_server.cpp
)
target_include_directories(test_control_server PRIVATE src tests)
target_link_libraries(test_control_server PRIVATE
    vivid_runtime_testlib)
add_dependencies(test_control_server test_op_v1 semantic_ms_source_op semantic_s_dest_op
    semantic_unknown_source_op untagged_dest_op export_custom_port_op test_op_bad_custom_type
    string_source_op string_sink_op)
add_test(NAME test_control_server COMMAND test_control_server ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Phase 5 crash-recovery HTTP endpoints (focused test; no operators needed)
add_executable(test_control_server_crash
    tests/control/test_control_server_crash.cpp
)
target_include_directories(test_control_server_crash PRIVATE src tests)
target_link_libraries(test_control_server_crash PRIVATE vivid_runtime_testlib)
add_test(NAME test_control_server_crash COMMAND test_control_server_crash
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_control_server_crash PROPERTIES TIMEOUT 30)

# Perception introspection matrix test (domain snapshots + deterministic health)
add_executable(test_perception_introspection
    tests/ops/test_perception_introspection.cpp
)
target_include_directories(test_perception_introspection PRIVATE src tests)
target_link_libraries(test_perception_introspection PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio ixwebsocket webgpu rtmidi stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
add_dependencies(test_perception_introspection test_op_v1 oscillator shape_2d)
add_test(NAME test_perception_introspection COMMAND test_perception_introspection WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_audio_debug_introspection
    tests/control/test_audio_debug_introspection.cpp
)
target_include_directories(test_audio_debug_introspection PRIVATE src tests)
target_link_libraries(test_audio_debug_introspection PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio ixwebsocket webgpu rtmidi stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
add_dependencies(test_audio_debug_introspection multi_channel_dc_source_op audio_reduce_op gain)
add_test(NAME test_audio_debug_introspection COMMAND test_audio_debug_introspection ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Test operator plugins

# AudioEngine integration test (null audio backend, no GPU, no window)
add_executable(test_audio_engine
    tests/audio/test_audio_engine.cpp
)
target_include_directories(test_audio_engine PRIVATE src tests)
target_link_libraries(test_audio_engine PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_audio_engine test_op_v1 audio_test_op)
add_test(NAME test_audio_engine COMMAND test_audio_engine WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_scalar_hold_bridge
    tests/lanes/test_scalar_hold_bridge.cpp
)
target_include_directories(test_scalar_hold_bridge PRIVATE src tests)
target_link_libraries(test_scalar_hold_bridge PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_scalar_hold_bridge test_op_v1 audio_scalar_probe_op)
add_test(NAME test_scalar_hold_bridge COMMAND test_scalar_hold_bridge
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Audio thread robustness test (exception handling in audio callback)
add_executable(test_audio_robustness
    tests/audio/test_audio_robustness.cpp
)
target_include_directories(test_audio_robustness PRIVATE src tests)
target_link_libraries(test_audio_robustness PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_audio_robustness audio_test_op audio_throwing_op)
add_test(NAME test_audio_robustness COMMAND test_audio_robustness WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Scheduler unit test (no GPU, no audio playback, no window)
add_executable(test_runtime_core
    tests/control/test_runtime_core.cpp
)
target_include_directories(test_runtime_core PRIVATE src tests)
target_link_libraries(test_runtime_core PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_runtime_core test_op_v1 control_pass_op lane_source_op audio_test_op)
add_test(NAME test_runtime_core COMMAND test_runtime_core WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Runtime health snapshot — aggregator + JSON serializer + severity rollup
add_executable(test_runtime_health_snapshot
    tests/control/test_runtime_health_snapshot.cpp
)
target_include_directories(test_runtime_health_snapshot PRIVATE src tests)
target_link_libraries(test_runtime_health_snapshot PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_runtime_health_snapshot test_op_v1 control_pass_op)
add_test(NAME test_runtime_health_snapshot
    COMMAND test_runtime_health_snapshot
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_runtime_health_snapshot PROPERTIES
    LABELS "HEADLESS_SMOKE"
    TIMEOUT 15)

# Runtime-health samplers — sustained-silence/black ring buffer + reducers
add_executable(test_runtime_health_samplers
    tests/control/test_runtime_health_samplers.cpp
)
target_include_directories(test_runtime_health_samplers PRIVATE src tests)
target_link_libraries(test_runtime_health_samplers PRIVATE
    vivid_runtime_testlib vivid_operator_api webgpu)
add_test(NAME test_runtime_health_samplers
    COMMAND test_runtime_health_samplers
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_runtime_health_samplers PROPERTIES
    LABELS "HEADLESS_SMOKE"
    TIMEOUT 5)

# v3 operator metadata — assert display_name/keywords/summary appear in the
# JSON emitted by build_operator_docs_response (used by handle_operator_docs
# and handle_list_types).
add_executable(test_operator_docs_metadata
    tests/control/test_operator_docs_metadata.cpp
)
target_include_directories(test_operator_docs_metadata PRIVATE src tests)
target_link_libraries(test_operator_docs_metadata PRIVATE
    vivid_runtime_testlib vivid_operator_api webgpu)
add_test(NAME test_operator_docs_metadata
    COMMAND test_operator_docs_metadata
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_operator_docs_metadata PROPERTIES
    LABELS "HEADLESS_SMOKE"
    TIMEOUT 5)

# Graph data-model unit test (no operators, no runtime, no GPU)
add_executable(test_graph
    tests/graph/test_graph.cpp
)
target_include_directories(test_graph PRIVATE src tests)
target_link_libraries(test_graph PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_test(NAME test_graph COMMAND test_graph WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# JSON migration round-trip regression test
add_executable(test_json_migration
    tests/integration/test_json_migration.cpp
)
target_include_directories(test_json_migration PRIVATE src tests)
target_link_libraries(test_json_migration PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
target_compile_definitions(test_json_migration PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_json_migration COMMAND test_json_migration WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Topological sort unit test (header-only, no dependencies)
add_executable(test_topo_sort tests/common/test_topo_sort.cpp)
target_include_directories(test_topo_sort PRIVATE src tests)
target_link_libraries(test_topo_sort PRIVATE vivid_runtime_testlib)
add_test(NAME test_topo_sort COMMAND test_topo_sort WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# OperatorCreator unit test
add_executable(test_operator_creator
    tests/ops/test_operator_creator.cpp
)
target_include_directories(test_operator_creator PRIVATE src tests)
target_link_libraries(test_operator_creator PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_operator_creator COMMAND test_operator_creator WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Math operator unit tests (all seven ops, divide/modulo zero safety, Euclidean mod)
add_executable(test_math_op
    tests/ops/test_math_op.cpp
)
target_include_directories(test_math_op PRIVATE src tests)
target_link_libraries(test_math_op PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_dependencies(test_math_op math)
add_test(NAME test_math_op COMMAND test_math_op ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_math_op PROPERTIES TIMEOUT 10)

# test_shape_op deleted 2026-04-19 — legacy Shape operator removed in the
# 2D pipeline clean break; its drawable-pipeline replacement (Shape2D) is
# covered by test_demo_graphs and test_gpu_operators.

# Colormap operator tests (Phase 3 of operator-gaps plan: scalar → palette RGB)
add_executable(test_colormap_op
    tests/ops/test_colormap_op.cpp
)
target_include_directories(test_colormap_op PRIVATE src tests)
target_link_libraries(test_colormap_op PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_dependencies(test_colormap_op colormap)
add_test(NAME test_colormap_op COMMAND test_colormap_op ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_colormap_op PROPERTIES TIMEOUT 10)


# Instanced Shapes lane-array inputs (Phase 5 of operator-gaps plan)
add_executable(test_instanced_shapes_lanes
    tests/ops/test_instanced_shapes_lanes.cpp
)
target_include_directories(test_instanced_shapes_lanes PRIVATE src tests)
target_link_libraries(test_instanced_shapes_lanes PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_dependencies(test_instanced_shapes_lanes shape_field)
add_test(NAME test_instanced_shapes_lanes COMMAND test_instanced_shapes_lanes ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_instanced_shapes_lanes PROPERTIES TIMEOUT 10)

# Instanced Shapes rotation + shape_idx lanes (Phase 6 of operator-gaps plan)
add_executable(test_instanced_shapes_phase6
    tests/ops/test_instanced_shapes_phase6.cpp
)
target_include_directories(test_instanced_shapes_phase6 PRIVATE src tests)
target_link_libraries(test_instanced_shapes_phase6 PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_dependencies(test_instanced_shapes_phase6 shape_field)
add_test(NAME test_instanced_shapes_phase6 COMMAND test_instanced_shapes_phase6 ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_instanced_shapes_phase6 PROPERTIES TIMEOUT 10)

# Settings round-trip unit test
add_executable(test_settings
    tests/core/test_settings.cpp
)
target_include_directories(test_settings PRIVATE src tests)
target_link_libraries(test_settings PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_test(NAME test_settings COMMAND test_settings WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Editor window geometry persistence (Phase 3 host integration)
add_executable(test_settings_editor_geometry
    tests/core/test_settings_editor_geometry.cpp
)
target_include_directories(test_settings_editor_geometry PRIVATE src tests)
target_link_libraries(test_settings_editor_geometry PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_test(NAME test_settings_editor_geometry
         COMMAND test_settings_editor_geometry
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Platform-specific editor-open shortcut contract
add_executable(test_window_manager_shortcuts
    tests/core/test_window_manager_shortcuts.cpp
)
target_include_directories(test_window_manager_shortcuts PRIVATE src tests)
target_link_libraries(test_window_manager_shortcuts PRIVATE vivid_runtime_testlib glfw)
add_test(NAME test_window_manager_shortcuts
         COMMAND test_window_manager_shortcuts
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Operator destination policy precedence unit test
add_executable(test_operator_destination_policy
    tests/ops/test_operator_destination_policy.cpp
)
target_include_directories(test_operator_destination_policy PRIVATE src tests)
target_link_libraries(test_operator_destination_policy PRIVATE vivid_runtime_testlib)
add_test(NAME test_operator_destination_policy
    COMMAND test_operator_destination_policy
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Theme loader unit test
add_executable(test_theme_loader
    tests/ui/test_theme_loader.cpp
    src/ui/style/theme_loader.cpp
    src/ui/style/ui_style.cpp
)
target_include_directories(test_theme_loader PRIVATE src tests)
target_link_libraries(test_theme_loader PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_test(NAME test_theme_loader COMMAND test_theme_loader WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Overlay layout/hit-test regressions
add_executable(test_overlay_layouts
    tests/ui/test_overlay_layouts.cpp
    src/ui/rendering/overlay_layouts.cpp
)
target_include_directories(test_overlay_layouts PRIVATE src tests)
target_link_libraries(test_overlay_layouts PRIVATE vivid_runtime_testlib)
add_test(NAME test_overlay_layouts COMMAND test_overlay_layouts WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Advanced-port affordance: descriptor + compiler + snapshot round-trip the
# VIVID_PORT_DISPLAY_ADVANCED hint so the inspector can hide breakout ports.
add_executable(test_advanced_port_filter
    tests/ui/test_advanced_port_filter.cpp
)
target_include_directories(test_advanced_port_filter PRIVATE src tests)
target_link_libraries(test_advanced_port_filter PRIVATE vivid_runtime_testlib)
add_dependencies(test_advanced_port_filter test_op_advanced_port)
add_test(NAME test_advanced_port_filter
         COMMAND test_advanced_port_filter ${CMAKE_BINARY_DIR}
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_graph_snapshot_contract
    tests/graph/test_graph_snapshot_contract.cpp
)
target_include_directories(test_graph_snapshot_contract PRIVATE src tests)
target_link_libraries(test_graph_snapshot_contract PRIVATE vivid_runtime_testlib)
add_test(NAME test_graph_snapshot_contract COMMAND test_graph_snapshot_contract WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_graph_snapshot_builder tests/graph/test_graph_snapshot_builder.cpp)
target_link_libraries(test_graph_snapshot_builder PRIVATE vivid_runtime_testlib)
add_test(NAME test_graph_snapshot_builder COMMAND test_graph_snapshot_builder ${CMAKE_BINARY_DIR})
add_dependencies(test_graph_snapshot_builder test_op_v1)

add_executable(test_graph_file_io tests/control/test_graph_file_io.cpp)
target_link_libraries(test_graph_file_io PRIVATE vivid_runtime_testlib)
add_test(NAME test_graph_file_io COMMAND test_graph_file_io)

add_executable(test_workspace_manager tests/core/test_workspace_manager.cpp)
target_link_libraries(test_workspace_manager PRIVATE vivid_runtime_testlib)
add_test(NAME test_workspace_manager COMMAND test_workspace_manager)

# Highway SIMD smoke test (no GPU, no audio, no window)
add_executable(test_highway_smoke tests/core/test_highway_smoke.cpp)
target_include_directories(test_highway_smoke PRIVATE src tests/core)
if(VIVID_ENABLE_HIGHWAY)
    target_link_libraries(test_highway_smoke PRIVATE hwy)
    target_compile_definitions(test_highway_smoke PRIVATE VIVID_HAS_HIGHWAY=1)
endif()
add_test(NAME test_highway_smoke COMMAND test_highway_smoke WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_highway_smoke PROPERTIES TIMEOUT 10)
