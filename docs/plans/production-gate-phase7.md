# Plan: Production Gate — Phase 7 (code quality & test depth)

## Context

Phase 6 closed the schema-honesty gaps and pulled the dense CMake bash chain out into a shell script. Phase 7 finishes the pre-merge cleanup pass by attacking the maintainability and robustness issues the audit flagged: duplicate field-gathering between `collect()` and `collect_summary()`, fragile assumptions about CWD when writing per-graph health JSONs, the unmitigated 1KB CTest stdout truncation that pushes legitimate failures into `unknown`, and the lack of any automated test for the diagnostics-panel health pill.

Phase 7 ships behavior-preserving refactors plus three small but real improvements. Schema is unchanged. No new probes. No new endpoints. The branch becomes easier to keep correct as Phase 8 starts adding probes.

Working in worktree branch `worktree-production-gate-and-health` at `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-and-health`. Phases 1–6 changes remain uncommitted alongside Phase 7.

## Decisions locked in (with rationale)

- **DRY via a shared `populate_minimal()` free function** in `runtime_health.cpp`'s anonymous namespace. Both `collect()` and `collect_summary()` call it; `collect()` then adds the per-node top-N aggregation. Keeps the public API unchanged.
- **`tests/integration/test_demo_graphs.cpp` accepts `--health-dir <path>`** with a default derived from `<argv[1]>/../reports/health`. The gate's invocation already places `argv[1]` at `<build>/graphs`, so the default resolves to `<build>/reports/health` — same path as today, but now explicit and overridable. Devs running the test from anywhere can point it at their preferred output dir.
- **Filename collision protection** — replace `<basename>.json` with the path relative to `argv[1]` with slashes converted to underscores. `intro/showcase_demo.json` → `intro_showcase_demo.json`. Eliminates the latent collision risk without needing a manifest.
- **`tools/production_gate_report.py` gains `--ctest-log-dir <path>`** (defaults to `<junit_dir>/../Testing/Temporary`). When `classify()` returns `unknown`, the tool lazily loads `LastTest.log` and re-classifies against the per-test full stdout extracted by regex. Mitigates the documented 1KB JUnit truncation. CTest writes this log unconditionally — no extra flag needed.
- **UI pill test via pure-function extraction**, NOT via a Renderer2D mock. Extract the anonymous-namespace `health_color()` from `node_graph_draw_overlays.cpp` into a tiny public header `src/ui/graph/health_color.h`. Unit-test `health_color(Severity::X)` returns the expected RGB. No GPU, no draw introspection, ~5 lines of test code. Visual rendering correctness stays covered by `test_ui_screenshot_smoke`.

## Scope

In scope:
- Refactor `runtime_health.cpp` to extract `populate_minimal(snap, graph, core, audio, gpu)`. `collect()` and `collect_summary()` both call it; existing tests stay unchanged (behavior-preserving).
- Extract `health_color()` and `HealthRgb` from `node_graph_draw_overlays.cpp` to `src/ui/graph/health_color.h`. Update the call site to include + use it.
- Extend `tests/ui/test_ui_overlay_interactions.cpp` with a case asserting `health_color(Severity::{Ok,Warning,Error,Fatal})` returns the expected RGB tuples.
- Add `--health-dir <path>` to `tests/integration/test_demo_graphs.cpp`. Default to `<argv[1]>/../reports/health`. Document in `docs/testing/production-gate.md`.
- Replace `<basename>.json` with `<rel_path_with_underscores>.json` in `write_health_dump()`. Compute the relative path against `argv[1]` (the graphs root).
- Add `--ctest-log-dir <path>` to `tools/production_gate_report.py`. Lazily load `LastTest.log` when a failure classifies as `unknown`; extract the per-test stdout by regex anchors and re-classify. Cache the log so we don't re-read for each unknown.
- Update `cmake/tests/90-production-gate.cmake` and `scripts/run_production_gate_profile.sh` to pass `--ctest-log-dir` to the report tool. (The default already resolves correctly; explicit pass-through documents the intent.)
- Extend `tests/cli/test_production_gate_report.py` with a fallback-classification case: a fixture JUnit with truncated `<system-out>` + a synthetic LastTest.log → expect re-classified failure.
- Update `docs/testing/production-gate.md` with the `--health-dir` and `--ctest-log-dir` notes.

