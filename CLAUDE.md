# Vivid — Code Navigation Guide

Vivid is a real-time audiovisual graph engine where audio and visuals are peers in the same graph. Operators process GPU textures, audio buffers, and control signals; the graph runtime compiles topology, executes at dual cadences (frame-rate and audio-rate), and hot-reloads operator code on save.

## Documentation Map

| Document | Purpose |
|----------|---------|
| `AGENTS.md` | Behavioral instructions: conventions, build commands, anti-patterns |
| `docs/ARCHITECTURE.md` | Design-level architecture: execution model, port types, lanes, operator contract |
| `docs/runtime/*.md` | Per-subsystem engineering docs (graph, audio engine, control server, packages, etc.) |
| `docs/INTERFACE.md` | UI architecture, visual style, session/variation surface |
| `docs/LLM-INTEGRATION.md` | MCP server design and LLM integration roles |
| `docs/COMPOSITION-GUIDE.md` | Mechanical primitives + reference-translation workflow for AV graphs. Opens with how to turn a user-supplied precedent (URL, YouTube, image, artist) into an operator graph; the rest is value-neutral mechanics (anti-patterns, metric thresholds, diagnostic tree). Not a menu of "good" patterns — what's compelling depends on the project. |

## Module Map

### Runtime (`src/runtime/`)

| Directory | Role | Deep Docs |
|-----------|------|-----------|
| `core/` | App bootstrap (`main.cpp`), main loop, `RuntimeCore` orchestration, settings, undo, hot reload, file watcher | `docs/runtime/architecture.md`, `docs/runtime/runtime_core.md` |
| `graph/` | Graph compiler (7-pass), `CompiledGraph`, frame executor, audio executor, lane state, snapshot types | [CLAUDE.md](src/runtime/graph/CLAUDE.md), `docs/runtime/graph.md` |
| `control/` | HTTP control server (port 9876), request dispatch, `RuntimeAPI` command layer | [CLAUDE.md](src/runtime/control/CLAUDE.md), `docs/runtime/control_server.md` |
| `operators/` | Operator registry, dylib loading/probing, scaffolding code generation, source-derived docs | `docs/runtime/operator_loader.md` |
| `packages/` | Package install/uninstall/link, CMake-based compilation, test runner, catalog | `docs/runtime/package_system.md` |
| `audio/` | Audio device I/O via miniaudio, `AudioFrameBridge` double-buffered cross-cadence transport | `docs/runtime/audio_engine.md` |
| `gpu/` | Dawn/WebGPU context, surface management, WGSL header parsing, Metal interop | `docs/runtime/gpu.md` |
| `debug/` | Capture coordinator, analysis, recording | — |
| `platform/` | macOS-specific: native menus, system MIDI | — |
| `assets/` | Asset library for workspace/package media | — |

### UI (`src/ui/`)

| Directory | Role | Deep Docs |
|-----------|------|-----------|
| `graph/` | Node graph editor — drawing, input handling, inspector, overlays | [CLAUDE.md](src/ui/graph/CLAUDE.md), `docs/INTERFACE.md` |
| `rendering/` | GPU-accelerated 2D renderer, thumbnail cache and renderer | — |
| `inspector/` | Parameter inspector layout and controller | — |
| `style/` | Theme loading and UI style constants | — |
| `dialogs/` | Dialog manager (draw + input), modal dialogs | — |

### Operator API (`src/operator_api/`)

The public contract between the runtime and all operators. See [CLAUDE.md](src/operator_api/CLAUDE.md).

### Other Top-Level Directories

| Directory | Role |
|-----------|------|
| `src/common/` | Shared utilities: GPU helpers, string utils, topological sort, path utils |
| `src/export/` | Standalone binary export pipeline |
| `operators/` | Seed operators organized by domain: `gpu/`, `audio/`, `control/`, `shared/` |
| `filters/` | Self-describing WGSL shader presets |
| `mcp/` | Python MCP bridge servers (`vivid_mcp.py`, `vivid_opdev_mcp.py`) |
| `tests/` | Integration and unit tests mirroring source structure |
| `graphs/` | Demo/example graph JSON files |
| `cmake/` | Build system modules (app, dependencies, operators, tests) |

## Key Concepts

**Three domains and dual cadences.** GPU and Control operators run at frame cadence (~60 Hz) on the main thread. Audio operators run at audio cadence (~48 kHz) on a real-time thread. Audio and GPU never communicate directly — everything routes through Control. See `docs/ARCHITECTURE.md` §5.3–5.4.

**Lanes.** Every value in the graph can carry multiple parallel elements. A scalar is a one-lane value; an FFT output might be 512 lanes. Multi-lane outputs automatically vectorize downstream operators. Lane-set provenance tracks alignment; `vivid_lane_state()` provides identity-keyed per-lane persistent state. See `docs/ARCHITECTURE.md` §5.9.

**Graph compilation.** The `GraphCompiler` transforms a `Graph` (pure data model) into a `CompiledGraph` (live execution state) in 7 passes. Topology changes trigger a full recompile — the `CompiledGraph` is never mutated during execution. See `src/runtime/graph/CLAUDE.md`.

**Operator contract.** Operators are self-contained `.dylib` compilation units loaded via `dlopen`. They inherit `OperatorBase` plus one domain mixin (`FrameProcessable`, `AudioProcessable`, or `GpuProcessable`). `VIVID_REGISTER` generates the `extern "C"` boundary. See `docs/ARCHITECTURE.md` §5.7.

**Cross-cadence bridges.** `AudioFrameBridge` uses lock-free double-buffered snapshots (`ParamSnapshot`, `AnalysisSnapshot`, `LaneSnapshot`) so neither cadence blocks the other. See `docs/ARCHITECTURE.md` §5.5.

## Subsystem Guides

Major subsystem directories have their own navigation guides where the orientation cost justifies it:

- [`src/runtime/core/CLAUDE.md`](src/runtime/core/CLAUDE.md) — app bootstrap, main loop, RuntimeCore
- [`src/runtime/graph/CLAUDE.md`](src/runtime/graph/CLAUDE.md) — graph compiler, executors, lane state
- [`src/runtime/control/CLAUDE.md`](src/runtime/control/CLAUDE.md) — control server, RuntimeAPI
- [`src/runtime/packages/CLAUDE.md`](src/runtime/packages/CLAUDE.md) — package lifecycle
- [`src/runtime/gpu/CLAUDE.md`](src/runtime/gpu/CLAUDE.md) — WebGPU context, WGSL header parser
- [`src/operator_api/CLAUDE.md`](src/operator_api/CLAUDE.md) — operator API headers
- [`src/ui/graph/CLAUDE.md`](src/ui/graph/CLAUDE.md) — node graph editor
- [`operators/CLAUDE.md`](operators/CLAUDE.md) — seed operator directory conventions
- [`mcp/CLAUDE.md`](mcp/CLAUDE.md) — MCP bridge servers
- [`tests/CLAUDE.md`](tests/CLAUDE.md) — test structure and partitioning
- [`cmake/CLAUDE.md`](cmake/CLAUDE.md) — build system modules
