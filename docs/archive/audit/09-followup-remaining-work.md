# Audit Follow-Up: Remaining Work

**Date:** 2026-04-03
**Status:** Current-state follow-up

The original audit was useful, but parts of it are now stale because substantial cleanup work has already landed. This follow-up is a current-state evaluation of what still remains, with explicit separation between work that is done, work that moved materially in the right direction, and work that is still genuinely deferred.

## What Landed Since The Audit

Several of the audit's most important findings should now be considered resolved.

- **The flat-directory findings are no longer current.** `src/runtime/`, `src/ui/`, and `tests/` now have meaningful subsystem structure instead of the fully flat layout described in Phase 1.
- **The worst build duplication issue was fixed.** The build now uses `vivid_runtime_testlib`, so the old "compile the same runtime sources into every test target" problem is no longer an accurate description of the current build.
- **Build configuration is now modularized.** The root [CMakeLists.txt](/Users/jeff/Developer/vivid/CMakeLists.txt) is small and delegates to `cmake/dependencies.cmake`, `cmake/operators.cmake`, `cmake/app.cmake`, and `cmake/tests.cmake`. The original "single giant top-level CMakeLists" finding is stale.
- **The WebGPU dependency is pinned and warning flags are configured.** Those Phase 6 findings are now resolved in [CMakeLists.txt](/Users/jeff/Developer/vivid/CMakeLists.txt).
- **Some of the largest monoliths were split successfully.** The old `control_server.cpp` monolith is now decomposed into focused files under `src/runtime/control/`, and the node-graph work is spread across many `src/ui/graph/` files rather than the original single-file layout.
- **The obvious frame-executor null-instance bug is fixed.** [src/runtime/graph/frame_executor.cpp](/Users/jeff/Developer/vivid/src/runtime/graph/frame_executor.cpp) now guards null instances.
- **Two older duplication findings are effectively resolved.** `pattern_seq` now uses `pattern_seq_core.h`, and `tests/test_helpers.h` plus `ScopedTempDir` are used widely enough that the original "most tests redefine their own helpers" finding is no longer current.
- **The control-server port-collision finding was fixed in the named test.** [tests/control/test_control_server.cpp](/Users/jeff/Developer/vivid/tests/control/test_control_server.cpp) now uses port `0` and reads back the assigned port instead of hardcoding `19876`.

## Partially Resolved

These areas improved meaningfully, but there is still enough debt left that they remain active follow-up candidates.

- **Large-file reduction is real, but not complete.** The worst files from the original audit are no longer the same files, which is good progress. But several runtime and UI files are still large enough to slow navigation and ownership, including [src/runtime/core/main.cpp](/Users/jeff/Developer/vivid/src/runtime/core/main.cpp), [src/runtime/control/runtime_api.cpp](/Users/jeff/Developer/vivid/src/runtime/control/runtime_api.cpp), [src/runtime/packages/package_manager.cpp](/Users/jeff/Developer/vivid/src/runtime/packages/package_manager.cpp), [src/runtime/graph/graph_compiler.cpp](/Users/jeff/Developer/vivid/src/runtime/graph/graph_compiler.cpp), [src/runtime/operators/operator_registry.cpp](/Users/jeff/Developer/vivid/src/runtime/operators/operator_registry.cpp), and multiple `src/ui/graph/` files.
- **Header hygiene improved, but the remaining boundary issue is still real.** [src/runtime/control/runtime_command_sink.h](/Users/jeff/Developer/vivid/src/runtime/control/runtime_command_sink.h) is much smaller than the audit-era version, which is a clear win. But [src/runtime/control/graph_file_io.h](/Users/jeff/Developer/vivid/src/runtime/control/graph_file_io.h) still depends on `ui/graph/dialog_types.h`, so the runtime↔UI shared-data boundary cleanup is not finished.
- **The test framework is healthier, but not fully unified.** `ScopedTempDir` exists in [tests/test_helpers.h](/Users/jeff/Developer/vivid/tests/test_helpers.h), and helper adoption is broad. But a small number of tests still define their own `failures`/`check()` helpers or use bespoke setup patterns, so the original duplication/framework concerns are reduced rather than fully eliminated.
- **The build is modularized, but `cmake/tests.cmake` is now the new concentration point.** The top-level CMake problem is gone, but [cmake/tests.cmake](/Users/jeff/Developer/vivid/cmake/tests.cmake) is still very large and remains an obvious follow-on refactor target.

