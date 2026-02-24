# Vivid — Agent Instructions

## What This Is

Vivid is a real-time audiovisual creative coding framework where audio and visuals are equal peers — authored in the same graph, driven by the same data. Read `docs/PRD.md` for the full vision.

## Architecture Overview

- **Language:** Zig (runtime, interface) + C++ (operators) compiled by Zig
- **GPU:** wgpu-native (Metal on macOS)
- **Audio:** miniaudio (device I/O only; DSP lives in audio operators)
- **Window:** GLFW 3.4
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
├── build.zig                 # Build system entry point
├── build.zig.zon             # Zig package manifest
├── deps/                     # Third-party source (compiled by Zig)
│   ├── wgpu/  ├── glfw/  ├── miniaudio/  ├── stb/  └── yyjson/
├── src/
│   ├── runtime/              # Core engine (Zig)
│   ├── interface/            # UI layer (Zig)
│   └── operator_api/         # C ABI contract (headers)
├── operators/                # Built-in operators (C++, each a directory)
│   ├── gpu/                  # noise/, blur/, particles/, composite/
│   ├── audio/                # oscillator/, delay/, fft_analysis/, beat_detect/
│   └── control/              # lfo/, clock/, midi_cc/, math/, envelope/
├── projects/                 # Example projects
└── docs/                     # Design documents
```

## Key Conventions

### Operators
- Each operator is a directory: `operators/gpu/noise/` containing `noise.cpp` and `noise.wgsl`
- Operators implement a C ABI defined in `src/operator_api/operator.h`
- During development: compiled to `.dylib`, loaded via `dlopen`, hot-reloaded on save
- For export: same source compiled statically into a single binary

### The JSON Graph
- The graph JSON is the single source of truth for a patch
- Node IDs are object keys (e.g., `"lfo1"`, `"particles1"`), not UUIDs
- Parameters in JSON carry current values only — metadata (min, max, tags) lives in operator C++ code
- Connections: `{"from": "node/port", "to": "node/port"}`
- Every wire implicitly carries a Spread (ordered collection); single values are Spreads of length 1

### Three Domains
- **GPU** (cyan `#4ECDC4`): textures, shaders, meshes, particles — wgpu
- **Audio** (amber `#F0A030`): synthesis, effects, analysis — miniaudio device I/O, operators do DSP
- **Control** (gray `#C0C8D0`): floats, events, MIDI, clocks — CPU, no fixed rate

Control is the hub. Audio and GPU never communicate directly — everything routes through Control.

### Building
```bash
zig build              # Build runtime + all operators
zig build run          # Build and run
zig build -Doperator=noise  # Build single operator (hot-reload)
```

### Visual Style
Dark steel background, monospace type, sharp corners. Domain identity through accent colors and preview content, not container shapes. See `docs/INTERFACE.md` §Visual Style for details.

## What NOT to Do

- Don't create a DSL or scripting layer — all logic is visible Control operators in the graph
- Don't embed a code editor — operator editing happens in the user's external IDE
- Don't use immediate-mode UI — the interface is retained-mode with custom widgets
- Don't add dependencies without checking `docs/ARCHITECTURE.md` §Dependency Manifest
- Don't make audio and GPU communicate directly — route through Control
