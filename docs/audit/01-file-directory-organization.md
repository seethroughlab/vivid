# Phase 1: File & Directory Organization

**Date:** 2026-04-03
**Status:** Complete

## Summary Table

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| F-01 | Critical | Oversized File | `node_graph_draw.cpp` — 4,659 lines, 37+ methods | `src/ui/` |
| F-02 | Critical | Oversized File | `main.cpp` — 4,604 lines, monolithic entry point | `src/runtime/` |
| F-03 | High | Flat Directory | 105 files with no subdirectory structure | `src/runtime/` |
| F-04 | High | Flat Directory | 97 test files with no subdirectory structure | `tests/` |
| F-05 | High | Oversized File | `control_server.cpp` — 3,071 lines | `src/runtime/` |
| F-06 | High | Oversized File | `node_graph_input.cpp` — 2,414 lines | `src/ui/` |
| F-07 | High | Oversized File | `node_graph.cpp` — 2,300 lines | `src/ui/` |
| F-08 | Medium | Flat Directory | 38 files with no subdirectory structure | `src/ui/` |
| F-09 | Medium | Oversized File | `runtime_api.cpp` — 1,737 lines | `src/runtime/` |
| F-10 | Medium | Oversized File | `dialog_manager_draw.cpp` — 1,693 lines | `src/ui/` |
| F-11 | Medium | Oversized File | `package_manager.cpp` — 1,451 lines | `src/runtime/` |
| F-12 | Medium | Oversized File | `graph_compiler.cpp` — 1,390 lines | `src/runtime/` |
| F-13 | Medium | Oversized File | `dialog_manager_input.cpp` — 1,199 lines | `src/ui/` |
| F-14 | Medium | Oversized File | `operator_registry.cpp` — 1,189 lines | `src/runtime/` |
| F-15 | Medium | Oversized File | `graph.cpp` — 1,163 lines | `src/runtime/` |
| F-16 | Medium | Oversized File | `capture_coordinator.cpp` — 1,053 lines | `src/runtime/` |
| F-17 | Medium | Oversized File | `operator_creator.cpp` — 941 lines | `src/runtime/` |
| F-18 | Medium | Large Impl Header | `runtime_command_sink.h` — 932 lines of implementation | `src/runtime/` |
| F-19 | Medium | Oversized File | `renderer_2d.cpp` — 855 lines | `src/ui/` |
| F-20 | Medium | Oversized File | `audio_executor.cpp` — 852 lines | `src/runtime/` |
| F-21 | Medium | Large Impl Header | `tracker_core.h` — 1,146 lines of implementation | `operators/control/tracker/` |
| F-22 | Medium | Large Impl Header | `drum_sequencer_core.h` — 954 lines of implementation | `operators/control/drum_sequencer/` |
| F-23 | Info | Well-Organized | No issues found | `src/common/`, `src/export/`, `operators/`, etc. |

## Severity Definitions

- **Critical** — Files so large they actively impede development. Merge conflicts are frequent, navigation is painful, and the monolithic structure hides bugs. Address in the near term.
- **High** — Structural issues that slow onboarding and make the codebase harder to reason about. Address within the next few development cycles.
- **Medium** — Files that are large or misplaced but still individually manageable. Address opportunistically or when touching the area.
- **Low** — Minor organizational improvements. Nice-to-have.
- **Info** — Observations, positive findings, or context. No action required.

---

## Category A: Flat Directories

### F-03: `src/runtime/` — 105 files, completely flat [High]

**What:** All 105 source files (`.h`, `.cpp`, `.mm`) sit in a single directory with no subdirectories. Logical groupings are evident from naming conventions but not enforced by structure.

**Why it matters:** Finding related files requires memorizing naming prefixes. IDE file trees are unusable at this scale. New contributors cannot orient themselves. It also makes it harder to reason about dependency boundaries between subsystems.

**Recommendation:** Create subdirectories mirroring the logical groupings already visible in naming:

