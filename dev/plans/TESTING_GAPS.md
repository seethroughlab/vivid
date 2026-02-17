# Testing Gaps Analysis

There are three distinct categories of testing in Vivid, each serving a different audience. This document identifies gaps in each so they can be addressed independently.

1. **Automated Tests** — CI and developer-facing. Catch2 + CTest.
2. **Agent Tools for User Projects** — Claude-facing. MCP tools + CLI commands for the inner loop.
3. **Agent Workflow for Framework Development** — Claude-facing. Documentation gap, not tooling.

---

## Category 1: Automated Tests (CI / Developer)

### What Exists

**Infrastructure:** Catch2 test framework, CTest integration, GitHub Actions CI on Linux + macOS + Windows + Raspberry Pi ARM64. Coverage via lcov/Codecov (Linux only).

**Test labels:** `unit`, `integration`, `build`, `smoke`, `visual`, `gui`, `mcp`, `cli`, `modules`, `examples`, `audio`.

**Test executables (15+):**

| Executable | Label(s) | What it tests |
|---|---|---|
| `test_effects_2d` | unit | 2D effect operators (Blur, Bloom, ChromaticAberration, etc.) |
| `test_module_registry` | unit, modules | Module loading and registration |
| `test_audio_analysis` | unit | Audio buffer analysis (RMS, peak, spectrum, crest factor) |
| `test_audio_assertions` | unit | `audio.*` assertion path resolution, conditional assertions |
| `test_particle_forces` | unit | Particle system physics |
| `test_gui` | unit, gui | Panel manager, layout, status bar, preferences |
| `test_integration` | integration | Core chain integration |
| `test_smoke` | smoke | Snapshot mode: examples run without crashing |
| `test_visual_regression` | visual | Output compared to reference images |
| `test_gui_visual_regression` | visual, gui | GUI snapshots with scripted interactions |
| `test_mcp` | mcp | MCP JSON-RPC: server init, tool listing, operator metadata |
| `test_cli_commands` | cli | `vivid build`, `params`, `graph`, `docs` |
| `test_export` | cli | `vivid export` audio/video output |
| `test_audio_evaluation` | cli | `vivid check` with audio assertions |
| `test_inspect` | cli | `vivid inspect` JSON structure, multi-sample, per-operator |

**Module tests:** vivid-audio (clock, sequencer, synthesis, effects, modulators), vivid-render3d, vivid-network (OSC, UDP, WebSocket), vivid-serial (serial/DMX), plus example smoke tests for video and MIDI modules.

### What's Well-Covered

- Operator param APIs (`test_effects_2d`) — getParam/setParam roundtrips, range clamping
- Audio analysis pipeline (`test_audio_analysis`, `test_audio_evaluation`) — RMS, spectrum, assertion evaluation
- Module dependency loading (`test_module_registry`) — registration order, missing deps
- GUI layout math (`test_gui`) — scroll clamping, pin layout, toolbar spacing, panel z-order
- CLI JSON output (`test_cli_commands`, `test_inspect`) — structured error parsing, topology dumps, multi-sample inspect
- MCP protocol (`test_mcp`) — JSON-RPC lifecycle, tool enumeration, operator metadata validation

### Gaps

#### Hot-reload — no tests

The hot-reload system (`hot_reload.h`: `setSourceFile`, `checkNeedsReload`, `reload`, `tryCompile`, `loadCompiled`) has zero test coverage. This is the most critical runtime path for the agent workflow (edit → reload → inspect → iterate). Untested behaviors:

- Successful reload after source change
- Failed compilation leaves previous chain running
- Compile error message parsing
- Library unload/reload cycle
- Parameter preservation across reloads

#### ~~Snapshot/preset system — no unit tests~~ DONE

~~`SnapshotStore` and `PresetCapable` had zero test coverage.~~ Added `test_snapshot` (easing + snapshot store: capture, recall, crossfade, interpolation types, management, JSON persistence) and `test_audio_presets` (FMSynth preset save/load, directory helpers). 15 new unit tests.

