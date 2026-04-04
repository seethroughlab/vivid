# --- Shared runtime library for tests (eliminates redundant compilation) ---
add_library(vivid_runtime_testlib STATIC
    src/runtime/audio/audio_engine.cpp
    src/runtime/audio/audio_frame_bridge.cpp
    src/runtime/audio/system_midi.cpp
    src/runtime/control/control_server.cpp
    src/runtime/control/control_server_checks.cpp
    src/runtime/control/control_server_dispatch.cpp
    src/runtime/control/control_server_query.cpp
    src/runtime/control/runtime_api.cpp
    src/runtime/control/runtime_command_sink.cpp
    src/runtime/core/editor_detect.cpp
    src/runtime/core/file_drop_registry.cpp
    src/runtime/core/file_watcher.cpp
    src/runtime/core/hot_reload.cpp
    src/runtime/core/runtime_bootstrap.cpp
    src/runtime/core/runtime_core.cpp
    src/runtime/core/settings.cpp
    src/runtime/core/tool_discovery.cpp
    src/runtime/core/undo_manager.cpp
    src/runtime/debug/capture_coordinator.cpp
    src/runtime/debug/output_analyzer.cpp
    src/runtime/gpu/screenshot.cpp
    src/runtime/gpu/wgsl_header_parser.cpp
    src/runtime/graph/audio_executor.cpp
    src/runtime/graph/frame_executor.cpp
    src/runtime/graph/graph.cpp
    src/runtime/graph/graph_compiler.cpp
    src/runtime/graph/port_type_registry.cpp
    src/runtime/graph/subgraph_module.cpp
    src/runtime/operators/builtin_operators.cpp
    src/runtime/operators/operator_creator.cpp
    src/runtime/operators/operator_loader.cpp
    src/runtime/operators/operator_registry.cpp
    src/runtime/operators/operator_source_docs.cpp
    src/runtime/packages/package_catalog.cpp
    src/runtime/packages/package_compiler.cpp
    src/runtime/packages/package_manager.cpp
    src/runtime/packages/package_scaffolder.cpp
    src/runtime/packages/package_test_runner.cpp
    src/runtime/platform/app_update_manager.cpp
    src/runtime/platform/av_exporter.mm
    src/runtime/platform/platform.cpp
)
target_include_directories(vivid_runtime_testlib PUBLIC src tests)
target_link_libraries(vivid_runtime_testlib PUBLIC
    vivid_operator_api nlohmann_json::nlohmann_json webgpu
    miniaudio rtmidi snappy stb_truetype ixwebsocket)
