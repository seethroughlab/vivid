# Demo graph smoke test (loads every demo graph, builds runtime, ticks 5 frames)
add_executable(test_demo_graphs
    tests/integration/test_demo_graphs.cpp
)
target_include_directories(test_demo_graphs PRIVATE src tests)
target_link_libraries(test_demo_graphs PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_demo_graphs
    # control
    lfo_fr lfo_au clock_fr clock_au math envelope_fr envelope_au midi_input fft_analysis
    logic gate_au smooth_fr smooth_au
    stack alternate
    modulated_gain
    # gpu (compiled plugins — WGSL filter presets are loaded from filters/)
    noise shape composite
    bloom feedback movie_file texture_analysis metronome_viz
    # audio
    oscillator gain
    reverb convolution_reverb delay bitcrush distortion filter audio_noise mixer
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
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_media_headless
    movie_file
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
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
add_dependencies(test_audio_sequencer_graph
    clock_au oscillator gain drum_sequencer_au drum_kit_au drum_kick)
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
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu rtmidi)
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

# Operator descriptor hash test (stability + sensitivity of fingerprint)
add_executable(test_operator_descriptor_hash
    tests/operators/test_operator_descriptor_hash.cpp
)
target_include_directories(test_operator_descriptor_hash PRIVATE src tests)
target_link_libraries(test_operator_descriptor_hash PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_operator_descriptor_hash
    COMMAND test_operator_descriptor_hash
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_operator_descriptor_hash PROPERTIES LABELS "PACKAGE" TIMEOUT 15)

# ProjectLockfile JSON model + parser test (round-trip, canonical order, version validation)
add_executable(test_project_lockfile
    tests/packages/test_project_lockfile.cpp
)
target_include_directories(test_project_lockfile PRIVATE src tests)
target_link_libraries(test_project_lockfile PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_project_lockfile
    COMMAND test_project_lockfile
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_project_lockfile PROPERTIES LABELS "PACKAGE" TIMEOUT 15)

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
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_package_test_runner
    COMMAND test_package_test_runner ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_package_test_runner PROPERTIES LABELS "PACKAGE")