## Still Deferred

These issues are still plainly true in the current tree and remain good candidates for additional cleanup.

- **Test isolation and parallel-safety remain the clearest high-priority gap.** Many tests still rely on hardcoded `/tmp/` paths. The old fixed-port problem in `test_control_server` is fixed, but a fixed-port pattern still exists in [tests/ops/test_perception_introspection.cpp](/Users/jeff/Developer/vivid/tests/ops/test_perception_introspection.cpp).
- **Several oversized test files are still intact.** [tests/control/test_control_server.cpp](/Users/jeff/Developer/vivid/tests/control/test_control_server.cpp), [tests/graph/test_graph.cpp](/Users/jeff/Developer/vivid/tests/graph/test_graph.cpp), and [tests/ui/test_ui_screenshot_smoke.cpp](/Users/jeff/Developer/vivid/tests/ui/test_ui_screenshot_smoke.cpp) remain large multi-topic files that would benefit from splitting.
- **The remaining large runtime and UI files still deserve focused seam extraction.** The highest-value current hotspots are [src/runtime/core/main.cpp](/Users/jeff/Developer/vivid/src/runtime/core/main.cpp), [src/runtime/control/runtime_api.cpp](/Users/jeff/Developer/vivid/src/runtime/control/runtime_api.cpp), [src/runtime/packages/package_manager.cpp](/Users/jeff/Developer/vivid/src/runtime/packages/package_manager.cpp), [src/runtime/graph/graph_compiler.cpp](/Users/jeff/Developer/vivid/src/runtime/graph/graph_compiler.cpp), [src/runtime/operators/operator_registry.cpp](/Users/jeff/Developer/vivid/src/runtime/operators/operator_registry.cpp), [src/ui/graph/node_graph_draw_inspector.cpp](/Users/jeff/Developer/vivid/src/ui/graph/node_graph_draw_inspector.cpp), [src/ui/graph/node_graph_input_click.cpp](/Users/jeff/Developer/vivid/src/ui/graph/node_graph_input_click.cpp), and [src/ui/graph/node_graph_update.cpp](/Users/jeff/Developer/vivid/src/ui/graph/node_graph_update.cpp).
- **Two large operator implementation headers remain exactly as the original audit described.** [operators/control/tracker/tracker_core.h](/Users/jeff/Developer/vivid/operators/control/tracker/tracker_core.h) and [operators/control/drum_sequencer/drum_sequencer_core.h](/Users/jeff/Developer/vivid/operators/control/drum_sequencer/drum_sequencer_core.h) are still large implementation headers and remain worthwhile extraction targets.
- **The runtime↔UI shared-data boundary still needs a proper home.** The `graph_file_io.h` dependency on `ui/graph/dialog_types.h` is still the clearest concrete example.
- **Low-priority encapsulation cleanup is still available.** [src/ui/graph/node_graph.h](/Users/jeff/Developer/vivid/src/ui/graph/node_graph.h) and [src/runtime/graph/compiled_graph.h](/Users/jeff/Developer/vivid/src/runtime/graph/compiled_graph.h) still contain the sort of internal/public boundary issues called out in the audit. These are not urgent, but they remain valid cleanup candidates.
- **Minor duplication and naming cleanup are still background work.** The remaining duplication findings and the small naming outliers still make sense to clean up opportunistically, but they do not look like front-of-queue work anymore.

## Recommended Next Work

If follow-up work is going to continue, the best current order is:

1. **Finish test isolation and parallel-safety**
2. **Continue decomposing the remaining runtime and UI hotspots**
3. **Move shared graph metadata types out of the UI layer**
4. **Split the oversized test files by topic**
5. **Break down `cmake/tests.cmake` and related centralized build glue further**
6. **Extract the remaining large operator implementation headers when those operators are next in flight**
7. **Handle low-priority encapsulation, duplication, and naming cleanups opportunistically**

The main conclusion from the follow-up is that the audit did drive real improvement. The codebase is in noticeably better shape than the original phase documents now suggest. The remaining work is narrower than the original backlog and is concentrated mostly in test isolation, the last large-file hotspots, a few lingering boundary issues, and the remaining large operator/test/build concentration points.