Out of scope (Phase 8+):
- New runtime probes.
- Schema additions.
- A general Renderer2D mock framework — only the small pure-function extraction.

## Files

New:
- `src/ui/graph/health_color.h` — `HealthRgb` POD + `health_color(runtime_health::Severity)` declaration. Inline definition lives in the header (small, branch-only function).
- `tests/cli/fixtures/junit/unknown_then_classifiable.xml` — JUnit with truncated `<system-out>` that trips `unknown`.
- `tests/cli/fixtures/ctest_logs/unknown_then_classifiable_LastTest.log` — synthetic LastTest.log with full output containing a `webgpu_error`-classifiable signature.

Modified:
- `src/runtime/core/runtime_health.cpp` — extract `populate_minimal()`.
- `src/ui/graph/node_graph_draw_overlays.cpp` — replace anonymous `health_color()` with `#include "ui/graph/health_color.h"` and use the public function.
- `tests/integration/test_demo_graphs.cpp` — `--health-dir` flag, collision-safe filename, default derived from argv[1].
- `tools/production_gate_report.py` — `--ctest-log-dir` flag, fallback classifier, lazy log loader + cache.
- `tests/cli/test_production_gate_report.py` — new fallback-classification case.
- `tests/ui/test_ui_overlay_interactions.cpp` — new pill-color unit test.
- `cmake/tests/90-production-gate.cmake` — pass `--ctest-log-dir` (or rely on default; both work).
- `scripts/run_production_gate_profile.sh` — same.
- `docs/testing/production-gate.md` — document new flags.

## Code sketches

### `populate_minimal()` extraction

```cpp
// runtime_health.cpp (anonymous namespace)
void populate_minimal(RuntimeHealthSnapshot& snap,
                      const Graph& graph,
                      const RuntimeCore& core,
                      const AudioEngine* audio_engine,
                      const GpuContext* gpu_context) {
    if (audio_engine) {
        snap.audio.running = audio_engine->running();
        snap.audio.sample_rate = static_cast<int>(audio_engine->sample_rate());
        snap.audio.buffer_size = static_cast<int>(audio_engine->buffer_size());
        snap.audio.node_count  = static_cast<int>(audio_engine->node_count());
        snap.audio.xruns       = audio_engine->underrun_count();
        snap.audio.last_buffer_underrun = audio_engine->last_buffer_underrun();
        snap.audio.load        = static_cast<double>(audio_engine->audio_load());
    }
    snap.audio.lane_overflow_count = core.audio_frame_bridge().lane_overflow_count();

    if (gpu_context) {
        snap.gpu.device_lost = gpu_context->device_lost();
        snap.gpu.last_error  = gpu_context->last_error();
    }

    snap.prior_crash_operator = core.prior_crash_operator();

    snap.graph.declared_nodes        = static_cast<int64_t>(graph.nodes().size());
    snap.graph.declared_connections  = static_cast<int64_t>(graph.connections().size());

    const auto* cg = core.compiled_graph();
    if (!cg) return;

    snap.graph.compiled_nodes  = static_cast<int64_t>(cg->nodes.size());
    snap.graph.frame_nodes     = static_cast<int64_t>(cg->frame_order.size());
    snap.graph.audio_nodes     = static_cast<int64_t>(cg->audio_order.size());
    snap.graph.total_edges     = static_cast<int64_t>(cg->edges.size());
    snap.graph.frame_edges     = static_cast<int64_t>(cg->frame_direct_edges.size());
    snap.graph.audio_edges     = static_cast<int64_t>(cg->audio_direct_edges.size());
    snap.graph.snapshot_edges  = static_cast<int64_t>(
        cg->frame_to_audio_edges.size() + cg->audio_to_frame_edges.size());
    snap.graph.dropped_connections = static_cast<int64_t>(cg->dropped_connections.size());

    std::set<std::string> missing_types;
    for (const auto& n : cg->nodes) {
        if (n.errored) snap.graph.errored_nodes++;
        if (n.missing_operator) {
            snap.graph.missing_operators++;
            if (!n.type_name.empty()) missing_types.insert(n.type_name);
        }
        if (n.gpu) {
            snap.gpu.texture_nodes++;
            if (n.gpu->shader_error) snap.gpu.shader_errors++;
        }
    }
    snap.graph.missing_operator_types.assign(missing_types.begin(), missing_types.end());
}
```

