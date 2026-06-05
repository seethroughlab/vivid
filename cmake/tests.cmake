# --- Shared runtime library for tests (eliminates redundant compilation) ---
add_library(vivid_runtime_testlib STATIC
    src/runtime/audio/audio_engine.cpp
    src/runtime/audio/audio_frame_bridge.cpp
    src/runtime/audio/audio_device_list.cpp
    src/runtime/audio/system_midi.cpp
    src/runtime/assets/asset_library.cpp
    src/runtime/assets/asset_library_internal.cpp
    src/runtime/assets/asset_library_discovery.cpp
    src/runtime/assets/asset_library_metadata.cpp
    src/runtime/assets/asset_library_import.cpp
    src/runtime/control/control_server.cpp
    src/runtime/control/control_server_assets.cpp
    src/runtime/control/control_server_checks.cpp
    src/runtime/control/control_server_crash.cpp
    src/runtime/control/control_server_dispatch.cpp
    src/runtime/control/control_server_query.cpp
    src/runtime/control/control_server_query_source.cpp
    src/runtime/control/control_server_query_packages.cpp
    src/runtime/control/graph_file_io.cpp
    src/runtime/control/runtime_api.cpp
    src/runtime/control/runtime_api_live.cpp
    src/runtime/control/runtime_api_variations.cpp
    src/runtime/control/runtime_api_session.cpp
    src/runtime/control/runtime_api_modulation.cpp
    src/runtime/control/runtime_api_persistence.cpp
    src/runtime/control/runtime_command_sink.cpp
    src/runtime/core/editor_detect.cpp
    src/runtime/core/file_drop_registry.cpp
    src/runtime/core/file_watcher.cpp
    src/runtime/core/hot_reload.cpp
    src/runtime/core/source_index.cpp
    src/runtime/core/runtime_bootstrap.cpp
    src/runtime/core/runtime_core.cpp
    src/runtime/core/runtime_health.cpp
    src/runtime/core/runtime_health_samplers.cpp
    src/runtime/core/settings.cpp
    src/runtime/core/crash_recovery.cpp
    src/runtime/core/quarantine.cpp
    src/runtime/core/system_requirements.cpp
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
    src/runtime/graph/operator_aliases.cpp
    src/runtime/graph/graph_snapshot_builder.cpp
    src/runtime/graph/graph_compiler.cpp
    src/runtime/graph/graph_compiler_init.cpp
    src/runtime/graph/graph_compiler_planning.cpp
    src/runtime/graph/graph_compiler_reload.cpp
    src/runtime/graph/lane_buffer_gpu.cpp
    src/runtime/graph/port_type_registry.cpp
    src/runtime/graph/subgraph_module.cpp
    src/runtime/operators/builtin_operators.cpp
    src/runtime/operators/operator_creator.cpp
    src/runtime/operators/operator_loader.cpp
    src/runtime/operators/operator_registry.cpp
    src/runtime/operators/operator_registry_scan.cpp
    src/runtime/operators/operator_registry_lookup.cpp
    src/runtime/operators/operator_registry_metadata.cpp
    src/runtime/operators/operator_registry_diagnostics.cpp
    src/runtime/operators/operator_descriptor_hash.cpp
    src/runtime/operators/operator_descriptor_validation.cpp
    src/runtime/operators/operator_source_docs.cpp
    src/runtime/operators/project_package.cpp
    src/runtime/packages/package_catalog.cpp
    src/runtime/packages/package_compiler.cpp
    src/runtime/packages/package_manager.cpp
    src/runtime/packages/package_manager_discovery.cpp
    src/runtime/packages/package_manager_manifest.cpp
    src/runtime/packages/package_manager_install.cpp
    src/runtime/packages/package_manager_build.cpp
    src/runtime/packages/project_lockfile.cpp
    src/common/hash_util.cpp
    src/runtime/packages/package_scaffolder.cpp
    src/runtime/packages/package_test_runner.cpp
    src/runtime/platform/app_update_manager.cpp
    src/runtime/platform/av_exporter.mm
    src/runtime/platform/platform.cpp
    src/runtime/platform/process_runner.cpp
    src/runtime/net/http_fetch.cpp
)
target_include_directories(vivid_runtime_testlib PUBLIC src tests)
target_link_libraries(vivid_runtime_testlib PUBLIC
    vivid_operator_api vivid_source_syntax nlohmann_json::nlohmann_json dragonbox::dragonbox_to_chars webgpu
    miniaudio rtmidi snappy stb_truetype ixwebsocket efsw tinyxml2 CURL::libcurl)
