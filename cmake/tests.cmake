# --- Shared runtime library for tests (eliminates redundant compilation) ---
add_library(vivid_runtime_testlib STATIC
    src/runtime/audio/audio_engine.cpp
    src/runtime/audio/audio_frame_bridge.cpp
    src/runtime/audio/system_midi.cpp
    src/runtime/control/control_server.cpp
    src/runtime/control/control_server_checks.cpp
    src/runtime/control/control_server_dispatch.cpp
    src/runtime/control/control_server_query.cpp
    src/runtime/control/graph_file_io.cpp
    src/runtime/control/runtime_api.cpp
    src/runtime/control/runtime_api_live.cpp
    src/runtime/control/runtime_api_variations.cpp
    src/runtime/control/runtime_api_persistence.cpp
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
    src/runtime/core/workspace_manager.cpp
    src/runtime/debug/capture_coordinator.cpp
    src/runtime/debug/output_analyzer.cpp
    src/runtime/gpu/screenshot.cpp
    src/runtime/gpu/wgsl_header_parser.cpp
    src/runtime/graph/audio_executor.cpp
    src/runtime/graph/frame_executor.cpp
    src/runtime/graph/graph.cpp
    src/runtime/graph/graph_compiler.cpp
    src/runtime/graph/graph_compiler_init.cpp
    src/runtime/graph/graph_compiler_planning.cpp
    src/runtime/graph/graph_compiler_reload.cpp
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
    src/runtime/packages/package_manager_discovery.cpp
    src/runtime/packages/package_manager_manifest.cpp
    src/runtime/packages/package_manager_install.cpp
    src/runtime/packages/package_manager_build.cpp
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
# Shared test operator fixtures and support plugins.
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

# Additional test-only fixture operators consumed by the grouped test includes.
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
add_vivid_operator(gpu_fill_op      tests/operators/gpu_fill_op.cpp EXTRA_LIBS webgpu)

include(cmake/tests/10-runtime-control-graph.cmake)
include(cmake/tests/20-ui-and-common.cmake)
include(cmake/tests/30-ops-stability-domains.cmake)
include(cmake/tests/40-packages-media-misc.cmake)