`collect()` becomes ~30 lines shorter; `collect_summary()` becomes ~15 lines.

### `health_color.h`

```cpp
#pragma once
#include "runtime/core/runtime_health.h"

namespace vivid::ui {

struct HealthRgb { float r, g, b; };

inline HealthRgb health_color(vivid::runtime_health::Severity s) {
    using S = vivid::runtime_health::Severity;
    switch (s) {
        case S::Ok:      return {0.30f, 0.85f, 0.40f};
        case S::Warning: return {0.95f, 0.82f, 0.30f};
        case S::Error:   return {0.95f, 0.55f, 0.20f};
        case S::Fatal:   return {0.95f, 0.35f, 0.30f};
    }
    return {0.30f, 0.85f, 0.40f};
}

}  // namespace vivid::ui
```

Header-only inline function — no .cpp needed, no link-time impact.

### Per-graph filename + `--health-dir`

```cpp
// In test_demo_graphs.cpp:run_single_graph (post-tick health dump section)
std::filesystem::path graph_root = ...;  // from argv[1] passed via env or arg
std::filesystem::path graph_full = graph_path;
std::filesystem::path rel = std::filesystem::relative(graph_full, graph_root);
std::string flat = rel.string();
std::replace(flat.begin(), flat.end(), '/', '_');
// flat ends with ".json"; strip it then add it back for clarity
auto out_path = health_dir / flat;
```

Default `health_dir` is `<argv[1]>/../reports/health`; CLI overrides via `--health-dir`.

### LastTest.log fallback

```python
# In production_gate_report.py
class LastTestLog:
    """Lazy loader for ${ctest_log_dir}/LastTest.log + per-test extractor."""
    def __init__(self, log_dir: Path | None):
        self.log_dir = log_dir
        self._text: str | None = None
        self._index: dict[str, str] = {}  # test_name -> full output

    def output_for(self, test_name: str) -> str:
        if self.log_dir is None: return ""
        if self._text is None:
            log_path = self.log_dir / "LastTest.log"
            try: self._text = log_path.read_text(errors="ignore")
            except OSError: self._text = ""
            self._build_index()
        return self._index.get(test_name, "")

    def _build_index(self):
        # Anchor pattern: "N/M Testing: <name>" then later "Output:" then "----"
        # then text until "----\nTest time =" or next "N/M Testing:"
        ...

# In build_report:
last_test = LastTestLog(args.ctest_log_dir)
for c in cases:
    if c["failed"]:
        cls = classify(c["name"], log_for_class, c["failure_type"])
        if cls == "unknown":
            full_log = last_test.output_for(c["name"])
            if full_log:
                cls = classify(c["name"], full_log, c["failure_type"])
        ...
```

The classifier function itself is unchanged — we just feed it more text.

## Test design

`tests/cli/test_production_gate_report.py` — new case:

