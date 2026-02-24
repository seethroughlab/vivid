# Vivid — Agent Instructions

## What This Is

Vivid is a real-time audiovisual creative coding framework where audio and visuals are equal peers — authored in the same graph, driven by the same data. Read `docs/PRD.md` for the full vision.

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
| Priorities, open questions | `docs/ROADMAP.md` |

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
├── operators/                # Built-in operators (each a directory)
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
