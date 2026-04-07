# Codebase Backlog

This is the compact operational backlog derived from the original audit snapshot in [docs/archive/audit/INDEX.md](/Users/jeff/Developer/vivid/docs/archive/audit/INDEX.md). Current evaluative context also lives in [docs/archive/audit/09-followup-remaining-work.md](/Users/jeff/Developer/vivid/docs/archive/audit/09-followup-remaining-work.md); the archived audit remains historical context, not the live source of truth by itself.

## Still Deferred

- **Test isolation and parallel-safety remain the clearest high-priority gap.** Many tests still rely on hardcoded `/tmp/` paths and bespoke temp-path patterns instead of shared test helpers.
- **Several oversized test files are still intact.** `tests/control/test_control_server.cpp`, `tests/graph/test_graph.cpp`, and `tests/ui/test_ui_screenshot_smoke.cpp` remain large multi-topic files that would benefit from splitting.
- **The remaining large runtime and UI files still deserve focused seam extraction.** Highest-value hotspots: `src/runtime/core/main.cpp`, `src/runtime/control/runtime_api.cpp`, `src/runtime/packages/package_manager.cpp`, `src/runtime/graph/graph_compiler.cpp`, `src/runtime/operators/operator_registry.cpp`, `src/ui/graph/node_graph_draw_inspector.cpp`, `src/ui/graph/node_graph_input_click.cpp`, and `src/ui/graph/node_graph_update.cpp`.
- **`cmake/tests.cmake` is still a large centralized build file.** The top-level CMake split landed, but test-build glue is still concentrated in `cmake/tests.cmake` and remains a worthwhile follow-on refactor target.
- **Two large operator implementation headers remain.** `operators/control/tracker/tracker_core.h` and `operators/control/drum_sequencer/drum_sequencer_core.h` are still large implementation headers and remain worthwhile extraction targets.
- **The runtime↔UI shared-data boundary still needs a proper home.** `src/runtime/control/graph_file_io.h` depends on `ui/graph/dialog_types.h`.
- **Low-priority encapsulation cleanup is still available.** `src/ui/graph/node_graph.h` and `src/runtime/graph/compiled_graph.h` still contain internal/public boundary issues.
- **Minor duplication and naming cleanup remain background work.** Good candidates for opportunistic cleanup, not front-of-queue work.

## Recommended Priority Order

1. Finish test isolation and parallel-safety
2. Continue decomposing the remaining runtime and UI hotspots
3. Move shared graph metadata types out of the UI layer
4. Split the oversized test files by topic
5. Break down `cmake/tests.cmake` and related centralized build glue further
6. Extract the remaining large operator implementation headers when those operators are next in flight
7. Handle low-priority encapsulation, duplication, and naming cleanups opportunistically
