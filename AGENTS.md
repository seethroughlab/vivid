# Vivid — Agent Instructions

## What This Is

Vivid is a real-time audiovisual creative coding framework where audio and visuals are equal peers — authored in the same graph, driven by the same data. Read `docs/PRD.md` for the full vision.

## Design Philosophy

**Vivid's value is the environment, not the operators.**

In the age of LLM-assisted development, writing an operator that does exactly what you want is cheap. What's expensive is making experimentation and discovery possible and productive. This has concrete implications for how you work on Vivid:

- **Compose first, author when needed.** When a user needs functionality, the LLM should first check what operators already exist (`list_types` returns seed operators and all installed packages) and compose them via graph wiring. Only scaffold a new operator when no existing combination achieves the goal.
- **When authoring, design for reuse.** LLM-generated operators should have generic names, broadly useful parameters, and clear single responsibilities — not one-off implementations tied to a specific patch. They live in the project directory and become part of the user's growing library.
- **Vivid ships seed operators and a growing package ecosystem.** The built-in operators validate each domain and serve as building blocks. Installed packages extend the palette. LLM-generated operators fill gaps that neither provides.
- **The operator contract is the most important API surface.** Every friction point in writing an operator — unclear types, boilerplate, implicit conventions — is a direct tax on the core workflow. When improving Vivid, prioritize making operator authoring simpler and more reliable.
- **Exploration > construction.** Since writing operators is cheap, the bottleneck is the experimentation loop: how fast can the user try an idea, hear it, see it, and try the next one? Optimize for this loop in every design decision.
- **When scaffolding operators:** use the existing seed operators as templates. Match the patterns in `operators/gpu/`, `operators/audio/`, `operators/control/` — same file structure, same macro conventions, same parameter style. This is what makes LLM-generated operators reliable.

## Architecture Overview