**`src/runtime/graph/`** — Graph compilation and execution
| File | Lines |
|------|-------|
| `graph.h` | 247 |
| `graph.cpp` | 1,163 |
| `graph_compiler.h` | 74 |
| `graph_compiler.cpp` | 1,390 |
| `compiled_graph.h` | 385 |
| `frame_executor.h` | 78 |
| `frame_executor.cpp` | 780 |
| `audio_executor.h` | 107 |
| `audio_executor.cpp` | 852 |
| `lane_state.h` | 104 |
| `lane_types.h` | 42 |
| `cadence_types.h` | 13 |
| `snapshot_types.h` | 86 |
| `subgraph_module.h` | 103 |
| `subgraph_module.cpp` | 567 |
| `port_type_registry.cpp` | 107 |

**`src/runtime/gpu/`** — GPU context and rendering
| File | Lines |
|------|-------|
| `gpu_context.h` | 69 |
| `gpu_context.cpp` | 353 |
| `fullscreen_blit.h` | 48 |
| `fullscreen_blit.cpp` | 451 |
| `gpu_frame_analysis.h` | 122 |
| `metal_interop.h` | 12 |
| `metal_interop.mm` | 70 |
| `syphon_output.h` | 32 |
| `syphon_output.mm` | 130 |
| `wgsl_header_parser.h` | 43 |
| `wgsl_header_parser.cpp` | 211 |
| `screenshot.cpp` | 2 |

**`src/runtime/audio/`** — Audio engine
| File | Lines |
|------|-------|
| `audio_engine.h` | 85 |
| `audio_engine.cpp` | 224 |
| `audio_frame_bridge.h` | 92 |
| `audio_frame_bridge.cpp` | 376 |
| `system_midi.h` | 62 |
| `system_midi.cpp` | 116 |

**`src/runtime/packages/`** — Package management
| File | Lines |
|------|-------|
| `package_manager.h` | 189 |
| `package_manager.cpp` | 1,451 |
| `package_compiler.h` | 69 |
| `package_compiler.cpp` | 482 |
| `package_catalog.h` | 79 |
| `package_catalog.cpp` | 373 |
| `package_scaffolder.h` | 32 |
| `package_scaffolder.cpp` | 195 |
| `package_test_runner.h` | 35 |
| `package_test_runner.cpp` | 307 |

**`src/runtime/operators/`** — Operator registry, loading, and creation
| File | Lines |
|------|-------|
| `operator_registry.h` | 203 |
| `operator_registry.cpp` | 1,189 |
| `operator_creator.h` | 35 |
| `operator_creator.cpp` | 941 |
| `operator_loader.h` | 93 |
| `operator_loader.cpp` | 542 |
| `operator_source_docs.h` | 41 |
| `operator_source_docs.cpp` | 606 |
| `operator_info_cache.h` | 113 |
| `operator_destination_policy.h` | 172 |
| `builtin_operators.h` | 7 |
| `builtin_operators.cpp` | 96 |

**`src/runtime/control/`** — Control server and runtime API
| File | Lines |
|------|-------|
| `control_server.h` | 80 |
| `control_server.cpp` | 3,071 |
| `control_server_checks.h` | 20 |
| `control_server_checks.cpp` | 701 |
| `runtime_api.h` | 211 |
| `runtime_api.cpp` | 1,737 |
| `runtime_command_sink.h` | 932 |

**`src/runtime/debug/`** — Capture, analysis, and testing
| File | Lines |
|------|-------|
| `capture_coordinator.h` | 155 |
| `capture_coordinator.cpp` | 1,053 |
| `output_analyzer.h` | 68 |
| `output_analyzer.cpp` | 354 |
| `output_window.h` | 35 |
| `output_window.cpp` | 227 |
| `ui_test_runner.h` | 103 |
| `ui_test_runner.cpp` | 376 |

**`src/runtime/platform/`** — Platform-specific code
| File | Lines |
|------|-------|
| `platform.h` | 25 |
| `platform.cpp` | 86 |
| `macos_menu.h` | 70 |
| `macos_menu.mm` | 528 |
| `macos_frame_timer.h` | 21 |
| `macos_frame_timer.cpp` | 70 |
| `sparkle_bridge.h` | 16 |
| `sparkle_bridge.mm` | 68 |
| `app_update_manager.h` | 70 |
| `app_update_manager.cpp` | 291 |
| `av_exporter.h` | 36 |
| `av_exporter.mm` | 342 |