if(VIVID_ENABLE_HIGHWAY)
    target_link_libraries(vivid_runtime_testlib PUBLIC hwy)
    target_compile_definitions(vivid_runtime_testlib PUBLIC
        VIVID_HAS_HIGHWAY=1
        "VIVID_DRAGONBOX_INCLUDE_DIR=\"${dragonbox_SOURCE_DIR}/include\""
        "VIVID_DRAGONBOX_LIBRARY_PATH=\"$<TARGET_FILE:dragonbox::dragonbox_to_chars>\""
        "VIVID_HIGHWAY_INCLUDE_DIR=\"${highway_SOURCE_DIR}\""
        "VIVID_HIGHWAY_LIBRARY_PATH=\"$<TARGET_FILE:hwy>\"")
else()
    target_compile_definitions(vivid_runtime_testlib PUBLIC
        "VIVID_DRAGONBOX_INCLUDE_DIR=\"${dragonbox_SOURCE_DIR}/include\""
        "VIVID_DRAGONBOX_LIBRARY_PATH=\"$<TARGET_FILE:dragonbox::dragonbox_to_chars>\"")
endif()
target_compile_definitions(vivid_runtime_testlib PRIVATE
    "VIVID_CORE_VERSION=\"${PROJECT_VERSION}\"")
if(APPLE AND VIVID_ENABLE_ACCELERATE)
    target_compile_definitions(vivid_runtime_testlib PUBLIC VIVID_HAS_ACCELERATE=1)
    target_link_libraries(vivid_runtime_testlib PUBLIC "-framework Accelerate")
endif()
# Core git metadata (for project_lockfile.cpp's lf.vivid_core.commit field).
target_compile_definitions(vivid_runtime_testlib PUBLIC
    "VIVID_CORE_COMMIT=\"${VIVID_CORE_COMMIT}\""
    "VIVID_CORE_REPO_URL=\"${VIVID_CORE_REPO_URL}\"")
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
#
# add_vivid_test_fixture is add_vivid_operator + TEST_FIXTURE. It keeps the
# fixture dylibs at build/ (where tests load them from via
# WORKING_DIRECTORY) but prevents them from racing with vivid's POST_BUILD
# codesign and polluting the shipped bundle with test-only plugins.
# Tests that need a fixture inside Vivid.app (currently only
# test_ui_screenshot_smoke for the file_drop ops) stage it explicitly and
# call vivid_codesign_bundle() to re-seal afterwards.
function(add_vivid_test_fixture name source)
    add_vivid_operator(${name} ${source} ${ARGN} TEST_FIXTURE)
endfunction()