- **Language:** C++ throughout (runtime, interface, and operators)
- **GPU:** Dawn (Google's WebGPU implementation, fetched via FetchContent at build time)
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
| Implementing a roadmap phase | `docs/LEGACY-REFERENCE.md` |
| Runtime core development | `docs/runtime/architecture.md` then subsystem doc below |

## Runtime Core Development

When working on `src/runtime/`, read the architecture doc first, then the relevant subsystem doc:

| Subsystem | Doc | Key source files |
|-----------|-----|-----------------|
| Overall architecture | `docs/runtime/architecture.md` | `src/runtime/main.cpp` |
| Graph data model | `docs/runtime/graph.md` | `graph.h/cpp` |
| Scheduler / execution | `docs/runtime/scheduler.md` | `scheduler.h/cpp` |
| Audio engine | `docs/runtime/audio_engine.md` | `audio_engine.h/cpp` |
| HTTP control server | `docs/runtime/control_server.md` | `control_server.h/cpp` |
| Operator loading / ABI | `docs/runtime/operator_loader.md` | `operator_loader.h/cpp`, `operator_registry.h/cpp` |
| RuntimeAPI (C++ commands) | `docs/runtime/runtime_api.md` | `runtime_api.h/cpp` |
| Package system | `docs/runtime/package_system.md` | `package_manager.h/cpp`, `package_compiler.h/cpp` |
| Hot reload | `docs/runtime/hot_reload.md` | `hot_reload.h/cpp` |
| GPU / WebGPU | `docs/runtime/gpu.md` | `gpu_context.h/cpp` |

## Project Structure

```
vivid/
├── CMakeLists.txt
├── AGENTS.md
├── deps/                     # Third-party (git submodules + FetchContent)
│   ├── glfw/  ├── glfw3webgpu/  ├── hap/  ├── miniaudio/  ├── oscpack/  ├── rtmidi/  ├── stb/  ├── syphon/  └── yyjson/
├── src/
│   ├── runtime/              # Core engine (~30 modules)
│   │   ├── main.cpp          # Entry point, GLFW loop, input dispatch
│   │   ├── graph.cpp/.h      # JSON graph data model
│   │   ├── scheduler.cpp/.h  # Topological tick, domain dispatch
│   │   ├── gpu_context.cpp/.h
│   │   ├── audio_engine.cpp/.h
│   │   ├── hot_reload.cpp/.h
│   │   ├── runtime_api.cpp/.h
│   │   ├── operator_loader.cpp/.h    # dlopen/dlsym plugin loading
│   │   ├── operator_registry.cpp/.h  # Type→plugin resolution
│   │   ├── operator_creator.cpp/.h   # LLM-assisted scaffolding
│   │   ├── control_server.cpp/.h     # HTTP API (port 9876)
│   │   ├── package_manager.cpp/.h    # Install/uninstall/list packages
│   │   ├── package_compiler.cpp/.h   # CMake-based package builds
│   │   ├── package_catalog.cpp/.h    # Remote catalog fetch + cache
│   │   ├── package_test_runner.cpp/.h
│   │   ├── settings.cpp/.h
│   │   ├── system_midi.cpp/.h
│   │   ├── macos_menu.h/.mm         # Native macOS menu bar
│   │   └── ...                       # file_watcher, fullscreen_blit, etc.
│   ├── ui/                   # Retained-mode node graph UI
│   │   ├── node_graph.cpp/.h         # Core graph editor
│   │   ├── node_graph_draw.cpp       # Rendering (wires, nodes, thumbnails)
│   │   ├── node_graph_input.cpp      # Mouse/key event handling
│   │   ├── renderer_2d.cpp/.h        # GPU-accelerated 2D primitives
│   │   ├── thumbnail_cache.cpp/.h
│   │   ├── thumbnail_renderer.cpp/.h
│   │   ├── theme_loader.cpp/.h       # JSON theme loading
│   │   ├── ui_style.cpp/.h           # Style constants
│   │   └── file_dialog.h/.mm         # Native file dialogs (macOS)
│   ├── common/               # Shared utilities
│   │   ├── gpu_util.h  ├── string_util.h  ├── topo_sort.h  └── system_info.h
│   ├── export/               # Standalone binary export
│   │   ├── export_pipeline.cpp/.h
│   │   ├── standalone_main.cpp
│   │   └── standalone.cmake.in
│   └── operator_api/         # Public operator contract headers (operator.h, types.h, gpu/audio bases, DSP utils, MIDI/media types)
├── operators/                # Seed operators (each a single-file directory)
│   ├── gpu/                  # 15 operators: noise, shape, composite, bloom, feedback,
│   │                         #   metaball, texture_loader, time_machine, text,
│   │                         #   texture_analysis, movie_loaded, movie_video_out,
│   │                         #   webcam_in, syphon_in, syphon_out
│   ├── audio/                # 12 operators: oscillator, gain, delay, reverb,
│   │                         #   bitcrush, distortion, filter, mixer, noise,
│   │                         #   spread_adsr, spread_lfo, movie_audio_out
│   ├── control/              # 24 operators: lfo, clock, math, envelope, midi_input,
│   │                         #   fft_analysis, euclidean, pat_transform, stack,
│   │                         #   alternate, gate, logic, random, smooth,
│   │                         #   modulated_gain, spread_noise, mouse, keyboard,
│   │                         #   basename, folder_list, osc_in, osc_out,
│   │                         #   step_counter, string_select
│   └── shared/               # Shared operator modules (media_session, movie_audio, movie_decode)
├── filters/                  # 25 self-describing WGSL shader presets
├── graphs/                   # 47 demo/example graph JSON files
├── mcp/                      # MCP server (Python bridge to control server)
│   ├── vivid_mcp.py
│   ├── vivid_opdev_mcp.py
│   ├── opdev_docs/
│   └── requirements.txt
├── site/                     # Website (landing page, package catalog)
├── platform/                 # Platform-specific resources
│   └── macos/                # Info.plist.in, Vivid.icns
├── fonts/                    # JetBrainsMono-Regular.ttf
├── tests/                    # 57 integration/unit tests
├── media/                    # Sample audio/video/image assets
└── docs/                     # Design documents
    └── runtime/              # Runtime subsystem internals (architecture, graph, scheduler, …)
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

### Package System
- Operators can be distributed as packages (git repos with `vivid-package.json`)
- `PackageManager` handles install/uninstall/link/rebuild/dependency resolution
- `PackageCompiler` does CMake-based builds of package operators
- Installed packages live in `~/.vivid/packages/`
- For development: `vivid link <path>` symlinks a local package, `vivid rebuild <name>` recompiles in-place

### Control Server
- HTTP API on port 9876 for runtime manipulation
- Endpoints for graph CRUD, parameter control, capture, package management
- MCP bridge (`mcp/vivid_mcp.py`) exposes control server to LLM tools

### Export Pipeline
- `src/export/` compiles a graph + its operators into a standalone binary
- Uses `standalone.cmake.in` template, generates self-contained CMakeLists

### Interactive Input
- When the graph UI is hidden, keyboard/mouse events are forwarded to operators via `VividInputState`
- Coordinates are normalized to [0,1] texture UV space accounting for fit mode

## What NOT to Do

- Don't create a DSL or scripting layer — all logic is visible Control operators in the graph
- Don't embed a code editor — operator editing happens in the user's external IDE
- Don't use immediate-mode UI — the interface is retained-mode with custom widgets
- Don't add dependencies without checking `docs/ARCHITECTURE.md` §Dependency Manifest
- Don't make audio and GPU communicate directly — route through Control