**`src/runtime/core/`** — Core runtime, settings, utilities
| File | Lines |
|------|-------|
| `main.cpp` | 4,604 |
| `runtime_core.h` | 102 |
| `runtime_core.cpp` | 241 |
| `runtime_bootstrap.h` | 44 |
| `runtime_bootstrap.cpp` | 113 |
| `settings.h` | 47 |
| `settings.cpp` | 176 |
| `file_watcher.h` | 61 |
| `file_watcher.cpp` | 288 |
| `hot_reload.h` | 77 |
| `hot_reload.cpp` | 254 |
| `undo_manager.h` | 32 |
| `undo_manager.cpp` | 66 |
| `file_drop_registry.h` | 31 |
| `file_drop_registry.cpp` | 54 |
| `editor_detect.h` | 16 |
| `editor_detect.cpp` | 41 |
| `shared_handle_registry.h` | 105 |
| `build_console.h` | 213 |
| `crash_guard.h` | 70 |
| `tool_discovery.h` | 16 |
| `tool_discovery.cpp` | 71 |

**Effort:** Large — must update CMakeLists.txt source lists and all `#include` paths referencing these files.

---

### F-04: `tests/` — 97 test files, flat [High]

**What:** All 97 test `.cpp` files live at the root of `tests/` with no subdirectories. Subdirectories exist only for support files (`fixtures/`, `graphs/`, `operators/`, `stubs/`).

**Why it matters:** Difficult to find tests for a specific subsystem. Cannot easily run a subset of tests by directory. Test runner output is an undifferentiated wall.

**Recommendation:** Mirror the proposed `src/runtime/` subdirectory structure:

**`tests/graph/`**
- `test_graph.cpp` (1,826), `test_graph_compiler.cpp` (666), `test_graph_compiler_init.cpp` (449), `test_frame_executor_queries.cpp` (278), `test_graph_snapshot_contract.cpp` (66), `test_subgraph_module.cpp` (552), `test_port_type_registry.cpp` (61)

**`tests/audio/`**
- `test_audio_engine.cpp` (188), `test_audio_frame_bridge.cpp` (534), `test_audio_correctness.cpp` (422), `test_audio_control_timing.cpp` (265), `test_audio_dsp_api.cpp` (68), `test_audio_hot_reload.cpp` (109), `test_audio_robustness.cpp` (127), `test_audio_sequencer_graph.cpp` (188), `test_midi.cpp` (212), `test_midi_file_parser.cpp` (197), `test_midi_file_player.cpp` (162)

**`tests/gpu/`**
- `test_gpu_operators.cpp` (739), `test_gpu_correctness.cpp` (413)

**`tests/packages/`**
- `test_package_manager.cpp` (851), `test_package_compiler.cpp` (311), `test_package_catalog.cpp` (232), `test_package_scaffolder.cpp` (113), `test_package_test_runner.cpp` (318), `test_package_contract_ecosystem.cpp` (153), `test_package_scope_registry.cpp` (183), `test_package_scope_resolver.cpp` (179), `test_package_stress.cpp` (201), `test_package_update_logic.cpp` (126)

**`tests/operators/`** (tests about operators, not test operator implementations — rename existing `tests/operators/` to `tests/test_operators/`)
- `test_operator_creator.cpp` (802), `test_operator_loader.cpp` (791), `test_operator_sweep.cpp` (812), `test_operator_source_docs.cpp` (212), `test_operator_destination_policy.cpp` (141), `test_operator_info_cache.cpp` (74), `test_builtin_operators.cpp` (81), `test_modulation_ops.cpp` (329), `test_synth_transform_ops.cpp` (364), `test_spatial_ops.cpp` (671), `test_child_op.cpp` (227), `test_arpeggiator_patterns.cpp` (46)

**`tests/control/`**
- `test_control_server.cpp` (2,531), `test_runtime_api.cpp` (903), `test_runtime_core.cpp` (339), `test_runtime_bootstrap_packages.cpp` (187)

