# Vivid — Agent Instructions

## What This Is

Vivid is a real-time audiovisual creative coding framework where audio and visuals are equal peers — authored in the same graph, driven by the same data. Read `docs/PRD.md` for the full vision.

## Design Philosophy

**Vivid's value is the environment, not the operators.**

In the age of LLM-assisted development, writing an operator that does exactly what you want is cheap. What's expensive is making experimentation and discovery possible and productive. This has concrete implications for how you work on Vivid:

- **Operator authoring is the primary LLM workflow.** When a user needs a new operator, the LLM writes it as a self-contained C++ file, hot-reload compiles it in under a second, and the user sees the result immediately. Don't assume operators already exist — generate them.
- **Vivid ships seed operators, not a library.** The built-in operators exist to validate each domain and serve as examples. Most operators in a real project are LLM-generated during the session, authored for specific creative intent, and kept in the project directory.
- **The operator contract is the most important API surface.** Every friction point in writing an operator — unclear types, boilerplate, implicit conventions — is a direct tax on the core workflow. When improving Vivid, prioritize making operator authoring simpler and more reliable.
- **Exploration > construction.** Since writing operators is cheap, the bottleneck is the experimentation loop: how fast can the user try an idea, hear it, see it, and try the next one? Optimize for this loop in every design decision.
- **When scaffolding operators:** use the existing seed operators as templates. Match the patterns in `operators/gpu/`, `operators/audio/`, `operators/control/` — same file structure, same macro conventions, same parameter style. This is what makes LLM-generated operators reliable.

## Architecture Overview