if(APPLE)
    target_link_libraries(vivid_runtime_testlib PUBLIC
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
        "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore"
        "-framework CoreMIDI")
    target_compile_options(vivid_runtime_testlib PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endif()

# --- Tests ---
# Test operator plugins (two versions of the same operator)
add_vivid_operator(test_op_v1     tests/operators/test_op_v1.cpp)
add_vivid_operator(test_op_v2     tests/operators/test_op_v2.cpp)
add_library(test_op_abi_v4 MODULE tests/operators/test_op_abi_v4.cpp)
target_link_libraries(test_op_abi_v4 PRIVATE vivid_runtime_testlib vivid_operator_api)
set_target_properties(test_op_abi_v4 PROPERTIES
    PREFIX ""
    SUFFIX ${VIVID_PLUGIN_SUFFIX}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
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
add_vivid_operator(prepare_assets_test_op tests/operators/prepare_assets_test_op.cpp)
add_vivid_operator(prepare_assets_legacy_op tests/operators/prepare_assets_legacy_op.cpp)
add_vivid_operator(semantic_ms_source_op tests/operators/semantic_ms_source_op.cpp)
add_vivid_operator(semantic_s_dest_op    tests/operators/semantic_s_dest_op.cpp)
add_vivid_operator(semantic_unknown_source_op tests/operators/semantic_unknown_source_op.cpp)
add_vivid_operator(untagged_dest_op          tests/operators/untagged_dest_op.cpp)


# Hot-reload integration test (no GPU, no audio, no window)
add_executable(test_hot_reload
    tests/core/test_hot_reload.cpp
)
target_include_directories(test_hot_reload PRIVATE src tests)
target_link_libraries(test_hot_reload PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_hot_reload test_op_v1 test_op_v2 test_op_incompatible_port)
add_test(NAME test_hot_reload COMMAND test_hot_reload WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_hot_reload PROPERTIES TIMEOUT 15)

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
    semantic_unknown_source_op untagged_dest_op export_custom_port_op test_op_bad_custom_type)
add_test(NAME test_control_server COMMAND test_control_server ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Perception introspection matrix test (domain snapshots + deterministic health)
add_executable(test_perception_introspection
    tests/ops/test_perception_introspection.cpp
)
target_include_directories(test_perception_introspection PRIVATE src tests)
target_link_libraries(test_perception_introspection PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio ixwebsocket webgpu rtmidi stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
add_dependencies(test_perception_introspection test_op_v1)
add_test(NAME test_perception_introspection COMMAND test_perception_introspection WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Test operator plugins
add_vivid_operator(audio_test_op      tests/operators/audio_test_op.cpp)
add_vivid_operator(audio_scalar_probe_op tests/operators/audio_scalar_probe_op.cpp)
add_vivid_operator(audio_throwing_op  tests/operators/audio_throwing_op.cpp)
add_vivid_operator(control_pass_op    tests/operators/control_pass_op.cpp)
add_vivid_operator(lane_source_op   tests/operators/lane_source_op.cpp)
add_vivid_operator(lane_sink_op     tests/operators/lane_sink_op.cpp)
add_vivid_operator(lane_metadata_op       tests/operators/lane_metadata_op.cpp)
add_vivid_operator(lane_metadata_audio_op tests/operators/lane_metadata_audio_op.cpp)
add_vivid_operator(lane_smooth_op         tests/operators/lane_smooth_op.cpp)
add_vivid_operator(lane_slew_op           tests/operators/lane_slew_op.cpp)
add_vivid_operator(identity_lane_source_op tests/operators/identity_lane_source_op.cpp)
add_vivid_operator(lane_state_tracker_op     tests/operators/lane_state_tracker_op.cpp)
add_vivid_operator(multi_channel_dc_source_op tests/operators/multi_channel_dc_source_op.cpp)
add_vivid_operator(dc_per_lane_op            tests/operators/dc_per_lane_op.cpp)
add_vivid_operator(lane_frame_op             tests/operators/lane_frame_op.cpp)
add_vivid_operator(audio_lane_op    tests/operators/audio_lane_op.cpp)

# AudioEngine integration test (null audio backend, no GPU, no window)
add_executable(test_audio_engine
    tests/audio/test_audio_engine.cpp
)
target_include_directories(test_audio_engine PRIVATE src tests)
target_link_libraries(test_audio_engine PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_audio_engine test_op_v1 audio_test_op)
add_test(NAME test_audio_engine COMMAND test_audio_engine WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_scalar_hold_bridge
    tests/lanes/test_scalar_hold_bridge.cpp
)
target_include_directories(test_scalar_hold_bridge PRIVATE src tests)
target_link_libraries(test_scalar_hold_bridge PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_scalar_hold_bridge test_op_v1 audio_scalar_probe_op)
add_test(NAME test_scalar_hold_bridge COMMAND test_scalar_hold_bridge
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Audio thread robustness test (exception handling in audio callback)
add_executable(test_audio_robustness
    tests/audio/test_audio_robustness.cpp
)
target_include_directories(test_audio_robustness PRIVATE src tests)
target_link_libraries(test_audio_robustness PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
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

# Settings round-trip unit test
add_executable(test_settings
    tests/core/test_settings.cpp
)
target_include_directories(test_settings PRIVATE src tests)
target_link_libraries(test_settings PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_test(NAME test_settings COMMAND test_settings WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

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

# Inspector layout normalization tests
add_executable(test_inspector_layout
    tests/ui/test_inspector_layout.cpp
)
target_include_directories(test_inspector_layout PRIVATE src tests)
target_link_libraries(test_inspector_layout PRIVATE vivid_runtime_testlib)
add_test(NAME test_inspector_layout COMMAND test_inspector_layout WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# UI overlay interaction regressions (example/package/meta overlay callbacks)
add_executable(test_ui_overlay_interactions
    tests/ui/test_ui_overlay_interactions.cpp
)
target_include_directories(test_ui_overlay_interactions PRIVATE src tests)
target_link_libraries(test_ui_overlay_interactions PRIVATE vivid_runtime_testlib vivid_ui webgpu glfw nlohmann_json::nlohmann_json stb_truetype)
if(APPLE)
    target_link_libraries(test_ui_overlay_interactions PRIVATE
        "-framework Cocoa" "-framework Foundation")
endif()
add_test(NAME test_ui_overlay_interactions COMMAND test_ui_overlay_interactions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_overlay_interactions PROPERTIES LABELS "UI_SMOKE")

add_executable(test_ui_editor_interactions
    tests/ui/test_ui_editor_interactions.cpp
)
target_include_directories(test_ui_editor_interactions PRIVATE src tests)
target_link_libraries(test_ui_editor_interactions PRIVATE vivid_runtime_testlib vivid_ui webgpu glfw nlohmann_json::nlohmann_json stb_truetype)
if(APPLE)
    target_link_libraries(test_ui_editor_interactions PRIVATE
        "-framework Cocoa" "-framework Foundation")
endif()
add_test(NAME test_ui_editor_interactions COMMAND test_ui_editor_interactions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_editor_interactions PROPERTIES LABELS "UI_SMOKE")

add_executable(test_ui_widget_interactions
    tests/ui/test_ui_widget_interactions.cpp
)
target_include_directories(test_ui_widget_interactions PRIVATE src tests)
target_link_libraries(test_ui_widget_interactions PRIVATE vivid_runtime_testlib vivid_ui webgpu glfw nlohmann_json::nlohmann_json stb_truetype)
if(APPLE)
    target_link_libraries(test_ui_widget_interactions PRIVATE
        "-framework Cocoa" "-framework Foundation")
endif()
add_test(NAME test_ui_widget_interactions COMMAND test_ui_widget_interactions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_widget_interactions PROPERTIES LABELS "UI_SMOKE")

# Windowed GUI smoke for inspector and editor-drop regressions.
add_executable(test_ui_screenshot_smoke
    tests/ui/test_ui_screenshot_smoke.cpp
)
target_include_directories(test_ui_screenshot_smoke PRIVATE src tests deps/stb)
target_link_libraries(test_ui_screenshot_smoke PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
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
add_executable(test_ui_arch_guard tests/ui/test_ui_arch_guard.cpp)
target_include_directories(test_ui_arch_guard PRIVATE src tests)
target_link_libraries(test_ui_arch_guard PRIVATE vivid_runtime_testlib)
add_test(NAME test_ui_arch_guard COMMAND test_ui_arch_guard ${CMAKE_SOURCE_DIR} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# WGSL header parser unit test
add_executable(test_wgsl_header
    tests/common/test_wgsl_header.cpp
)
target_include_directories(test_wgsl_header PRIVATE src tests)
target_link_libraries(test_wgsl_header PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_test(NAME test_wgsl_header COMMAND test_wgsl_header WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# WGSL include preprocessor diagnostics (missing include/cycle/include chain)
add_executable(test_wgsl_preprocessor
    tests/common/test_wgsl_preprocessor.cpp
)
target_include_directories(test_wgsl_preprocessor PRIVATE src tests)
target_link_libraries(test_wgsl_preprocessor PRIVATE vivid_runtime_testlib)
add_test(NAME test_wgsl_preprocessor COMMAND test_wgsl_preprocessor WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# String utility unit test (header-only, no dependencies)
add_executable(test_string_util tests/common/test_string_util.cpp)
target_include_directories(test_string_util PRIVATE src tests)
target_link_libraries(test_string_util PRIVATE vivid_runtime_testlib)
add_test(NAME test_string_util COMMAND test_string_util WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Text editing unit test (header-only, no dependencies)
add_executable(test_text_edit tests/ui/test_text_edit.cpp)
target_include_directories(test_text_edit PRIVATE src tests)
target_link_libraries(test_text_edit PRIVATE vivid_runtime_testlib)
add_test(NAME test_text_edit COMMAND test_text_edit WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# audio_dsp operator API compatibility test
add_executable(test_audio_dsp_api tests/audio/test_audio_dsp_api.cpp)
target_include_directories(test_audio_dsp_api PRIVATE src tests)
target_link_libraries(test_audio_dsp_api PRIVATE vivid_runtime_testlib)
add_test(NAME test_audio_dsp_api COMMAND test_audio_dsp_api WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

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

# GPU operator plugin for testing (solid color fill)
add_vivid_operator(gpu_fill_op      tests/operators/gpu_fill_op.cpp      EXTRA_LIBS webgpu)

# GPU operator integration tests (headless WebGPU, no window)
add_executable(test_gpu_operators
    tests/gpu/test_gpu_operators.cpp
    src/ui/rendering/renderer_2d.cpp
)
target_include_directories(test_gpu_operators PRIVATE src deps/stb tests)
target_link_libraries(test_gpu_operators PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_gpu_operators gpu_fill_op shape control_thumb_op lfo_fr envelope_fr gain)
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
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_lane_bridge_snapshot lane_source_op audio_lane_op)
add_test(NAME test_lane_bridge_snapshot COMMAND test_lane_bridge_snapshot
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Lane broadcast/wrap test
add_executable(test_lane_broadcast
    tests/lanes/test_lane_broadcast.cpp
)
target_include_directories(test_lane_broadcast PRIVATE src tests)
target_link_libraries(test_lane_broadcast PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_lane_broadcast lane_source_op lane_sink_op lane_smooth_op)
add_test(NAME test_lane_broadcast COMMAND test_lane_broadcast WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_reshape
    tests/lanes/test_lane_reshape.cpp
)
target_include_directories(test_lane_reshape PRIVATE src tests)
target_link_libraries(test_lane_reshape PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_lane_reshape lane_source_op lane_sink_op repeat tile select math)
add_test(NAME test_lane_reshape COMMAND test_lane_reshape WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_frame_lane_lifting
    tests/lanes/test_frame_lane_lifting.cpp
)
target_include_directories(test_frame_lane_lifting PRIVATE src tests)
target_link_libraries(test_frame_lane_lifting PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_frame_lane_lifting lane_source_op lane_sink_op lane_frame_op identity_lane_source_op repeat envelope_fr lfo_fr)
add_test(NAME test_frame_lane_lifting COMMAND test_frame_lane_lifting WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_breadth
    tests/lanes/test_lane_breadth.cpp
)
target_include_directories(test_lane_breadth PRIVATE src tests)
target_link_libraries(test_lane_breadth PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
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
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
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

# SCALAR port tests (type compatibility and audio-rate scalar routing)
add_executable(test_scalar_port
    tests/lanes/test_scalar_port.cpp
)
target_include_directories(test_scalar_port PRIVATE src tests)
target_link_libraries(test_scalar_port PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
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

# Demo graph smoke test (loads every demo graph, builds runtime, ticks 5 frames)
add_executable(test_demo_graphs
    tests/integration/test_demo_graphs.cpp
)
target_include_directories(test_demo_graphs PRIVATE src tests)
target_link_libraries(test_demo_graphs PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_demo_graphs
    # control
    lfo_fr lfo_au clock_fr clock_au math envelope_fr envelope_au midi_input fft_analysis
    logic gate_fr gate_au smooth_fr smooth_au
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
    tests/media/test_media_headless.cpp
)
target_include_directories(test_media_headless PRIVATE src tests)
target_link_libraries(test_media_headless PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_media_headless
    movie_file_in movie_file_audio
    lfo_fr noise composite bloom feedback
)
add_test(NAME test_media_headless
    COMMAND test_media_headless ${CMAKE_BINARY_DIR}/graphs
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_media_headless PROPERTIES
    LABELS "MEDIA_HEADLESS"
    TIMEOUT 60)

# Audio-domain sequencer graph integration test
add_executable(test_audio_sequencer_graph
    tests/audio/test_audio_sequencer_graph.cpp
)
target_include_directories(test_audio_sequencer_graph PRIVATE src tests)
target_link_libraries(test_audio_sequencer_graph PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_audio_sequencer_graph clock_au oscillator gain)
add_test(NAME test_audio_sequencer_graph
    COMMAND test_audio_sequencer_graph ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_audio_sequencer_graph PROPERTIES LABELS "STABILITY")

# Fixed-cadence assignment integration test
add_executable(test_fixed_cadence_assignment
    tests/lanes/test_fixed_cadence_assignment.cpp
)
target_include_directories(test_fixed_cadence_assignment PRIVATE src tests)
target_link_libraries(test_fixed_cadence_assignment PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_fixed_cadence_assignment clock_fr clock_au oscillator lfo_fr lfo_au)
add_test(NAME test_fixed_cadence_assignment
    COMMAND test_fixed_cadence_assignment ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_fixed_cadence_assignment PROPERTIES LABELS "STABILITY")

# PackageCompiler unit test (compiles a mock package operator)
add_executable(test_package_compiler
    tests/packages/test_package_compiler.cpp
)
target_include_directories(test_package_compiler PRIVATE src tests)
target_link_libraries(test_package_compiler PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_compiler
    COMMAND test_package_compiler ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_compiler PROPERTIES LABELS "PACKAGE")

# PackageCatalog unit test (JSON parsing, cache, merge)
add_executable(test_package_catalog
    tests/packages/test_package_catalog.cpp
)
target_include_directories(test_package_catalog PRIVATE src tests)
target_link_libraries(test_package_catalog PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_catalog
    COMMAND test_package_catalog
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_catalog PROPERTIES LABELS "PACKAGE")

# PackageManager lifecycle test (install/list/uninstall with local path)
add_executable(test_package_manager
    tests/packages/test_package_manager.cpp
)
target_include_directories(test_package_manager PRIVATE src tests)
target_link_libraries(test_package_manager PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_manager
    COMMAND test_package_manager ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_manager PROPERTIES LABELS "PACKAGE")

add_executable(test_runtime_bootstrap_packages
    tests/control/test_runtime_bootstrap_packages.cpp
)
target_include_directories(test_runtime_bootstrap_packages PRIVATE src tests)
target_link_libraries(test_runtime_bootstrap_packages PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_runtime_bootstrap_packages lane_source_op lane_sink_op)
add_test(NAME test_runtime_bootstrap_packages
    COMMAND test_runtime_bootstrap_packages ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_runtime_bootstrap_packages PROPERTIES LABELS "PACKAGE")

# Package scope resolver test (deterministic multi-scope precedence/conflicts)
add_executable(test_package_scope_resolver
    tests/packages/test_package_scope_resolver.cpp
)
target_include_directories(test_package_scope_resolver PRIVATE src tests)
target_link_libraries(test_package_scope_resolver PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_scope_resolver
    COMMAND test_package_scope_resolver ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_scope_resolver PROPERTIES LABELS "PACKAGE")

# Package scope + registry integration test (winner package only)
add_executable(test_package_scope_registry
    tests/packages/test_package_scope_registry.cpp
)
target_include_directories(test_package_scope_registry PRIVATE src tests)
target_link_libraries(test_package_scope_registry PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_scope_registry
    COMMAND test_package_scope_registry ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_scope_registry PROPERTIES LABELS "PACKAGE")

# Package scaffolder unit test (template copy + placeholder replacement)
add_executable(test_package_scaffolder
    tests/packages/test_package_scaffolder.cpp
)
target_include_directories(test_package_scaffolder PRIVATE src tests)
target_link_libraries(test_package_scaffolder PRIVATE vivid_runtime_testlib)
add_test(NAME test_package_scaffolder
    COMMAND test_package_scaffolder ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_scaffolder PROPERTIES LABELS "PACKAGE")

# PackageManager update metadata logic test (semantic version + compatibility)
add_executable(test_package_update_logic
    tests/packages/test_package_update_logic.cpp
)
target_include_directories(test_package_update_logic PRIVATE src tests)
target_link_libraries(test_package_update_logic PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_update_logic
    COMMAND test_package_update_logic
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_update_logic PROPERTIES LABELS "PACKAGE")

# Core app update metadata parsing test (appcast + semver compare)
add_executable(test_app_update_manager
    tests/core/test_app_update_manager.cpp
)
target_include_directories(test_app_update_manager PRIVATE src tests)
target_link_libraries(test_app_update_manager PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_test(NAME test_app_update_manager
    COMMAND test_app_update_manager
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_app_update_manager PROPERTIES LABELS "PACKAGE")

# PackageTestRunner test (graph + C++ test execution for packages)
add_executable(test_package_test_runner
    tests/packages/test_package_test_runner.cpp
)
target_include_directories(test_package_test_runner PRIVATE src tests)
target_link_libraries(test_package_test_runner PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_package_test_runner control_pass_op)
add_test(NAME test_package_test_runner
    COMMAND test_package_test_runner ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_test_runner PROPERTIES LABELS "PACKAGE")

add_executable(test_package_contract_ecosystem
    tests/packages/test_package_contract_ecosystem.cpp
)
target_include_directories(test_package_contract_ecosystem PRIVATE src tests)
target_link_libraries(test_package_contract_ecosystem PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_package_contract_ecosystem control_pass_op)
add_test(NAME test_package_contract_ecosystem
    COMMAND test_package_contract_ecosystem ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_contract_ecosystem PROPERTIES LABELS "PACKAGE")

# CaptureCoordinator unit test (non-GPU paths only)
add_executable(test_capture_coordinator
    tests/core/test_capture_coordinator.cpp
)
target_include_directories(test_capture_coordinator PRIVATE src tests)
target_link_libraries(test_capture_coordinator PRIVATE
    vivid_runtime_testlib)
add_test(NAME test_capture_coordinator COMMAND test_capture_coordinator
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# OutputAnalyzer unit test (pure analysis functions, no GPU/audio)
add_executable(test_output_analyzer
    tests/integration/test_output_analyzer.cpp
)
target_include_directories(test_output_analyzer PRIVATE src tests)
target_link_libraries(test_output_analyzer PRIVATE vivid_runtime_testlib vivid_operator_api)
add_test(NAME test_output_analyzer COMMAND test_output_analyzer
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# SystemMidiListener unit test (no hardware needed)
add_executable(test_midi
    tests/audio/test_midi.cpp
)
target_include_directories(test_midi PRIVATE src tests)
target_link_libraries(test_midi PRIVATE vivid_runtime_testlib rtmidi)
add_test(NAME test_midi COMMAND test_midi WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# FileWatcher unit tests (kqueue, no runtime, no GPU)
add_executable(test_file_watcher
    tests/core/test_file_watcher.cpp
)
target_include_directories(test_file_watcher PRIVATE src tests)
target_link_libraries(test_file_watcher PRIVATE vivid_runtime_testlib)
add_test(NAME test_file_watcher COMMAND test_file_watcher WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# HotReloader queueing unit tests (no compiler invocation; package callback path)
add_executable(test_hot_reloader_queue
    tests/core/test_hot_reloader_queue.cpp
)
target_include_directories(test_hot_reloader_queue PRIVATE src tests)
target_link_libraries(test_hot_reloader_queue PRIVATE vivid_runtime_testlib)
add_test(NAME test_hot_reloader_queue COMMAND test_hot_reloader_queue WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_build_console
    tests/core/test_build_console.cpp
)
target_include_directories(test_build_console PRIVATE src tests)
target_link_libraries(test_build_console PRIVATE vivid_runtime_testlib)
add_test(NAME test_build_console COMMAND test_build_console WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# path_util unit tests (header-only, no extra deps)
add_executable(test_path_util
    tests/common/test_path_util.cpp
)
target_include_directories(test_path_util PRIVATE src tests)
target_link_libraries(test_path_util PRIVATE vivid_runtime_testlib)
add_test(NAME test_path_util COMMAND test_path_util WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# HAP helper unit tests (FourCC and format mapping)
add_executable(test_hap_codec
    tests/media/test_hap_codec.cpp
)
target_include_directories(test_hap_codec PRIVATE src tests ${CMAKE_SOURCE_DIR})
target_link_libraries(test_hap_codec PRIVATE vivid_runtime_testlib)
add_test(NAME test_hap_codec COMMAND test_hap_codec WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_movie_decode_upload
    tests/media/test_movie_decode_upload.cpp
    operators/shared/movie_decode/texture_upload.cpp
)
target_include_directories(test_movie_decode_upload PRIVATE src tests ${CMAKE_SOURCE_DIR})
target_link_libraries(test_movie_decode_upload PRIVATE vivid_runtime_testlib webgpu)
add_test(NAME test_movie_decode_upload COMMAND test_movie_decode_upload WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_movie_load_generation
    tests/media/test_movie_load_generation.cpp
)
target_include_directories(test_movie_load_generation PRIVATE src tests ${CMAKE_SOURCE_DIR})
target_link_libraries(test_movie_load_generation PRIVATE vivid_runtime_testlib)
add_test(NAME test_movie_load_generation COMMAND test_movie_load_generation WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_string_ports
    tests/lanes/test_string_ports.cpp
)
target_include_directories(test_string_ports PRIVATE src tests)
target_link_libraries(test_string_ports PRIVATE vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_string_ports test_op_v1 string_source_op string_sink_op)
add_test(NAME test_string_ports COMMAND test_string_ports WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_movie_load_async
    tests/media/test_movie_load_async.cpp
)
target_include_directories(test_movie_load_async PRIVATE src tests ${CMAKE_SOURCE_DIR})
target_link_libraries(test_movie_load_async PRIVATE vivid_runtime_testlib)
add_test(NAME test_movie_load_async COMMAND test_movie_load_async WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_movie_av_sync
    tests/media/test_movie_av_sync.cpp
)
target_include_directories(test_movie_av_sync PRIVATE src tests ${CMAKE_SOURCE_DIR})
target_link_libraries(test_movie_av_sync PRIVATE vivid_runtime_testlib)
add_test(NAME test_movie_av_sync COMMAND test_movie_av_sync WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_port_type_registry
    tests/graph/test_port_type_registry.cpp
)
target_include_directories(test_port_type_registry PRIVATE src tests ${CMAKE_SOURCE_DIR})
target_link_libraries(test_port_type_registry PRIVATE vivid_runtime_testlib)
add_test(NAME test_port_type_registry
    COMMAND test_port_type_registry
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if(APPLE)
    add_executable(test_movie_decode_route
        tests/media/test_movie_decode_route.cpp
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
    target_link_libraries(test_movie_decode_route PRIVATE vivid_runtime_testlib snappy
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
        "-framework Foundation" "-framework QuartzCore")
    add_test(NAME test_movie_decode_route
        COMMAND test_movie_decode_route ${CMAKE_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# UndoManager unit tests (snapshot history behavior)
add_executable(test_undo_manager
    tests/core/test_undo_manager.cpp
)
target_include_directories(test_undo_manager PRIVATE src tests)
target_link_libraries(test_undo_manager PRIVATE vivid_runtime_testlib)
add_test(NAME test_undo_manager COMMAND test_undo_manager WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# editor_detect unit tests (filesystem scan, no display)
add_executable(test_editor_detect
    tests/core/test_editor_detect.cpp
)
target_include_directories(test_editor_detect PRIVATE src tests)
target_link_libraries(test_editor_detect PRIVATE vivid_runtime_testlib)
add_test(NAME test_editor_detect COMMAND test_editor_detect WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_tool_discovery
    tests/core/test_tool_discovery.cpp
)
target_include_directories(test_tool_discovery PRIVATE src tests)
target_link_libraries(test_tool_discovery PRIVATE vivid_runtime_testlib)
add_test(NAME test_tool_discovery COMMAND test_tool_discovery WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_graph_compiler_init
    tests/graph/test_graph_compiler_init.cpp
)
target_include_directories(test_graph_compiler_init PRIVATE src tests)
target_link_libraries(test_graph_compiler_init PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_graph_compiler_init COMMAND test_graph_compiler_init WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_frame_executor_queries
    tests/graph/test_frame_executor_queries.cpp
)
target_include_directories(test_frame_executor_queries PRIVATE src tests)
target_link_libraries(test_frame_executor_queries PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_test(NAME test_frame_executor_queries COMMAND test_frame_executor_queries WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_audio_frame_bridge
    tests/audio/test_audio_frame_bridge.cpp
)
target_include_directories(test_audio_frame_bridge PRIVATE src tests)
target_link_libraries(test_audio_frame_bridge PRIVATE vivid_runtime_testlib vivid_operator_api webgpu)
add_test(NAME test_audio_frame_bridge COMMAND test_audio_frame_bridge WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_subgraph_module
    tests/graph/test_subgraph_module.cpp
)
target_include_directories(test_subgraph_module PRIVATE src tests)
target_link_libraries(test_subgraph_module PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_subgraph_module COMMAND test_subgraph_module WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_graph_compiler
    tests/graph/test_graph_compiler.cpp
)
target_include_directories(test_graph_compiler PRIVATE src tests)
target_link_libraries(test_graph_compiler PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_graph_compiler lfo_fr gain lane_source_op lane_metadata_audio_op lane_slew_op prepare_assets_test_op)
add_test(NAME test_graph_compiler
    COMMAND test_graph_compiler ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_propagation
    tests/lanes/test_lane_propagation.cpp
)
target_include_directories(test_lane_propagation PRIVATE src tests)
target_link_libraries(test_lane_propagation PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_lane_propagation COMMAND test_lane_propagation WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_state tests/lanes/test_lane_state.cpp)
target_include_directories(test_lane_state PRIVATE src tests)
target_link_libraries(test_lane_state PRIVATE vivid_runtime_testlib vivid_operator_api)
add_test(NAME test_lane_state COMMAND test_lane_state WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_capacity
    tests/lanes/test_lane_capacity.cpp
)
target_include_directories(test_lane_capacity PRIVATE src tests)
target_link_libraries(test_lane_capacity PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_lane_capacity lane_source_op lane_slew_op)
add_test(NAME test_lane_capacity
    COMMAND test_lane_capacity ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_compaction
    tests/lanes/test_lane_compaction.cpp
)
target_include_directories(test_lane_compaction PRIVATE src tests)
target_link_libraries(test_lane_compaction PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_lane_compaction identity_lane_source_op lane_state_tracker_op)
add_test(NAME test_lane_compaction
    COMMAND test_lane_compaction ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_equivalence
    tests/lanes/test_lane_equivalence.cpp
)
target_include_directories(test_lane_equivalence PRIVATE src tests)
target_link_libraries(test_lane_equivalence PRIVATE
    vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_lane_equivalence identity_lane_source_op lane_slew_op multi_channel_dc_source_op dc_per_lane_op lane_source_op)
add_test(NAME test_lane_equivalence
    COMMAND test_lane_equivalence ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Undo coverage across roadmap mutation types via RuntimeCommandSink + RuntimeAPI
add_executable(test_undo_mutation_types
    tests/core/test_undo_mutation_types.cpp
)
target_include_directories(test_undo_mutation_types PRIVATE src tests)
target_link_libraries(test_undo_mutation_types PRIVATE
    vivid_runtime_testlib)
add_dependencies(test_undo_mutation_types test_op_v1)
add_test(NAME test_undo_mutation_types
    COMMAND test_undo_mutation_types ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})


# Team workflow regression: project package isolation (Cases A/C: unit; Case B: dylib staging)
add_executable(test_team_workflow_regression
    tests/integration/test_team_workflow_regression.cpp
)
target_include_directories(test_team_workflow_regression PRIVATE src tests)
target_link_libraries(test_team_workflow_regression PRIVATE
    vivid_runtime_testlib)
add_dependencies(test_team_workflow_regression test_op_v1)
add_test(NAME test_team_workflow_regression
    COMMAND test_team_workflow_regression ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# builtin_operators unit tests (no dylib, no GPU)
add_executable(test_builtin_operators
    tests/ops/test_builtin_operators.cpp
)
target_include_directories(test_builtin_operators PRIVATE src tests)
target_link_libraries(test_builtin_operators PRIVATE vivid_runtime_testlib vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_test(NAME test_builtin_operators COMMAND test_builtin_operators WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# OperatorInfoCache unit tests (uses builtin operators as fixtures, no dylib)
add_executable(test_operator_info_cache
    tests/ops/test_operator_info_cache.cpp
)
target_include_directories(test_operator_info_cache PRIVATE src tests)
target_link_libraries(test_operator_info_cache PRIVATE vivid_runtime_testlib vivid_operator_api webgpu nlohmann_json::nlohmann_json)
add_test(NAME test_operator_info_cache COMMAND test_operator_info_cache WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Path Animate unit tests (pure math, no runtime dependencies)
add_executable(test_path_animate tests/core/test_path_animate.cpp)
target_include_directories(test_path_animate PRIVATE src tests)
target_link_libraries(test_path_animate PRIVATE vivid_runtime_testlib)
add_test(NAME test_path_animate COMMAND test_path_animate WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# i18n module tests (load, get, get_plural, fallbacks)
add_executable(test_i18n
    tests/ui/test_i18n.cpp
    src/ui/style/i18n.cpp
)
target_include_directories(test_i18n PRIVATE src tests)
target_link_libraries(test_i18n PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_test(NAME test_i18n COMMAND test_i18n WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Semantic tag vocabulary validation (script-only, no binary)
add_test(NAME test_semantic_tags
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/validate_semantic_tags.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