**`tests/lanes/`**
- `test_lane_state.cpp` (205), `test_lane_propagation.cpp` (261), `test_lane_equivalence.cpp` (306), `test_compute_lane_equivalence.cpp` (548), `test_lane_broadcast.cpp` (149), `test_lane_capacity.cpp` (156), `test_lane_compaction.cpp` (196), `test_lane_reshape.cpp` (327), `test_lane_breadth.cpp` (209), `test_lane_bridge_snapshot.cpp` (209), `test_lane_metadata.cpp` (107), `test_frame_lane_lifting.cpp` (444), `test_fixed_cadence_assignment.cpp` (194), `test_latency_validation.cpp` (308), `test_scalar_hold_bridge.cpp` (116), `test_scalar_port.cpp` (126), `test_string_ports.cpp` (98), `test_perception_introspection.cpp` (206)

**`tests/ui/`**
- `test_ui_screenshot_smoke.cpp` (1,467), `test_ui_editor_interactions.cpp` (372), `test_ui_overlay_interactions.cpp` (351), `test_ui_widget_interactions.cpp` (450), `test_ui_arch_guard.cpp` (52), `test_text_edit.cpp` (396), `test_theme_loader.cpp` (439), `test_inspector_layout.cpp` (226), `test_overlay_layouts.cpp` (53), `test_i18n.cpp` (78)

**`tests/core/`**
- `test_settings.cpp` (224), `test_file_watcher.cpp` (185), `test_hot_reload.cpp` (146), `test_hot_reload_stress.cpp` (146), `test_hot_reloader_queue.cpp` (145), `test_undo_manager.cpp` (103), `test_undo_mutation_types.cpp` (380), `test_file_drop_registry.cpp` (64), `test_editor_detect.cpp` (54), `test_tool_discovery.cpp` (111), `test_build_console.cpp` (50), `test_app_update_manager.cpp` (96), `test_state_machine.cpp` (279), `test_path_animate.cpp` (238)

**`tests/common/`**
- `test_string_util.cpp` (65), `test_path_util.cpp` (110), `test_topo_sort.cpp` (149), `test_json_migration.cpp` (292)

**`tests/integration/`**
- `test_demo_graphs.cpp` (532), `test_mixed_runtime_stability.cpp` (202), `test_runtime_stress.cpp` (173), `test_team_workflow_regression.cpp` (293), `test_capture_coordinator.cpp` (144), `test_output_analyzer.cpp` (246), `test_wgsl_header.cpp` (299), `test_wgsl_preprocessor.cpp` (73)

**`tests/media/`**
- `test_media_headless.cpp` (390), `test_movie_av_sync.cpp` (510), `test_movie_decode_route.cpp` (78), `test_movie_decode_upload.cpp` (27), `test_movie_load_async.cpp` (57), `test_movie_load_generation.cpp` (31), `test_hap_codec.cpp` (45), `test_export_pipeline.cpp` (168)

**Effort:** Large — must update CMakeLists.txt test registrations.

---

### F-08: `src/ui/` — 38 files, flat [Medium]

**What:** All 38 UI source files in a single directory. Thematic groups are clear from naming.

**Why it matters:** Less severe than runtime (38 vs 105 files) but the node_graph group alone is 7+ files totaling ~10,700 lines.

**Recommendation:**

**`src/ui/graph/`** — Node graph UI
| File | Lines |
|------|-------|
| `node_graph.h` | 779 |
| `node_graph.cpp` | 2,300 |
| `node_graph_draw.cpp` | 4,659 |
| `node_graph_input.cpp` | 2,414 |
| `node_graph_util.h` | 282 |
| `node_graph_constants.h` | 391 |
| `graph_snapshot.h` | 294 |

**`src/ui/dialogs/`** — Dialog system
| File | Lines |
|------|-------|
| `dialog_manager.h` | 320 |
| `dialog_manager.cpp` | 468 |
| `dialog_manager_draw.cpp` | 1,693 |
| `dialog_manager_input.cpp` | 1,199 |
| `dialog_types.h` | 75 |
| `file_dialog.h` | 22 |
| `file_dialog.mm` | 88 |