- **Language:** C++ throughout (runtime, interface, and operators)
- **GPU:** Dawn (Google's WebGPU implementation, C++ used directly)
- **Audio:** miniaudio (device I/O only; DSP lives in audio operators)
- **Window:** GLFW 3.4
- **Build:** CMake
- **Platform:** macOS first (cross-platform later, architecture supports it)

## Before You Start

Read the relevant doc for your task:

| Task | Read First |
|------|-----------|
| Understanding the project | `docs/PRD.md` |
| Runtime, graph, operators, build | `docs/ARCHITECTURE.md` |
| UI, widgets, layout, visual style | `docs/INTERFACE.md` |
| LLM chat, MCP server, perception | `docs/LLM-INTEGRATION.md` |
| Implementing a roadmap phase | §Legacy Reference below |

## Project Structure

```
vivid/
├── CMakeLists.txt            # Top-level build
├── deps/                     # Third-party (git submodules or FetchContent)
│   ├── dawn/  ├── glfw/  ├── miniaudio/  ├── stb/  └── yyjson/
├── src/
│   ├── runtime/              # Core engine
│   │   ├── main.cpp
│   │   ├── graph.cpp/.h
│   │   ├── scheduler.cpp/.h
│   │   ├── spreads.cpp/.h
│   │   ├── bridges.cpp/.h
│   │   ├── params.cpp/.h
│   │   ├── gpu_context.cpp/.h
│   │   ├── audio_context.cpp/.h
│   │   ├── hot_reload.cpp/.h
│   │   └── runtime_api.cpp/.h
│   ├── interface/            # UI layer
│   │   ├── widgets/
│   │   ├── layout.cpp/.h
│   │   ├── input.cpp/.h
│   │   ├── renderer.cpp/.h
│   │   ├── theme.cpp/.h
│   │   └── text.cpp/.h
│   └── operator_api/        # Shared headers for operator contract
│       ├── operator.h
│       ├── spread.h
│       └── types.h
├── operators/                # Seed operators (each a directory)
│   ├── gpu/                  # noise/, blur/, particles/, composite/
│   ├── audio/                # oscillator/, delay/, fft_analysis/, beat_detect/
│   └── control/              # lfo/, clock/, midi_cc/, math/, envelope/
├── projects/                 # Example projects
└── docs/                     # Design documents
```

## Key Conventions

### Operators
- Each operator is a directory: `operators/gpu/noise/` containing `noise.cpp` and `noise.wgsl`
- Operators `#include "operator.h"` and use C++ types (Param<float>, base classes)
- The dlopen boundary uses `extern "C"` functions produced by `VIVID_REGISTER` macro
- During development: compiled to `.dylib`, loaded via `dlopen`, hot-reloaded on save
- For export: same source compiled and statically linked into a single binary
- **Composite operators (ChildOp):** Control operators can embed other operators as persistent members
  via `ChildOp<T>` (`src/operator_api/child_op.h`). The child must be header-only (e.g. `control/lfo/lfo.h`).
  Parent sets child params, calls `child.process(ctx)`, reads child outputs. See
  `operators/control/modulated_gain/modulated_gain.cpp`. Control-domain only — audio needs per-sample
  processing, GPU uses shader pipelines.

### The JSON Graph
- The graph JSON is the single source of truth for a patch
- Node IDs are object keys (e.g., `"lfo1"`, `"particles1"`), not UUIDs
- Parameters in JSON carry current values only — metadata (min, max, tags) lives in operator code
- Connections: `{"from": "node/port", "to": "node/port"}`
- Every wire implicitly carries a Spread (ordered collection); single values are Spreads of length 1

### Three Domains
- **GPU** (cyan `#4ECDC4`): textures, shaders, meshes, particles — Dawn/WebGPU
- **Audio** (amber `#F0A030`): synthesis, effects, analysis — miniaudio device I/O, operators do DSP
- **Control** (gray `#C0C8D0`): floats, events, MIDI, clocks — CPU, no fixed rate

Control is the hub. Audio and GPU never communicate directly — everything routes through Control.

### Building
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/vivid                         # Run
cmake --build build --target noise    # Build single operator (hot-reload)
```

### Visual Style
Dark steel background, monospace type, sharp corners. Domain identity through accent colors and preview content, not container shapes. See `docs/INTERFACE.md` §Visual Style for details.

## What NOT to Do

- Don't create a DSL or scripting layer — all logic is visible Control operators in the graph
- Don't embed a code editor — operator editing happens in the user's external IDE
- Don't use immediate-mode UI — the interface is retained-mode with custom widgets
- Don't add dependencies without checking `docs/ARCHITECTURE.md` §Dependency Manifest
- Don't make audio and GPU communicate directly — route through Control

## Legacy Reference

The `legacy` branch (777 commits) is a mature, monolithic C++ implementation that covers nearly every feature on the roadmap. The current master is a clean rewrite with a different architecture (C ABI + dlopen operators, JSON graph, Dawn/WebGPU), so legacy code should never be copied verbatim. Instead, read it for **patterns and design decisions** — bind group caching, RGBA16Float intermediates, generation-based cooking, RAII handles — that would otherwise be rediscovered through debugging.

### Reading legacy without switching branches

```bash
git show legacy:<path>                    # View a single file
git ls-tree --name-only legacy <dir>/     # List a directory
git grep <pattern> legacy -- '<glob>'     # Search across files
```

### Phase → Legacy File Mapping

All paths are relative to the legacy branch root. Core engine files live under `modules/vivid-core/` unless a full module path is shown.

| Phase | Legacy files to consult |
|-------|------------------------|
| 5: Control→GPU | `include/vivid/param.h`, `include/vivid/param_registry.h`, `src/context.cpp` |
| 6: Audio Output | `src/audio_output.cpp`, `src/audio_graph.cpp`, `include/vivid/audio_output.h`, `include/vivid/audio_graph.h` |
| 7: Audio→Control | `src/audio_analysis.cpp`, `src/av_analysis.cpp`, `include/vivid/audio_analysis.h` |
| 8: Hot-Reload | `src/hot_reload.cpp`, `include/vivid/hot_reload.h`, `src/shader_preprocessor.cpp` |
| 9: REPL | `src/cli/runtime_api.cpp`, `src/cli/cli.cpp` |
| 10: MIDI | `modules/vivid-midi/src/midi_in.cpp`, `modules/vivid-midi/src/midi_out.cpp` |
| 11: UI Node Graph | `src/gui/node_graph.cpp`, `src/gui/gui.cpp`, `src/gui/panel_manager.cpp` |
| 12: Thumbnails | `src/gui/scratch_texture.cpp`, `include/vivid/operator_viz.h` |
| 13: Spreads | `include/vivid/dsp_utils.h`, `src/effects/gpu_particles.cpp` |
| 14: Polyphonic Audio | `modules/vivid-audio/src/poly_synth.cpp`, `modules/vivid-audio/src/envelope.cpp`, `modules/vivid-audio/src/sequencer.cpp` |
| 15: Instance Operator | `src/effects/gpu_particles.cpp` (instanced rendering pattern) |
| 16: MCP Server | `src/cli/mcp_server.cpp`, `docs/MCP-TOOLS.md` |
| 17: Chat Panel | `modules/vivid-imgui/`, `src/cli/runtime_api.cpp` |
| 20: Patterns | `modules/vivid-audio/src/sequencer.cpp`, `modules/vivid-audio/src/euclidean.cpp`, `modules/vivid-audio/src/arpeggiator.cpp` |
| 22: Export | `src/cli/main_production.cpp`, `include/vivid/video_exporter.h`, `include/vivid/snapshot.h` |
| 23: Operator Library | `include/vivid/module_manager.h`, `include/vivid/module_registry.h`, `docs/MODULES.md` |
| 24: LLM Perception | `src/cli/analysis_hints.cpp`, `src/cli/assertion.cpp`, `docs/ANALYSIS-TOOLS.md` |
| 25: WebSocket API | `src/cli/runtime_api.cpp`, `docs/WEBSOCKET_API.md` |

### Legacy docs worth reading

- `docs/CREATING-OPERATORS.md` — operator lifecycle, param registration, GPU resource patterns
- `docs/RECIPES.md` — complete audio-visual chain examples
- `docs/MCP-TOOLS.md` — MCP tool catalog (Phase 16 target)
- `docs/MODULES.md` — module system design (Phase 23 target)
- `docs/ANALYSIS-TOOLS.md` — perception/introspection (Phase 24 target)
