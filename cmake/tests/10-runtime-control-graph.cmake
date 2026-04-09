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
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio ixwebsocket webgpu rtmidi stb_truetype
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework VideoToolbox" "-framework Foundation" "-framework QuartzCore")
add_dependencies(test_perception_introspection test_op_v1 oscillator shape)
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