**`src/ui/inspector/`** — Inspector panel
| File | Lines |
|------|-------|
| `inspector_controller.h` | 188 |
| `inspector_controller.cpp` | 12 |
| `inspector_layout.h` | 235 |

**`src/ui/rendering/`** — 2D rendering, overlays, thumbnails
| File | Lines |
|------|-------|
| `renderer_2d.h` | 122 |
| `renderer_2d.cpp` | 855 |
| `overlay_layouts.h` | 50 |
| `overlay_layouts.cpp` | 125 |
| `thumbnail_renderer.h` | 69 |
| `thumbnail_renderer.cpp` | 322 |
| `thumbnail_cache.h` | 62 |
| `thumbnail_cache.cpp` | 84 |

**`src/ui/style/`** — Theming, styling, i18n
| File | Lines |
|------|-------|
| `theme_loader.h` | 39 |
| `theme_loader.cpp` | 656 |
| `ui_style.h` | 55 |
| `ui_style.cpp` | 120 |
| `i18n.h` | 36 |
| `i18n.cpp` | 71 |

**`src/ui/` (root)** — Widgets and top-level
| File | Lines |
|------|-------|
| `text_edit.h` | 165 |
| `active_text_field.h` | 47 |
| `build_console_panel.h` | 79 |
| `build_console_panel.cpp` | 307 |
| `ui_command_sink.h` | 114 |

**Effort:** Medium — fewer files and fewer cross-references than runtime.

---

## Category B: Oversized Files

### F-01: `src/ui/node_graph_draw.cpp` — 4,659 lines [Critical]

**What:** The single largest file in the codebase. Contains 37+ `NodeGraphUI::draw_*` methods covering: graph/node/wire rendering, inspector panel (header, knobs, xy-pads, color swatches, scrollbar, MIDI map banner, state presets, outputs, resolution), custom inspector callbacks, patch panel, preview wire, box select, chooser, grid, sticky notes, overlays, performance bars/sparklines, and tooltips.

**Why it matters:** Any visual change risks merge conflicts. Compile time for this translation unit is disproportionate. Methods are so numerous that finding the right one requires searching rather than scanning.

**Recommendation:** Split by visual subsystem. All files implement methods on the same `NodeGraphUI` class, so the header doesn't change — only method bodies move.

| Proposed File | Methods | Est. Lines |
|---------------|---------|------------|
| `node_graph_draw.cpp` | `draw`, `draw_graph` (orchestrators) | ~350 |
| `node_graph_draw_inspector.cpp` | `draw_inspector`, `draw_inspector_header`, `draw_inspector_knob`, `draw_inspector_xy_pad`, `draw_inspector_color_swatch`, `draw_inspector_scrollbar`, `draw_inspector_params`, `draw_inspector_group_header`, `draw_one_inspector_param`, `draw_one_inspector_param_simple`, `draw_custom_inspector`, `draw_section_separator`, `draw_inspector_resolution`, `draw_inspector_state_presets`, `draw_inspector_outputs`, `draw_midi_map_banner`, `draw_color_popup` | ~1,800 |
| `node_graph_draw_connections.cpp` | `draw_connections`, `draw_preview_wire`, `draw_wire_tooltip`, `draw_patch_panel`, dashed-wire helpers | ~800 |
| `node_graph_draw_overlays.cpp` | `draw_overlays`, `draw_perf_bar`, `draw_perf_sparkline`, `draw_perf_expanded` | ~700 |
| `node_graph_draw_elements.cpp` | `draw_grid`, `draw_sticky_notes`, `draw_box_select`, `draw_chooser`, `draw_node_error_tooltip`, `draw_param_tooltip` | ~1,000 |

**Effort:** Medium — method bodies move between `.cpp` files; no interface changes. Static helpers at the top of the file need to move to whichever file uses them (or to a shared internal header if used by multiple).

---

### F-02: `src/runtime/main.cpp` — 4,604 lines [Critical]

**What:** The application entry point contains: string utility functions (URL encoding, CSV parsing, trimming), workspace seeding/migration logic, monitor/window management, GLFW callbacks, graph snapshot building (~370 lines), the entire main loop with phase timing, hot-reload polling, custom thumbnail drawing, screenshot capture, and UI test script execution.