#### MCP live-instance tools — only docs/metadata tools tested

`test_mcp_server.cpp` (638 lines) tests server init, tool listing, `list_operators`, `search_docs`, and operator metadata validation. Every tool that requires a running Vivid instance is untested:

- Project lifecycle: `run_project`, `stop_project`
- Parameter control: `set_param`, `get_live_params`, `get_pending_changes`
- Introspection: `inspect_chain`, `get_chain_structure`, `get_frame_info`, `get_performance_stats`
- Capture: `capture_frame`, `capture_at_frame`, `capture_audio`
- Comparison: `compare_frames`, `compare_audio`, `sweep_param`, `sweep_param_audio`
- Animation: `advance_frames`, `reset_time`, `orbit_camera`, `param_ramp`
- Snapshots: `save_snapshot`, `recall_snapshot`, `list_snapshots`, `delete_snapshot`
- Solo/window: `solo_operator`, `exit_solo`, `set_window_mode`
- Reload: `wait_for_reload`, `get_runtime_status`, `get_compile_errors`

These would require a running Vivid instance in the test harness, which is a nontrivial CI challenge (GPU, window system).

#### Visual/smoke tests fragile in CI

Both `smoke` and `visual` test steps are `continue-on-error: true` in CI (`ci.yml:76,83`). They only run on Linux via xvfb with Mesa llvmpipe software rendering. wgpu-native surface creation doesn't work reliably with Mesa/xvfb. macOS and Windows CI only run `unit|integration|build` and `mcp` — no smoke or visual tests at all.

This means visual regressions can ship without being caught.

#### No GPU headless rendering path

The smoke/visual test fragility stems from needing a real GPU surface. wgpu-native supports headless rendering (render to texture without a surface), but Vivid doesn't expose this path. A headless mode would:

- Make smoke/visual tests reliable in CI (no xvfb, no Mesa)
- Enable macOS/Windows visual testing
- Remove `continue-on-error` from CI

#### Scripted export events untested

`test_export.cpp` tests audio flag behavior but not the `--script events.json` path. Event types (param_set, param_ramp, key_press, trigger, midi_note, snapshot_recall, mouse_move, mouse_click) are only exercised via GUI visual regression tests (`--snapshot-ui --script`), not via export.

---

## Category 2: Agent Tools for User Project Evaluation

### What Exists

The inner loop (`build` → `inspect` → `check` → iterate) is well-designed and documented in CLAUDE.md. The structured data pipeline works:

- **`vivid build`** — gate check, exit code 0/1, structured JSON errors
- **`vivid inspect`** — FrameAnalysis (brightness, contrast, histogram, spatial), AudioAnalysis (RMS, spectrum, crest factor), per-operator metrics, multi-sample mode
- **`vivid check`** — assertion evaluation against `vivid-assertions.json`, conditional guards (`after_frame`, `when_path`)
- **`inspect_chain` MCP** — same metrics from a running instance via WebSocket
- **Delta comparison** — `compare_frames` (RMSE, per-channel diff), `compare_audio` (RMS diff, spectral diff, correlation)
- **Parameter sweeps** — `sweep_param`, `sweep_param_audio` for exploring parameter space

### Footguns

#### Hang-on-error: every command except `vivid build` blocks forever on compile failure

`vivid inspect`, `vivid check`, and `vivid export` all hang indefinitely if the chain doesn't compile. CLAUDE.md documents this (`vivid build` is the only command that exits non-zero), but it's a footgun when the agent skips the build gate or when compilation breaks mid-session. The MCP `validate_chain` tool has a 60-second timeout as a safety net, but the CLI commands don't.

#### `validate_chain` uses `--snapshot /dev/null` instead of `vivid build`

