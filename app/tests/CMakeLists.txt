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
vivid_add_test(test_mapping_request)    # MCP helper contract: audio characteristic -> visual mapping request
vivid_add_test(test_visual_control)     # ADR-0053 Phase B: control-edge resolver == registry formula (parity + envelope)
vivid_add_test(test_reactive_bus)       # ADR-0053 Phase B: reactive-signal bus round-trip (master + stable-id tracks)
target_sources(test_reactive_bus PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/reactive_bus.cpp)
vivid_add_test(test_reactive_signals)   # ADR-0053 Phase B5: reactive signal grammar == bridge suffix grammar (migration lane contract)
vivid_add_test(test_undo_manager)   # ADR-0017/G1 undo history cursor (pure)
target_sources(test_undo_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/undo_manager.cpp)
vivid_add_test(test_undo_projection)  # ADR-0017/G1 canonical projection + audio tiering (pure)
target_sources(test_undo_projection PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/persist_undo.cpp)
target_link_libraries(test_undo_projection PRIVATE nlohmann_json::nlohmann_json)
vivid_add_test(test_param_widget)   # inspector widget classifier (type/hint/enum -> widget)
vivid_add_test(test_graph_camera)   # ADR-0023 #1/#3d shared graph camera (transform + pan/zoom/reset math)
vivid_add_test(test_graph_selection)  # ADR-0033 P1 multi-select set + marquee-resolve (pure, header-only)
vivid_add_test(test_chooser_rank)   # ADR-0046 add-node chooser demotes recipe ops below primitives
vivid_add_test(test_port_compat)    # ADR-0047 typed-port compatibility rule (visual connection validation)
vivid_add_test(test_editor_controls)  # ADR-0048 clip/sample editor control substrate hit geometry
vivid_add_test(test_output_preview) # ADR-0025 OutputPreview::clamp geometry (float-preview stays in-column)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/movie_audio_bus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/glitch/glitch_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
vivid_add_test(test_audio_graph)    # AG-0 per-track audio DAG: topo/sum/cycle + zero-alloc
target_sources(test_audio_graph PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/audio_graph.cpp)
vivid_add_test(test_bridge_override)  # ADR-0030 P2: native frame-override channel (non-destructive bridge)
target_sources(test_bridge_override PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/audio_op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/builtin_audio_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/movie_audio_bus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/glitch/glitch_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
vivid_add_test(test_sampler_op)     # AO-4 native Sampler: slice-per-note + zero-alloc
target_sources(test_sampler_op PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/audio_op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/builtin_audio_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/movie_audio_bus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/glitch/glitch_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
# ADR-0015 / M4: the Arp note effect — notes in, notes out. The proof that notes are a real signal
# in the graph (M1's note edges are inaudible on their own), plus its zero-alloc RT contract.
vivid_add_test(test_arp_op)
target_sources(test_arp_op PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/audio_op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/builtin_audio_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/movie_audio_bus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/glitch/glitch_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)

vivid_add_test(test_generator_ops)   # ADR-0022 P3.3: the note generators (Euclid/Chord/RandMelody)
target_sources(test_generator_ops PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/audio_op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/builtin_audio_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/movie_audio_bus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/glitch/glitch_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)

vivid_add_test(test_lfo_op)         # ADR-0022 P0.5: the LFO, the first modulator (control_out, ABI v13)
target_sources(test_lfo_op PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/audio_op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/builtin_audio_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/movie_audio_bus.cpp
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
vivid_add_test(test_note_bus)       # ADR-0029: active-notes bus correctness + concurrent TSan target
target_sources(test_note_bus PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/note_bus.cpp)
target_link_libraries(test_note_bus PRIVATE Threads::Threads)
# The THREAD label is what the ThreadSanitizer CI leg runs (`ctest -L THREAD`): TSan's value is racing
# tests, and the package/dlopen tests SEGV under it (loading a non-instrumented dylib), so the leg opts
# IN the concurrency tests rather than running the whole suite. A new threaded test adds this label.
set_tests_properties(test_note_bus PROPERTIES LABELS "HEADLESS_SMOKE;THREAD")
vivid_add_test(test_analysis_ring)  # ADR-0029 phase 2: atomic-slot spectrum ring, concurrent TSan target
target_link_libraries(test_analysis_ring PRIVATE Threads::Threads)
set_tests_properties(test_analysis_ring PROPERTIES LABELS "HEADLESS_SMOKE;THREAD")
vivid_add_test(test_node_ring_bank)  # ADR-0029 phase 2: atomic-slot per-node ring bank, concurrent TSan target
target_link_libraries(test_node_ring_bank PRIVATE Threads::Threads)
set_tests_properties(test_node_ring_bank PROPERTIES LABELS "HEADLESS_SMOKE;THREAD")
vivid_add_test(test_held_note_set)   # ADR-0029 phase 2: atomic-slot held-note set, concurrent TSan target
target_link_libraries(test_held_note_set PRIVATE Threads::Threads)
set_tests_properties(test_held_note_set PROPERTIES LABELS "HEADLESS_SMOKE;THREAD")
vivid_add_test(test_clap_param_queue)  # Ph2 P2-01: CLAP UI->audio param queue drop-on-full, concurrent TSan target
target_link_libraries(test_clap_param_queue PRIVATE Threads::Threads)
set_tests_properties(test_clap_param_queue PROPERTIES LABELS "HEADLESS_SMOKE;THREAD")
vivid_add_test(test_popup_menu)     # ADR-0027: PopupMenu builders + draw/hit-test geometry invariant
vivid_add_test(test_control_parse)

# Realtime AV video export coordinator (app/video_recorder) with a mock exporter — even-dim locking,
# frame/audio forwarding, blank-skip, crop, deadline auto-stop, RecordingTap ring. No GPU/AVFoundation.
vivid_add_test(test_video_recorder)
target_sources(test_video_recorder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/video_recorder.cpp)

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

# Ph5 P2-05: a plugin's authored param base follows its stable id across a re-cache (pure helper).
vivid_add_test(test_authored_base)

# Pre-P1 session migration (legacy VOp-int -> op-name). persist.h pulls nlohmann.
vivid_add_test(test_persist_chain_migration)
target_link_libraries(test_persist_chain_migration PRIVATE nlohmann_json::nlohmann_json)

# Ph4 P1-02: missing-operator params survive a save/load round-trip (persist_orphan.h, pure JSON).
vivid_add_test(test_persist_orphan)
target_link_libraries(test_persist_orphan PRIVATE nlohmann_json::nlohmann_json)

# Visuals executor topological order + cycle handling (gpu/graph_topo.h; header-only, wgpu-free).
vivid_add_test(test_graph_topo)
vivid_add_test(test_output_feed)   # P2-03: structural "Output has a feed" predicate (gpu/output_feed.h)

# Launch window sizing policy: compute_launch_rect (pure) + a prefs save/load round-trip.
vivid_add_test(test_window_prefs)
target_sources(test_window_prefs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/window_prefs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp)
target_link_libraries(test_window_prefs PRIVATE nlohmann_json::nlohmann_json)

# UX Ph4 F1: app-level settings store (reduce-motion) — defaults + save/load round-trip + bad-JSON guard.
vivid_add_test(test_app_settings)
target_sources(test_app_settings PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/app_settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp)
target_link_libraries(test_app_settings PRIVATE nlohmann_json::nlohmann_json)

# UX Ph4 F3: pure keyboard-editing decision logic (audio wire-kind inference + spatial nearest).
# Header-only (app/kbd_edit_logic.h), so no app/GLFW link.
vivid_add_test(test_kbd_edit_logic)

# P4.1: session-version load guard (classify_session_version is a pure header fn).
vivid_add_test(test_version_guard)
target_link_libraries(test_version_guard PRIVATE nlohmann_json::nlohmann_json)

# Ph6 P1-02: the release signer fails loud on missing notarization creds (portable shell test —
# the guard runs before any macOS-only codesign/notarytool step, so it runs on Linux CI too).
add_test(NAME test_notarize_guard
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/release/test_notarize_guard.sh)
set_tests_properties(test_notarize_guard PROPERTIES LABELS "HEADLESS_SMOKE")

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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_scan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/loaded_operator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp
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

vivid_add_test(test_project_recovery)   # ADR-0040 P3: beginner-recovery diagnostics (plugin/package)
target_sources(test_project_recovery PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/cli/project_recovery.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_manifest.cpp)
target_link_libraries(test_project_recovery PRIVATE nlohmann_json::nlohmann_json)

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

# ADR-0018 R1/R2: crash attribution + recovery. The record model + marker→record + history are
# App-free (crash_recovery.cpp; the App-linked warm snapshot lives in crash_recovery_snapshot.cpp).
# The R1 half forks a child that crashes inside a CrashGuard and asserts the handler's marker names
# the operator (skipped under sanitizers, which own the SEGV handler).
vivid_add_test(test_crash_recovery)
target_sources(test_crash_recovery PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/crash_recovery.cpp)
target_link_libraries(test_crash_recovery PRIVATE nlohmann_json::nlohmann_json)

# ADR-0045 P0-01: a hosted plugin crash on the RT thread is attributed (plugin_crash_name + CrashGuard).
vivid_add_test(test_plugin_crash_attrib)
# Ph4 P1-01: close-all-and-null the plugin-editor pools before a Full-tier undo frees the instances.
vivid_add_test(test_plugin_editor_pool)

# ADR-0018 R3: stateless 3-in-24h quarantine + un-quarantine, from the crash history (App-free).
vivid_add_test(test_quarantine)
target_sources(test_quarantine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/quarantine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/crash_recovery.cpp)
target_link_libraries(test_quarantine PRIVATE nlohmann_json::nlohmann_json)