add_vivid_test_fixture(test_op_v1     tests/operators/test_op_v1.cpp CODEGEN)
add_vivid_test_fixture(test_op_v2     tests/operators/test_op_v2.cpp CODEGEN)
add_library(test_op_abi_v4 MODULE tests/operators/test_op_abi_v4.cpp)
target_link_libraries(test_op_abi_v4 PRIVATE vivid_runtime_testlib vivid_operator_api)
set_target_properties(test_op_abi_v4 PROPERTIES
    PREFIX ""
    SUFFIX ${VIVID_PLUGIN_SUFFIX}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_vivid_test_fixture(test_op_incompatible_port tests/operators/test_op_incompatible_port.cpp CODEGEN)
add_vivid_test_fixture(test_op_null_desc tests/operators/test_op_null_desc.cpp)
add_vivid_test_fixture(test_op_advanced_port tests/operators/test_op_advanced_port.cpp CODEGEN)
add_vivid_test_fixture(control_thumb_op tests/operators/control_thumb_op.cpp CODEGEN EXTRA_LIBS webgpu)
add_vivid_test_fixture(editor_test_op tests/operators/editor_test_op.cpp CODEGEN)
add_vivid_test_fixture(test_op_bad_custom_type tests/operators/test_op_bad_custom_type.cpp CODEGEN)
add_vivid_test_fixture(audio_reload_v1 tests/operators/audio_reload_v1.cpp CODEGEN)
add_vivid_test_fixture(audio_reload_v2 tests/operators/audio_reload_v2.cpp CODEGEN)
add_vivid_test_fixture(audio_reload_v3 tests/operators/audio_reload_v3.cpp CODEGEN)
add_vivid_test_fixture(audio_reload_incompatible tests/operators/audio_reload_incompatible.cpp CODEGEN)
add_vivid_test_fixture(export_custom_port_op tests/operators/export_custom_port_op.cpp CODEGEN)
add_vivid_test_fixture(test_state_carry_op tests/operators/test_state_carry_op.cpp CODEGEN)
add_vivid_test_fixture(string_source_op tests/operators/string_source_op.cpp CODEGEN)
add_vivid_test_fixture(string_sink_op tests/operators/string_sink_op.cpp CODEGEN)
add_vivid_test_fixture(file_drop_test_op tests/operators/file_drop_test_op.cpp CODEGEN)
add_vivid_test_fixture(file_drop_test_op_alt tests/operators/file_drop_test_op_alt.cpp CODEGEN)
add_vivid_test_fixture(file_drop_bad_param_op tests/operators/file_drop_bad_param_op.cpp CODEGEN)
add_vivid_test_fixture(prepare_assets_test_op tests/operators/prepare_assets_test_op.cpp CODEGEN)
add_vivid_test_fixture(prepare_assets_legacy_op tests/operators/prepare_assets_legacy_op.cpp)
add_vivid_test_fixture(semantic_ms_source_op tests/operators/semantic_ms_source_op.cpp CODEGEN)
add_vivid_test_fixture(semantic_s_dest_op    tests/operators/semantic_s_dest_op.cpp CODEGEN)
add_vivid_test_fixture(semantic_unknown_source_op tests/operators/semantic_unknown_source_op.cpp CODEGEN)
add_vivid_test_fixture(untagged_dest_op          tests/operators/untagged_dest_op.cpp CODEGEN)

# Additional test-only fixture operators consumed by the grouped test includes.
add_vivid_test_fixture(audio_test_op      tests/operators/audio_test_op.cpp CODEGEN)
add_vivid_test_fixture(audio_scalar_probe_op tests/operators/audio_scalar_probe_op.cpp CODEGEN)
add_vivid_test_fixture(audio_throwing_op  tests/operators/audio_throwing_op.cpp CODEGEN)
add_vivid_test_fixture(control_pass_op    tests/operators/control_pass_op.cpp CODEGEN)
add_vivid_test_fixture(lane_source_op   tests/operators/lane_source_op.cpp CODEGEN)
add_vivid_test_fixture(lane_sink_op     tests/operators/lane_sink_op.cpp CODEGEN)
add_vivid_test_fixture(lane_metadata_op       tests/operators/lane_metadata_op.cpp CODEGEN)
add_vivid_test_fixture(lane_metadata_audio_op tests/operators/lane_metadata_audio_op.cpp CODEGEN)
add_vivid_test_fixture(lane_smooth_op         tests/operators/lane_smooth_op.cpp CODEGEN)
add_vivid_test_fixture(lane_slew_op           tests/operators/lane_slew_op.cpp CODEGEN)
add_vivid_test_fixture(identity_lane_source_op tests/operators/identity_lane_source_op.cpp CODEGEN)
add_vivid_test_fixture(lane_state_tracker_op     tests/operators/lane_state_tracker_op.cpp CODEGEN)
add_vivid_test_fixture(multi_channel_dc_source_op tests/operators/multi_channel_dc_source_op.cpp CODEGEN)
add_vivid_test_fixture(dc_per_lane_op            tests/operators/dc_per_lane_op.cpp CODEGEN)
add_vivid_test_fixture(lane_frame_op             tests/operators/lane_frame_op.cpp CODEGEN)
add_vivid_test_fixture(audio_lane_op    tests/operators/audio_lane_op.cpp CODEGEN)
add_vivid_test_fixture(audio_reduce_op  tests/operators/audio_reduce_op.cpp CODEGEN)
add_vivid_test_fixture(scalar_source_op  tests/operators/scalar_source_op.cpp CODEGEN)
add_vivid_test_fixture(dual_lane_sink_op tests/operators/dual_lane_sink_op.cpp CODEGEN)
add_vivid_test_fixture(gpu_fill_op      tests/operators/gpu_fill_op.cpp CODEGEN EXTRA_LIBS webgpu)
add_vivid_test_fixture(gpu_metronome_probe_op tests/operators/gpu_metronome_probe_op.cpp CODEGEN EXTRA_LIBS webgpu)

# SIMD smoke operator fixture (Highway when available, scalar fallback)
add_vivid_test_fixture(simd_smoke_op tests/operators/simd_smoke_op.cpp CODEGEN)
target_include_directories(simd_smoke_op PRIVATE tests/operators)
if(VIVID_ENABLE_HIGHWAY)
    target_link_libraries(simd_smoke_op PRIVATE hwy)
    target_compile_definitions(simd_smoke_op PRIVATE
        VIVID_HAS_HIGHWAY=1
        "VIVID_HIGHWAY_INCLUDE_DIR=\"${highway_SOURCE_DIR}\""
        "VIVID_HIGHWAY_LIBRARY_PATH=\"$<TARGET_FILE:hwy>\"")
endif()

include(cmake/tests/10-runtime-control-graph.cmake)
# SourceSyntaxParser unit tests — tree-sitter parsing foundation (Phase 1)
add_executable(test_source_syntax_parser
    tests/core/test_source_syntax_parser.cpp
)
target_include_directories(test_source_syntax_parser PRIVATE src tests)
target_link_libraries(test_source_syntax_parser PRIVATE
    vivid_operator_api vivid_source_syntax
    nlohmann_json::nlohmann_json
)
add_test(NAME test_source_syntax_parser COMMAND test_source_syntax_parser)
set_tests_properties(test_source_syntax_parser PROPERTIES TIMEOUT 15)

add_executable(test_operator_codegen
    tests/core/test_operator_codegen.cpp
    tools/operator_codegen/descriptor_builder.cpp
    tools/operator_codegen/uniform_codegen.cpp
)
target_include_directories(test_operator_codegen PRIVATE src tests ${CMAKE_SOURCE_DIR})
target_link_libraries(test_operator_codegen PRIVATE vivid_source_syntax vivid_wgsl_uniform_layout)
target_compile_definitions(test_operator_codegen PRIVATE
    "VIVID_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
add_test(NAME test_operator_codegen COMMAND test_operator_codegen)
set_tests_properties(test_operator_codegen PROPERTIES TIMEOUT 15)

include(cmake/tests/20-ui-and-common.cmake)
include(cmake/tests/30-ops-stability-domains.cmake)
include(cmake/tests/40-packages-media-misc.cmake)
include(cmake/tests/50-assets.cmake)
include(cmake/tests/90-production-gate.cmake)