**Why it matters:** The entry point should be a thin orchestrator. This file touches nearly every subsystem, making it a merge-conflict magnet and impossible to test in isolation. The 40+ static helper functions should live in focused modules.

**Recommendation:**

| Proposed File | Content | Est. Lines |
|---------------|---------|------------|
| `main.cpp` | CLI arg parsing, object creation, enter main loop | ~200 |
| `workspace_manager.h/cpp` | `ensure_workspace_seeded`, `copy_tree_missing`, `copy_tree_overwrite_newer`, scaffold resolution | ~350 |
| `window_manager.h/cpp` | Monitor detection, `monitor_for_window`, `monitor_for_target`, `clamp_window_rect_to_monitor`, GLFW callbacks (`key_callback`, `char_callback`, `cursor_pos_callback`, `mouse_button_callback`, `scroll_callback`, `drop_callback`), fullscreen toggle | ~500 |
| `graph_snapshot_builder.h/cpp` | `build_graph_snapshot` and related helpers | ~400 |
| `main_loop.h/cpp` | Frame loop body, `PhaseTimer`, `emit_clear_pass`, `draw_custom_thumbnails`, `run_ui_test_script_frame` | ~600 |
| `hot_reload_poll.h/cpp` | `poll_hot_reload`, `add_watch_for_resolved_package` | ~200 |
| `capture_helpers.h/cpp` | `capture_surface_png`, `try_capture_screenshot`, `SurfaceCaptureResult` | ~200 |
| Move to `src/common/string_util.h` | `url_encode`, `trim_copy`, `split_csv`, `join_csv` | ~80 |
| Move to `src/common/` or `core/` | `load_example_entry_from_graph`, `load_graph_meta_edit_data`, `save_graph_meta_edit_data`, graph listing helpers | ~300 |

**Effort:** Large — must carefully extract state, manage initialization order, and ensure GLFW user-data pointers still work.

---

### F-05: `src/runtime/control_server.cpp` — 3,071 lines [High]

**What:** Single class using pimpl pattern. The `Impl` struct contains all WebSocket/HTTP/OSC handling logic.

**Why it matters:** All control protocol handling in one file makes it difficult to work on one protocol without navigating past others.

**Recommendation:** Split by protocol or concern area — e.g., separate WebSocket message dispatch, HTTP endpoint handling, and OSC handling into their own files, all implementing methods on the same `Impl` struct.

**Effort:** Medium

---

### F-06: `src/ui/node_graph_input.cpp` — 2,414 lines [High]

**What:** All input handling for the node graph UI: mouse events, keyboard shortcuts, drag-and-drop, MIDI mapping interactions.

**Recommendation:** Split by input modality: mouse handling, keyboard handling, drag-and-drop, MIDI mapping. Same pattern as the draw split — methods move between `.cpp` files.

**Effort:** Medium

---

### F-07: `src/ui/node_graph.cpp` — 2,300 lines [High]

**What:** Core node graph state management, initialization, and snapshot handling.

**Recommendation:** Separate state mutation methods from initialization from query/serialization helpers.

**Effort:** Medium

---

### F-09: `src/runtime/runtime_api.cpp` — 1,737 lines [Medium]

**What:** Runtime API implementation — the command interface exposed to the control server.

**Recommendation:** Group API endpoints by domain (graph commands, package commands, system/query commands) into separate files.

**Effort:** Medium

---

### F-10: `src/ui/dialog_manager_draw.cpp` — 1,693 lines [Medium]

**What:** Drawing logic for all dialog types in one file.

**Recommendation:** Split by dialog type — each major dialog (settings, package browser, graph meta editor, etc.) gets its own draw file.

**Effort:** Medium

---

### F-11: `src/runtime/package_manager.cpp` — 1,451 lines [Medium]

**What:** Package management: install, uninstall, catalog sync, dependency resolution, linking.

**Recommendation:** Separate install/uninstall operations from catalog sync from dependency resolution.

**Effort:** Medium

