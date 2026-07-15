# Headless, portable test suite — no GUI/GPU/audio/VST deps, so it configures and
# runs anywhere (Linux CI included) and stays fast. Each test is a standalone
# executable that returns its failure count; ctest groups them by label tier.
find_package(Threads REQUIRED)   # std::thread in test_hot_reload

add_library(vivid_test_helpers INTERFACE)
target_include_directories(vivid_test_helpers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# vivid_add_test(<name> [LABEL])  — adds tests/<name>.cpp as a ctest case.
function(vivid_add_test name)
    set(label "HEADLESS_SMOKE")
    if(ARGV1)
        set(label "${ARGV1}")
    endif()
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE vivid_test_helpers)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "${label}")
endfunction()

vivid_add_test(test_mapping)
vivid_add_test(test_undo_manager)   # ADR-0017/G1 undo history cursor (pure)
target_sources(test_undo_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/undo_manager.cpp)
vivid_add_test(test_undo_projection)  # ADR-0017/G1 canonical projection + audio tiering (pure)
target_sources(test_undo_projection PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/persist_undo.cpp)
target_link_libraries(test_undo_projection PRIVATE nlohmann_json::nlohmann_json)
vivid_add_test(test_param_widget)   # inspector widget classifier (type/hint/enum -> widget)
vivid_add_test(test_shader_library_view)  # ADR-0021/P1 shader-view geometry + fork-name helpers
vivid_add_test(test_examples)             # ADR-0021/P2 example-project enumerator
target_sources(test_examples PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/examples.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp)
vivid_add_test(test_node_presets)         # ADR-0021/P4 node-preset storage (save/list/load/remove)
target_sources(test_node_presets PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/node_presets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp)
target_link_libraries(test_node_presets PRIVATE nlohmann_json::nlohmann_json)
vivid_add_test(test_note_ops)       # pure MIDI note-list transforms (midi/note_ops.h)
vivid_add_test(test_expr_curve)     # M3 per-note expression curve + scheduler emission
vivid_add_test(test_clip_loop)      # in-clip loop region scheduler wrapping (M2-followup)
vivid_add_test(test_audio_op_rt)    # AO-1 native audio-operator runtime: zero-alloc + note delivery
target_sources(test_audio_op_rt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/audio_op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/builtin_audio_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/glitch/glitch_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
vivid_add_test(test_audio_graph)    # AG-0 per-track audio DAG: topo/sum/cycle + zero-alloc
target_sources(test_audio_graph PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/audio_graph.cpp)
vivid_add_test(test_sampler_op)     # AO-4 native Sampler: slice-per-note + zero-alloc
target_sources(test_sampler_op PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/audio_op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/builtin_audio_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/glitch/glitch_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
# ADR-0015 / M4: the Arp note effect — notes in, notes out. The proof that notes are a real signal
# in the graph (M1's note edges are inaudible on their own), plus its zero-alloc RT contract.
vivid_add_test(test_arp_op)
target_sources(test_arp_op PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/audio_op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/builtin_audio_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/glitch/glitch_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)

vivid_add_test(test_note_tools)     # M5 musical transforms (midi/note_tools.h)
vivid_add_test(test_audio_warp)     # A0 pure audio-warp math (audio/audio_clip_shared.h)
target_link_libraries(test_audio_warp PRIVATE nlohmann_json::nlohmann_json)
vivid_add_test(test_audio_clip)     # A1 clip playback shaping (gain/reverse/fades)
target_link_libraries(test_audio_clip PRIVATE nlohmann_json::nlohmann_json)
vivid_add_test(test_clip_dsp)       # A2 warp engine smoke (signalsmith stretch path)
target_link_libraries(test_clip_dsp PRIVATE signalsmith_stretch nlohmann_json::nlohmann_json)
vivid_add_test(test_control_parse)

# ADR-0014: the visual output's format — the aspect/size preset tables + the Fit/Fill/Stretch UV
# window that letterboxes the output into a surface of a different shape (gpu/output_format.h).
vivid_add_test(test_output_format)

# What a plugin IS, from its factory metadata (audio/plugin_class.h) — pure string logic, no SDK.
vivid_add_test(test_plugin_class)

# The plugin probe's on-disk memory (audio/plugin_cache.h): staleness + round-trip. Notably, a
# plugin that crashed the probe must never be retried automatically.
vivid_add_test(test_plugin_cache)
target_sources(test_plugin_cache PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/plugin_cache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp)
target_link_libraries(test_plugin_cache PRIVATE nlohmann_json::nlohmann_json)
vivid_add_test(test_project_paths)   # project-folder vs single-.json path convention
vivid_add_test(test_control_errors)
target_link_libraries(test_control_errors PRIVATE nlohmann_json::nlohmann_json)
vivid_add_test(test_control_json)    # discovery serializer: descriptor -> rich JSON (semantic metadata)
target_link_libraries(test_control_json PRIVATE nlohmann_json::nlohmann_json)

# Operator descriptor validation (lifted operator_api) — compiles the validator
# .cpp into the test (header + stdlib only, so still headless/portable).
vivid_add_test(test_descriptor_validation)
target_sources(test_descriptor_validation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)

# In-process operator runtime (gpu/op_runtime is wgpu-free → headless).
vivid_add_test(test_op_registry)
target_sources(test_op_registry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)

# Pre-P1 session migration (legacy VOp-int -> op-name). persist.h pulls nlohmann.
vivid_add_test(test_persist_chain_migration)
target_link_libraries(test_persist_chain_migration PRIVATE nlohmann_json::nlohmann_json)

# Visuals executor topological order + cycle handling (gpu/graph_topo.h; header-only, wgpu-free).
vivid_add_test(test_graph_topo)

# Launch window sizing policy: compute_launch_rect (pure) + a prefs save/load round-trip.
vivid_add_test(test_window_prefs)
target_sources(test_window_prefs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/window_prefs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp)
target_link_libraries(test_window_prefs PRIVATE nlohmann_json::nlohmann_json)

# P4.1: session-version load guard (classify_session_version is a pure header fn).
vivid_add_test(test_version_guard)
target_link_libraries(test_version_guard PRIVATE nlohmann_json::nlohmann_json)

# Dynamic tracks: stable-id drop-on-delete of mappings (mapping.h is header-only -> headless).
vivid_add_test(test_track_drop_mappings)

# Minimal project workflow: current path/recent files + media-root diagnostics.
vivid_add_test(test_project_workflow)

# P4.3: runtime-health rollup + serialization (severity()/to_json() are App-free).
vivid_add_test(test_runtime_health)
target_sources(test_runtime_health PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/runtime_health.cpp)
target_link_libraries(test_runtime_health PRIVATE nlohmann_json::nlohmann_json)

# P4.4: semantic-metadata contract — descriptors built via register_op (wgpu-free) are
# checked against the vocabulary in operator_api/semantic_vocab.h.
vivid_add_test(test_semantic_metadata)
target_sources(test_semantic_metadata PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)

# --- P2.1: dlopen operator loader + LoadedOperator adapter ---
# GPU-free fixture operators built as loadable modules; the tests dlopen them by
# explicit path (so they need no wgpu device and stay headless/portable).
add_library(test_fixture_op MODULE fixtures/test_fixture_op.cpp)
target_include_directories(test_fixture_op PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_library(test_fixture_bad MODULE fixtures/test_fixture_bad.cpp)
target_include_directories(test_fixture_bad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Loader: dlopen + ABI/symbol/descriptor validation (operator_loader is wgpu-free).
vivid_add_test(test_operator_loader)
target_sources(test_operator_loader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_operator_loader PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(test_operator_loader test_fixture_op test_fixture_bad)
target_compile_definitions(test_operator_loader PRIVATE
    FIXTURE_OP_PATH="$<TARGET_FILE:test_fixture_op>"
    FIXTURE_BAD_PATH="$<TARGET_FILE:test_fixture_bad>")

# ADR-0021/P3: the file-drop registry, against a real fixture dylib that declares a drop handler.
vivid_add_test(test_file_drop_registry)
target_sources(test_file_drop_registry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/file_drop_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_file_drop_registry PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(test_file_drop_registry test_fixture_op)
target_compile_definitions(test_file_drop_registry PRIVATE
    FIXTURE_OP_PATH="$<TARGET_FILE:test_fixture_op>")

# Adapter: LoadedOperator mirrors a dylib descriptor through OpRegistry losslessly.
vivid_add_test(test_loaded_operator)
target_sources(test_loaded_operator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/loaded_operator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_loaded_operator PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(test_loaded_operator test_fixture_op)
target_compile_definitions(test_loaded_operator PRIVATE
    FIXTURE_OP_PATH="$<TARGET_FILE:test_fixture_op>")

# P2.2′: operator discovery metadata (display_name/keywords/summary) reaches the
# descriptor for both a compiled-in op and a loaded dylib op.
vivid_add_test(test_op_metadata)
target_sources(test_op_metadata PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/loaded_operator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_op_metadata PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(test_op_metadata test_fixture_op)
target_compile_definitions(test_op_metadata PRIVATE
    FIXTURE_OP_PATH="$<TARGET_FILE:test_fixture_op>")

# Manifest parsing only (pure, no clang++/wgpu): the operator `kind` field + wgpu-link defaulting.
vivid_add_test(test_package_manifest)
target_sources(test_package_manifest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_manifest.cpp)
target_link_libraries(test_package_manifest PRIVATE nlohmann_json::nlohmann_json)

# P2.3: package pipeline — parse manifest, compile the operator via clang++, load it.
# Compiles a GPU-free fixture op so it needs no wgpu at test time (portable/headless).
vivid_add_test(test_package_compile)
target_sources(test_package_compile PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_manifest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_compiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/loaded_operator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_scan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_package_compile PRIVATE ${CMAKE_DL_LIBS} nlohmann_json::nlohmann_json)
target_compile_definitions(test_package_compile PRIVATE
    PKG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pkg"
    VIVID_PKG_CXX="${CMAKE_CXX_COMPILER}"
    VIVID_PKG_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../src"
    VIVID_PKG_ARCH="${CMAKE_OSX_ARCHITECTURES}")

# Audio operator packages end-to-end: compile the example-audio package (audio_effect +
# instrument), load both, and RUN them through the native audio runtime (proves the loaded-dylib
# adapter forwards process_audio + the capability flags come from the dylib).
vivid_add_test(test_package_audio)
target_sources(test_package_audio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_manifest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_compiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/loaded_operator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_scan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/audio_op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_package_audio PRIVATE ${CMAKE_DL_LIBS} nlohmann_json::nlohmann_json)
target_compile_definitions(test_package_audio PRIVATE
    PKG_AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../operators/packages/example-audio"
    VIVID_PKG_CXX="${CMAKE_CXX_COMPILER}"
    VIVID_PKG_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../src"
    VIVID_PKG_ARCH="${CMAKE_OSX_ARCHITECTURES}")

# P2b: "Clone & Edit a built-in" — compile the real WGSL Plasma-clone template via the
# package compiler and register it (a GPU op, so it needs the webgpu include/lib dirs).
# App-only: the clone compiles against <webgpu/webgpu.h>, which is fetched with the GUI/GPU
# deps (VIVID_BUILD_APP=ON). In the portable tests-only gate (VIVID_BUILD_APP=OFF) webgpu
# isn't available, so this test is skipped there; it runs in the full-app / release gate.
if(VIVID_BUILD_APP)
vivid_add_test(test_operator_clone)
target_sources(test_operator_clone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/operator_clone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_manifest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_compiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/loaded_operator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_scan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_operator_clone PRIVATE ${CMAKE_DL_LIBS} nlohmann_json::nlohmann_json)
# NOTE: the wgpu dist is fetched by the PARENT *after* this subdir is processed, so the
# _deps/wgpu-*-src dir does not exist yet at this point — globbing for it here silently yields
# an empty path on a fresh configure (it only appeared to work in a tree that had been
# reconfigured, where the previous configure had left _deps populated). The two webgpu
# defines are therefore set by the parent, right after it resolves the dist root.
target_compile_definitions(test_operator_clone PRIVATE
    VIVID_PKG_CXX="${CMAKE_CXX_COMPILER}"
    VIVID_PKG_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../src"
    VIVID_PKG_ARCH="${CMAKE_OSX_ARCHITECTURES}")

# ADR-0016 / S3: the shader library — a .wgsl in a folder becomes a registry TYPE whose params
# reach the operator ABI. App-only for the same reason as above: ShaderFileOp holds WGPU handles
# (it never touches a device, so the test itself stays headless).
vivid_add_test(test_shader_library)
target_sources(test_shader_library PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/shader_library.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/file_watcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/shader_file_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/shader_meta.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_shader_library PRIVATE nlohmann_json::nlohmann_json)
# NOTE: `webgpu` (the target that carries the wgpu include/lib dirs) is fetched by the PARENT
# *after* this subdir is processed, so it cannot be linked here — the parent does it.
endif()  # VIVID_BUILD_APP (test_operator_clone + test_shader_library need webgpu)

# P2.4: hot-reload infrastructure — classify_hot_reload + FileWatcher + HotReloader.
vivid_add_test(test_hot_reload)
target_sources(test_hot_reload PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/file_watcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/hot_reload.cpp)
target_link_libraries(test_hot_reload PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)

# ADR-0016 / S1: the shader-file format — header parser, host-param expansion, the generated
# prelude and the uniform layout. Pure (no GPU, no filesystem), so it runs in headless CI.
vivid_add_test(test_shader_meta)
target_sources(test_shader_meta PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/shader_meta.cpp)
target_link_libraries(test_shader_meta PRIVATE nlohmann_json::nlohmann_json)