add_executable(test_package_contract_ecosystem
    tests/packages/test_package_contract_ecosystem.cpp
)
target_include_directories(test_package_contract_ecosystem PRIVATE src tests)
target_link_libraries(test_package_contract_ecosystem PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
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

# FileWatcher unit tests (efsw, no runtime, no GPU)
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

# ProcessRunner unit tests (argv-based process execution, no shell)
add_executable(test_process_runner
    tests/core/test_process_runner.cpp
)
target_include_directories(test_process_runner PRIVATE src tests)
target_link_libraries(test_process_runner PRIVATE vivid_runtime_testlib)
add_test(NAME test_process_runner COMMAND test_process_runner WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_build_activity_queries
    tests/control/test_build_activity_queries.cpp
)
target_include_directories(test_build_activity_queries PRIVATE src tests)
target_link_libraries(test_build_activity_queries PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_test(NAME test_build_activity_queries COMMAND test_build_activity_queries WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

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
    operators/shared/movie_session/movie_transport.cpp
)
target_include_directories(test_movie_av_sync PRIVATE src tests ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/operators/shared/movie_session)
target_link_libraries(test_movie_av_sync PRIVATE vivid_runtime_testlib)
add_test(NAME test_movie_av_sync COMMAND test_movie_av_sync WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_movie_transport
    tests/media/test_movie_transport.cpp
    operators/shared/movie_session/movie_transport.cpp
    operators/shared/movie_session/playback_session.cpp
    operators/shared/movie_session/session_registry.cpp
)
target_include_directories(test_movie_transport PRIVATE
    src tests ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/operators/shared/movie_session)
target_link_libraries(test_movie_transport PRIVATE vivid_runtime_testlib)
add_test(NAME test_movie_transport COMMAND test_movie_transport WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_decoded_frame_queue
    tests/media/test_decoded_frame_queue.cpp
    operators/shared/movie_session/decoded_frame_queue.cpp
)
target_include_directories(test_decoded_frame_queue PRIVATE
    src tests ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/operators/shared/movie_session)
target_link_libraries(test_decoded_frame_queue PRIVATE vivid_runtime_testlib)
add_test(NAME test_decoded_frame_queue COMMAND test_decoded_frame_queue WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_video_decode_worker
    tests/media/test_video_decode_worker.cpp
    operators/shared/movie_session/decoded_frame_queue.cpp
    operators/shared/movie_session/video_decode_worker.cpp
)
target_include_directories(test_video_decode_worker PRIVATE
    src tests ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/operators/shared/movie_session)
target_link_libraries(test_video_decode_worker PRIVATE vivid_runtime_testlib)
add_test(NAME test_video_decode_worker COMMAND test_video_decode_worker WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_avf_acquired_pixel_buffer
    tests/media/test_avf_acquired_pixel_buffer.mm
    operators/shared/movie_decode/avf_decoder.mm
)
target_include_directories(test_avf_acquired_pixel_buffer PRIVATE
    src tests ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/operators/shared/movie_decode
    ${CMAKE_SOURCE_DIR}/operators/shared/movie_session)
target_link_libraries(test_avf_acquired_pixel_buffer PRIVATE
    vivid_runtime_testlib
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework Foundation" "-framework QuartzCore" "-framework Metal")
add_test(NAME test_avf_acquired_pixel_buffer COMMAND test_avf_acquired_pixel_buffer WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_movie_playback_modes
    tests/media/test_movie_playback_modes.cpp
    operators/shared/movie_session/movie_transport.cpp
    operators/shared/movie_session/playback_session.cpp
    operators/shared/movie_session/session_registry.cpp
    operators/shared/movie_session/decoded_frame_queue.cpp
    operators/shared/movie_session/video_decode_worker.cpp
)
target_include_directories(test_movie_playback_modes PRIVATE
    src tests ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/operators/shared/movie_session)
target_link_libraries(test_movie_playback_modes PRIVATE vivid_runtime_testlib)
add_test(NAME test_movie_playback_modes COMMAND test_movie_playback_modes WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_movie_seek_stress
    tests/media/test_movie_seek_stress.cpp
    operators/shared/movie_session/movie_transport.cpp
)
target_include_directories(test_movie_seek_stress PRIVATE
    src tests ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/operators/shared/movie_session)
target_link_libraries(test_movie_seek_stress PRIVATE vivid_runtime_testlib)
add_test(NAME test_movie_seek_stress COMMAND test_movie_seek_stress WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

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
    target_include_directories(test_movie_decode_route PRIVATE
        src tests ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/operators/shared/movie_decode
        ${CMAKE_SOURCE_DIR}/operators/shared/movie_session
        ${CMAKE_SOURCE_DIR}/deps/hap)
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
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
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
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_subgraph_module COMMAND test_subgraph_module WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_graph_compiler
    tests/graph/test_graph_compiler.cpp
)
target_include_directories(test_graph_compiler PRIVATE src tests)
target_link_libraries(test_graph_compiler PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_graph_compiler
    lfo_fr gain lane_source_op lane_metadata_audio_op lane_slew_op prepare_assets_test_op
    drum_sequencer_au drum_kit_au)
add_test(NAME test_graph_compiler
    COMMAND test_graph_compiler ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_propagation
    tests/lanes/test_lane_propagation.cpp
)
target_include_directories(test_lane_propagation PRIVATE src tests)
target_link_libraries(test_lane_propagation PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
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
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_dependencies(test_lane_capacity lane_source_op lane_slew_op)
add_test(NAME test_lane_capacity
    COMMAND test_lane_capacity ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_compaction
    tests/lanes/test_lane_compaction.cpp
)
target_include_directories(test_lane_compaction PRIVATE src tests)
target_link_libraries(test_lane_compaction PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
add_dependencies(test_lane_compaction identity_lane_source_op lane_state_tracker_op)
add_test(NAME test_lane_compaction
    COMMAND test_lane_compaction ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(test_lane_equivalence
    tests/lanes/test_lane_equivalence.cpp
)
target_include_directories(test_lane_equivalence PRIVATE src tests)
target_link_libraries(test_lane_equivalence PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json miniaudio webgpu)
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