---

### F-12: `src/runtime/graph_compiler.cpp` — 1,390 lines [Medium]

**What:** Graph compilation pipeline — topology sort, lane assignment, validation, operator instantiation.

**Recommendation:** Separate compilation passes into focused files if the internal structure supports it.

**Effort:** Medium

---

### F-13: `src/ui/dialog_manager_input.cpp` — 1,199 lines [Medium]

**What:** Input handling for all dialog types.

**Recommendation:** Split parallel to the draw split — same dialog types, same file boundaries.

**Effort:** Medium

---

### F-14: `src/runtime/operator_registry.cpp` — 1,189 lines [Medium]

**What:** Operator registration, lookup, and type metadata management.

**Recommendation:** Separate registration logic from lookup/query from serialization.

**Effort:** Small

---

### F-15: `src/runtime/graph.cpp` — 1,163 lines [Medium]

**What:** Graph data structure — node/connection CRUD, serialization, validation.

**Recommendation:** Separate mutation operations from serialization/deserialization.

**Effort:** Small

---

### F-16: `src/runtime/capture_coordinator.cpp` — 1,053 lines [Medium]

**What:** Capture orchestration — coordinating GPU readback, encoding, and output.

**Recommendation:** Separate capture orchestration from encoding/format handling.

**Effort:** Small

---

### F-17: `src/runtime/operator_creator.cpp` — 941 lines [Medium]

**What:** Operator instantiation — creating operator instances from registry metadata.

**Recommendation:** Lower priority. Consider splitting by domain (audio/control/GPU creation paths) if they diverge significantly.

**Effort:** Small

---

### F-19: `src/ui/renderer_2d.cpp` — 855 lines [Medium]

**What:** 2D rendering primitives.

**Recommendation:** Near the threshold. Flag for review if it continues to grow, but no immediate split needed.

**Effort:** N/A (monitor)

---

### F-20: `src/runtime/audio_executor.cpp` — 852 lines [Medium]

**What:** Audio graph execution.

**Recommendation:** Same as F-19 — near threshold, monitor for growth.

**Effort:** N/A (monitor)

---

## Category C: Large Implementation Headers

### F-18: `src/runtime/runtime_command_sink.h` — 932 lines [Medium]

**What:** A header file containing 932 lines, most of which is method implementations rather than declarations. Every translation unit that includes this header recompiles all 932 lines on any change.

**Recommendation:** Extract method implementations into `runtime_command_sink.cpp`. Keep only class declarations, inline one-liners, and template definitions in the header.

**Effort:** Small

---

### F-21: `operators/control/tracker/tracker_core.h` — 1,146 lines [Medium]

**What:** Full implementation of tracker core logic in a header file.

**Recommendation:** Split into declaration header + `.cpp` implementation file within the same operator directory. Blast radius is small (only the tracker operator includes it).

**Effort:** Small

---

### F-22: `operators/control/drum_sequencer/drum_sequencer_core.h` — 954 lines [Medium]

**What:** Full implementation of drum sequencer core logic in a header file.

**Recommendation:** Same as F-21 — split into header + `.cpp`.

**Effort:** Small

---

## Category D: Well-Organized Areas [Info]

The following areas passed review with no issues:

- **`src/common/`** — 7 files, 532 lines. Genuinely shared utilities (JSON, string, path, MIDI, topo sort). Appropriate granularity.
- **`src/export/`** — 3 files, 1,096 lines. Well-scoped export pipeline.
- **`src/operator_api/`** — 25 files, 4,932 lines. Clean operator-facing API. Draw helpers (`draw_plot_helpers.h`, `draw_ui_helpers.h`, `adsr_inspector.h`) are correctly placed — they're used by operators for custom thumbnails/inspectors via `VividDrawAPI`, not by the UI layer.
- **`operators/`** — 113 operators consistently structured by domain (`audio/`, `control/`, `gpu/`, `shared/`). No cross-domain misplacements. Large operator files (tracker, drum_sequencer, arpeggiator) are justified by algorithmic complexity.
- **`graphs/`** — Well-organized by theme (audio, gpu, filters, intro, io).
- **`assets/`**, **`filters/`**, **`deps/`**, **`platform/`**, **`locales/`** — All appropriately scoped.
- **No orphaned files** — all source files are referenced in CMakeLists.txt or included by other files.
- **No stale/dead files** — no `_old`, `_backup`, `_temp`, or deprecated naming patterns.
- **Consistent naming** — snake_case throughout, both directories and files.