`validate_chain` (`mcp_server.cpp:2074-2126`) compiles by running `--snapshot /dev/null --snapshot-frame 0` with a 60s timeout, rather than using `vivid build`. This means:

- Different error format: `--snapshot` mode outputs raw compiler stderr, while `vivid build` outputs structured JSON
- The tool parses errors from raw output via `parseCompileErrors()`, which may miss errors that `vivid build` catches
- It works (the timeout protects against hangs), but it's a separate code path from the CLI gate check

#### `capture_snapshot` returns PNG path but no metrics

`capture_snapshot` (`mcp_server.cpp:2044-2073`) spawns a new process, saves a PNG, and returns `{"success": bool, "output": path}`. It doesn't return any FrameAnalysis metrics. The agent must call `inspect_chain` separately from a running instance, or use `vivid inspect` via CLI. There's no single "capture and analyze" tool for headless use.

#### Single-instance constraint blocks A/B comparison

Only one Vivid instance can run at a time (WebSocket port 9876). `compare_frames` and `compare_audio` work on saved files, but the agent can't run two projects simultaneously to do live A/B testing. The workflow is: capture before → make change → capture after → compare. This works but adds friction.

#### `capture_frame` includes devtools UI overlay

`capture_frame` from a running instance captures the full composited frame including any visible UI panels (`mcp_server.cpp:1264` description: "When devtools are active, captures the full composited frame including UI panels"). This is documented but surprising — an agent capturing frames for comparison or evaluation gets UI chrome mixed into the image, which throws off RMSE comparisons. The workaround is to use `capture_snapshot` (spawns a clean process) or hide devtools first, but there's no `capture_frame --no-ui` option.

---

## Category 3: Agent Workflow for Framework Development

### The Real Gap: Documentation, Not Tooling

The original analysis proposed MCP tools (`vivid self-test`, `vivid self-build`, `vivid self-check`) for framework evaluation. But investigation reveals the primitives already exist — they just aren't documented in CLAUDE.md:

- **CTest has native JSON output**: `ctest --show-only=json-v1` returns structured test metadata; `--output-junit` produces JUnit XML. The agent doesn't need custom wrappers.
- **Compiler errors are already parseable**: C++ compilers emit standard `file:line:col: error:` format. Grep handles this natively.
- **Claude Code's Bash tool captures exit codes**: `cmake --build build` returns non-zero on failure; `ctest` returns non-zero when tests fail. No wrapper needed.
- **Test labels provide fine-grained control**: The existing label system (`unit`, `integration`, `build`, `smoke`, `visual`, `gui`, `mcp`, `cli`, `modules`, `examples`) lets the agent run exactly the right subset.

CLAUDE.md has 590+ lines of detailed agent workflow for user projects and **zero guidance on framework development**. The agent doesn't know about test labels, fast feedback loops, or what to run before pushing.

### Fix: Add "Framework Development" Section to CLAUDE.md

Document the framework development inner loop, test labels and commands, and pre-push checklist. This is the primary fix — no new tooling required.

### ~~Remaining Gaps (Real, Even With Documentation)~~ DONE

#### ~~No batch evaluation across projects~~ DONE

~~There's no tool to run all example projects and verify they still work after a framework change.~~ Added `test_batch_build` (`tests/batch/`): dynamically discovers all projects with `chain.cpp`, runs `vivid build` on each, reports pass/fail. Label: `build-all`. No GPU required — runs on all CI platforms. `ctest --test-dir build -L build-all --output-on-failure`.

#### ~~No mapping from operators to test projects~~ DONE

~~When modifying an operator (e.g., `Bloom`), the agent doesn't know which projects exercise it.~~ The batch build test extracts `chain.add<Type>()` patterns from every `chain.cpp` and writes `build/operator-coverage.json` mapping each operator type to the projects that use it. Query with: `cat build/operator-coverage.json | python3 -c "import sys,json; d=json.load(sys.stdin); print('\n'.join(d.get(sys.argv[1],[])))" Bloom`