```python
def test_unknown_classification_falls_back_to_lasttestlog(tmp_path):
    """A failure whose <system-out> is too short to classify should reclassify
    once we read LastTest.log."""
    # Synthetic JUnit with empty system-out + failed test
    # Synthetic LastTest.log with full webgpu validation message
    # Run report, expect classification == "webgpu_error"
```

`tests/ui/test_ui_overlay_interactions.cpp` — new case:

```cpp
{
    using vivid::ui::health_color;
    using S = vivid::runtime_health::Severity;
    auto ok = health_color(S::Ok);
    check(ok.r == 0.30f && ok.g == 0.85f, "Ok pill is green");
    auto warn = health_color(S::Warning);
    check(warn.r > 0.9f && warn.g > 0.7f, "Warning pill is amber");
    auto err = health_color(S::Error);
    check(err.r > 0.9f && err.g < 0.6f, "Error pill is orange-red");
    auto fatal = health_color(S::Fatal);
    check(fatal.r > 0.9f && fatal.g < 0.4f, "Fatal pill is red");
}
```

Existing tests stay unchanged. Total: +2 test cases on the C++ side, +1 on the Python side.

## Verification

Local (in worktree):
```bash
# Pytest including the new fallback case
uv run --with pytest pytest tests/cli/test_production_gate_report.py -v

# C++ tests including the new pill assertion
cmake --build build --target test_runtime_health_snapshot test_ui_overlay_interactions \
                            test_control_server -j$(sysctl -n hw.logicalcpu)
ctest --test-dir build -R 'test_runtime_health_snapshot|test_ui_overlay_interactions|test_control_server' \
      --output-on-failure

# Per-graph filename collision-safe
ls build/reports/health/ | grep -E '_' | head   # rel-path filenames

# Run from worktree root with explicit health-dir
./build/test_demo_graphs build/graphs --health-dir /tmp/dev_health_test
ls /tmp/dev_health_test | head

# Full gate end-to-end
cmake --build build --target production_gate_core -j$(sysctl -n hw.logicalcpu)
```

Negative test:
- Force a failure whose JUnit `<system-out>` truncates before the WebGPU error message; confirm without `--ctest-log-dir` it classifies as `unknown`, with the flag (or default) it classifies correctly.

## Risks

1. **`populate_minimal()` ordering subtle.** `collect_summary()` previously didn't populate sample_rate, buffer_size, etc. — only the bits the rollup needed. The shared function now populates everything, slightly increasing per-frame cost. Negligible (no allocation), but documenting that the cost is uniform between collect/collect_summary.
2. **LastTest.log regex fragility.** The anchor format depends on CTest's output convention. Pinning to the observed pattern (`N/M Testing: <name>` + `Output:\n----\n...`) — if CTest changes this, the fallback silently degrades to `unknown`. Acceptable: it's a fallback, and the behavior without the fallback is what we have today.
3. **Filename change is observable.** Anything that consumed `build/reports/health/<basename>.json` in CI artifacts will see new names. We're the only consumer; budget evaluation reads via glob; no external scripts touch these files.
4. **`health_color.h` couples UI to runtime_health.h.** Already true via `graph_snapshot.h` (Phase 4). No new layer crossings.

## What I will do on approval

1. Move this plan to `docs/plans/production-gate-phase7.md`.
2. Refactor `runtime_health.cpp` with `populate_minimal()`. Run `test_runtime_health_snapshot` to confirm no behavior change.
3. Add `src/ui/graph/health_color.h`; update `node_graph_draw_overlays.cpp`. Add the pill test case.
4. Add `--health-dir` to `test_demo_graphs.cpp`; switch to relative-path filenames.
5. Add `--ctest-log-dir` to `production_gate_report.py` with the LastTest.log fallback. Add the pytest fallback case.
6. Update CMake/script to pass the flag (or rely on default). Update `production-gate.md`.
7. Run pytest, run targeted ctest, run `production_gate_core`. Confirm filenames are now `<dir>_<basename>.json` and the gate still passes.
8. Report back.