---

## Prioritized Action Plan

### Wave 1: Split Critical Files + Quick Wins
These changes are self-contained and don't affect directory structure.

1. **F-01** — Split `node_graph_draw.cpp` (4,659 lines) into 5 focused files. Medium effort, high impact.
2. **F-02** — Decompose `main.cpp` (4,604 lines) into 7+ focused modules. Large effort, high impact.
3. **F-18** — Extract `runtime_command_sink.h` implementation to `.cpp`. Small effort, improves build times.
4. **F-21, F-22** — Split `tracker_core.h` and `drum_sequencer_core.h` into header + `.cpp`. Small effort each.

### Wave 2: Directory Restructuring
These changes affect include paths and CMakeLists.txt. Do them as focused, coordinated efforts.

5. **F-03** — Introduce `src/runtime/` subdirectories (9 subdirs, 105 files). Large effort.
6. **F-08** — Introduce `src/ui/` subdirectories (5 subdirs, 38 files). Medium effort.
7. **F-04** — Introduce `tests/` subdirectories (11 subdirs, 97 files). Large effort. Do after src/ so test paths can mirror src/ structure.

### Wave 3: Remaining Oversized Files
Address these opportunistically or when touching the relevant code.

8. **F-05** — Split `control_server.cpp` (3,071 lines)
9. **F-06** — Split `node_graph_input.cpp` (2,414 lines)
10. **F-07** — Split `node_graph.cpp` (2,300 lines)
11. **F-09 through F-17** — Split remaining 1,000-1,700 line files
12. **F-19, F-20** — Monitor `renderer_2d.cpp` and `audio_executor.cpp` for growth

---

## Appendix: All Files Over 800 Lines

| # | File | Lines |
|---|------|-------|
| 1 | `src/ui/node_graph_draw.cpp` | 4,659 |
| 2 | `src/runtime/main.cpp` | 4,604 |
| 3 | `src/runtime/control_server.cpp` | 3,071 |
| 4 | `tests/test_control_server.cpp` | 2,531 |
| 5 | `src/ui/node_graph_input.cpp` | 2,414 |
| 6 | `src/ui/node_graph.cpp` | 2,300 |
| 7 | `tests/test_graph.cpp` | 1,826 |
| 8 | `src/runtime/runtime_api.cpp` | 1,737 |
| 9 | `src/ui/dialog_manager_draw.cpp` | 1,693 |
| 10 | `src/runtime/package_manager.cpp` | 1,451 |
| 11 | `src/runtime/graph_compiler.cpp` | 1,390 |
| 12 | `operators/control/tracker/tracker.cpp` | 1,279 |
| 13 | `src/ui/dialog_manager_input.cpp` | 1,199 |
| 14 | `src/runtime/operator_registry.cpp` | 1,189 |
| 15 | `src/runtime/graph.cpp` | 1,163 |
| 16 | `operators/control/tracker/tracker_core.h` | 1,146 |
| 17 | `operators/control/drum_sequencer/drum_sequencer.cpp` | 1,093 |
| 18 | `src/runtime/capture_coordinator.cpp` | 1,053 |
| 19 | `operators/control/drum_sequencer/drum_sequencer_core.h` | 954 |
| 20 | `src/runtime/operator_creator.cpp` | 941 |
| 21 | `src/runtime/runtime_command_sink.h` | 932 |
| 22 | `tests/test_runtime_api.cpp` | 903 |
| 23 | `src/ui/renderer_2d.cpp` | 855 |
| 24 | `src/runtime/audio_executor.cpp` | 852 |
| 25 | `tests/test_package_manager.cpp` | 851 |
| 26 | `tests/test_operator_sweep.cpp` | 812 |
| 27 | `tests/test_operator_creator.cpp` | 802 |
