# System Architecture

## 5.1 Two-Tier Interaction Model

**Fast path** — parameter adjustments and connection routing are instantaneous. No compilation, just reconfiguring the live graph. This layer is entirely declarative (parameters, topology), making it inherently LLM-friendly.

**Slow path** — editing an operator's C++ implementation. The user right-clicks a node and selects "Edit in IDE," which opens the operator's source file in their configured external editor (VS Code, CLion, etc.). On save, Vivid detects the file change, builds just that operator's shared library via the bundled compiler, and hot-swaps it into the running graph. Expected latency: 1–3 seconds. For new operators, Vivid scaffolds the boilerplate C++ with the correct base class and port declarations before opening the file.

## 5.2 Compiler Toolchain

**Decision: Bundle Zig as the default compiler,** with an advanced setting to point at a custom system compiler. Zig ships as a single ~40MB binary with a full C/C++ compiler (Clang frontend), requires no system dependencies, and has first-class support for building shared libraries with stable C ABIs. Its build system is Zig code, so build configurations for each operator can be generated programmatically. Bundling means zero-friction onboarding, a consistent compiler version, and a self-contained app bundle.

## 5.3 Three Domains

The system has three execution domains, each with distinct timing and resource characteristics:

- **Control** — floats, ints, bools, events, strings, buffers. Updated at arbitrary rates. Runs on the main thread or a dedicated control thread. No fixed timing — values propagate immediately on change.
- **Audio** — sample buffers at a fixed rate (48kHz typical). Runs on a real-time audio thread managed by miniaudio. Operators produce a buffer every callback, even if silence.
- **GPU** — textures, shaders, meshes, compute buffers. Runs at display refresh rate. Operators execute as wgpu render/compute passes.

## 5.4 Execution Model: Hybrid Push/Pull

Control is push-based — events propagate forward immediately. Audio and GPU are pull-based — driven by their respective hardware clocks. When a Control change reaches the boundary of an Audio or GPU subgraph, it updates the parameter store. The next pull cycle picks it up. No domain ever waits on another.

## 5.5 Domain Bridges: Control as Hub

**Decision: Control sits at the center of a star topology.** Audio and GPU never communicate directly — everything routes through Control. This simplifies the architecture from six specialized bridges to two bidirectional mechanisms:

### Control ↔ Audio
- **Control → Audio:** lock-free ring buffer or atomic. Audio callback reads at next block boundary. Latency: ~5ms at 256 samples / 48kHz.
- **Audio → Control:** audio analysis operators write results into a lock-free queue. Control nodes poll at whatever rate they like.

### Control ↔ GPU
- **Control → GPU:** atomics or double-buffered parameter store. GPU render loop picks up changes next frame. Latency: ~16ms at 60fps.
- **GPU → Control:** async readback from GPU staging buffers. Control emits events when data lands. Latency: 1–2 frames.

**Why not direct Audio ↔ GPU?** Audio and Control both live on the CPU. The "hop" through Control is just a CPU-side buffer copy (nanoseconds), followed by the same CPU→GPU upload that would happen regardless. The only real domain boundary is CPU↔GPU, and that crossing happens exactly once no matter how the data is routed.

**Backup approach:** If six explicit per-pair bridges prove to share enough machinery during implementation, a unified port abstraction may emerge naturally from the bottom up.

## 5.6 Port Type System

The type system serves three consumers: the graph runtime (bridge selection), the UI (valid connection enforcement), and the LLM (compatibility reasoning).

### Control Port Types
Control::Float, Control::Int, Control::Bool, Control::String, Control::Event (discrete trigger with optional payload), Control::Buffer (arbitrary blobs — JSON, point clouds, FFT spectra). These update at no fixed rate.

### Audio Port Types
Audio::Mono, Audio::Stereo, Audio::Multichannel(n). Implicitly carry sample rate and block size. Always continuous — producing a buffer every callback, even if silence.

### GPU Port Types
GPU::Texture2D, GPU::Texture3D, GPU::Buffer, GPU::Mesh. Each carries format metadata: resolution, pixel format, color space for textures; vertex layout, index count for meshes. Meshes are GPU domain data — their purpose and consumption is GPU rendering, even when vertex generation happens on the CPU (procedural geometry). CPU-side construction is an implementation detail of the operator, not a domain classification. If mesh properties need to feed back into the graph (vertex count, bounding box), they use the same GPU→Control async readback bridge that texture analysis uses.

### Semantic Tags (Advisory)
Port types can carry optional semantic tags: normalized (0–1), bipolar (-1 to 1), frequency_hz, decibels, midi_note, etc. **Tags are advisory hints, not enforced by the runtime.** When connecting ports with mismatched ranges, the graph editor suggests inserting a visible Remap node with the mapping pre-configured. No silent auto-mapping.

## 5.7 Operator API Contract

Each operator is a self-contained compilation unit — a shared library with a known C ABI interface. The graph runtime introspects inputs, outputs, and parameter declarations:

```cpp
#include "vivid/operator.h"
struct MyFilter : vivid::ImageOp {
    Param<float> intensity{"intensity", 0.5, 0.0, 1.0};
    void process(const Image& in, Image& out) override { ... }
};
VIVID_REGISTER(MyFilter)
```

The simpler this contract, the better everything downstream works: auto-generated UI knobs, confident LLM generation, and fast compilation of small self-contained units.

## 5.8 Hot-Reload Behavior

**Decision: Parameters survive, internal state resets.** Since parameters live outside the operator in the graph's Control-layer parameter store, they are untouched by a reload. The operator's private internal state reinitializes fresh. This avoids serialize/deserialize complexity and matches creative workflows where the user is iterating on behavior.

## 5.9 Spreads: Implicit Vectorization

**Decision: Every wire in the graph implicitly carries a Spread — an ordered collection of values.** A single number is a Spread of length 1. An FFT output is a Spread of length 512. When a Spread-producing output connects to a single-value input, the operation automatically vectorizes across all elements. No explicit loop nodes are needed for the common case.

This is the single most impactful design decision for Vivid's data model. It resolves the instantiation problem that plagues every visual programming environment for creative work: "how do I make 500 particles?" In Vivid, the answer is "connect a Spread of 500 positions to a rendering operator." Where that Spread came from — a grid generator, an FFT, a MIDI controller, a Spread literal — doesn't matter. The operator processes all elements.

Precedent: vvvv's Spreads, Houdini's per-point attribute operations, and Blender Geometry Nodes' Fields all validate this pattern. The systems that handle instantiation best all converge on the same insight: the right primitive for creative work is not an object with methods but an element with attributes, operated on in parallel.

**Key properties:**

- **Broadcasting:** when two Spreads of different lengths connect to the same operator, the shorter one repeats (wraps) to match the longer. A Spread of 3 colors applied to a Spread of 512 particles cycles through the 3 colors.
- **Cross-domain:** a Spread of Control values (e.g., 512 FFT bins) can connect directly to a GPU operator's parameter, producing 512 visual elements driven by audio. No explicit bridging required — the existing Control→GPU bridge handles the data; Spreads handle the cardinality.
- **LLM-friendly:** describing Spread-based operations in natural language is natural. "Create 512 particles in a circle, sized by the FFT, colored by frequency" maps directly to a chain of operations on Spreads.
- **Port types:** Spread\<Control::Float\>, Spread\<GPU::Texture2D\>, Spread\<Audio::Mono\> are all valid. The Spread is orthogonal to the domain type system.

## 5.10 Simulation Zones: Frame-to-Frame State

**Decision: Simulation Zones provide explicit, visible frame-to-frame feedback.** A Simulation Zone is a marked region of the graph whose output at frame N becomes an additional input at frame N+1. This is the mechanism for all persistent, evolving state: particle motion, video feedback, envelope followers, accumulators, counters.

In a normal dataflow graph, everything is stateless — each frame computes from scratch. But creative behaviors need memory: a particle's position at frame 42 depends on its position at frame 41 plus its velocity. Video feedback takes the previous frame's output, transforms it, and composites it with new input. An envelope follower smooths a signal by blending with its previous value.

The Simulation Zone makes this feedback explicit and visible in the graph, unlike TouchDesigner's implicit Feedback TOP where the feedback path is invisible. Inside the zone, a special "Previous State" input carries whatever the zone output last frame. The user wires up transformations — apply forces, decay opacity, blend with new input — and the output both leaves the zone for downstream use and loops back to become next frame's Previous State.

**Domain applications:**

- **GPU — video feedback:** previous frame's texture → Blur → Displace → Composite with new input. The classic generative feedback loop, now debuggable because every step is visible.
- **GPU — particle state:** the Previous State is a Spread of particle positions/velocities/colors. Inside: apply forces, update positions, kill dead particles, spawn new ones. The Spread output is both renderable data and state for next frame.
- **Audio — envelope follower:** previous smoothed value blended with new raw value by a coefficient. Output is the smoothed value.
- **Control — accumulators:** previous count incremented on each beat event. Running totals, state machines, event counters.

**Spread-compatible:** the state inside a Simulation Zone can be a Spread. "500 particles each with their own evolving state" is a Simulation Zone operating on a Spread of 500 elements. Each element carries its own position, velocity, color, and lifetime — updated in parallel every frame.

**JSON representation:** a Simulation Zone is a node with a feedback connection from its output to a designated state input. The runtime knows to buffer the previous frame's output and provide it as input on the next frame. The exact visual representation — whether a visible bounding box around grouped nodes or a single Feedback operator with an internal graph — is a UX question to be resolved during prototyping.

## 5.11 JSON Graph Schema

The JSON graph is the single source of truth for the entire system. Every operator, connection, parameter value, and structural relationship is captured in this format. The LLM reads and writes it directly.

```json
{
  "version": "0.1.0",
  "name": "audio_reactive_particles",
  "nodes": {
    "clock1": {
      "type": "Clock",
      "domain": "control"
    },
    "fft1": {
      "type": "FFTAnalysis",
      "domain": "audio",
      "params": { "bins": 512 }
    },
    "particles1": {
      "type": "Particles",
      "domain": "gpu",
      "params": { "count": 5000, "size": 2.4 }
    }
  },
  "connections": [
    { "from": "clock1/beat", "to": "fft1/trigger" },
    { "from": "fft1/spectrum", "to": "particles1/scale" }
  ]
}
```

**Design principles:**

- **Node IDs as object keys:** fast lookup, prevents duplicates. IDs are user-readable strings ("fft1", "particles1"), not UUIDs.
- **Params carry current values only:** parameter metadata (min, max, default, semantic tags) is declared in the operator's C++ code and introspected at load time. The JSON stores only the user's current values. This keeps the JSON compact and avoids dual source-of-truth.
- **Connections are source/target pairs:** "from": "node/port" and "to": "node/port". The operator declares its ports; the JSON just names them.
- **Spread-aware:** a connection from fft1/spectrum (Spread\<float\> of 512) to particles1/scale (float) implicitly fans out. The JSON doesn't need to represent this — the runtime infers cardinality from port types.
- **Domain is metadata:** the "domain" field is informational for the UI (accent colors, preview treatment) and the runtime (thread scheduling). It does not affect how connections are expressed.

## 5.12 Platform Target

**Decision: macOS first.** Phase 1 targets macOS exclusively. This eliminates cross-platform build/test complexity and matches the primary development environment. The architecture does not paint into a corner — Zig, wgpu-native, GLFW, and miniaudio all support Linux and Windows, so cross-platform is a matter of build configuration, not redesign.

## 5.13 Windowing: GLFW

**Decision: GLFW 3.4 for window creation and input.** GLFW creates the OS window, provides the Metal surface for wgpu, and handles keyboard/mouse input events. It is minimal (~200KB source), mature, and has proven wgpu-native integration.

Alternatives considered: SDL3 provides file dialogs, pen/tablet pressure, touch input, and a structured event queue, but adds ~2MB of surface area and capabilities that are not needed for Phase 1. Raw Cocoa (NSWindow + CAMetalLayer) provides maximum control but is macOS-only with no migration path and requires Objective-C interop from Zig.

GLFW does not provide file open/save dialogs or pen/tablet pressure. File dialogs will be added via tinyfiledialogs (single-header C library) or a small Cocoa shim when save/load is implemented. Tablet pressure support is a Phase 2+ concern and can be added via platform-specific input handling without replacing the windowing library.

## 5.14 Dependency Manifest

**Decision: Seven dependencies, four of which are single-header C libraries.** Zig compiles all of them from source as part of the build. No package manager, no pre-built binaries, no Node.js or Rust in the toolchain.

| Dependency | Purpose | Size |
|---|---|---|
| **Zig** (0.13.x or 0.14.x) | Compiler toolchain, builds C/C++, build system | ~40MB |
| **wgpu-native** (latest stable) | GPU abstraction over Metal/Vulkan/DX12 | ~5MB |
| **GLFW** (3.4) | Window creation, input events, Metal surface | ~200KB source |
| **miniaudio** (0.11.x) | Audio device I/O (not DSP) | single header |
| **stb_truetype** | Font rasterization for UI text | single header |
| **yyjson** | JSON parsing (graph files, project files) | ~40KB |
| **stb_image** | Image loading (PNG, JPEG, BMP) | single header |

**Not included in Phase 1:** WebSocket library (Phase 3), HTTP/networking, UI framework (custom widgets per §6.3), tinyfiledialogs (added when save/load is implemented).

## 5.15 Project Directory Structure

**Decision: Three compilation targets with a four-level operator search path.** The build produces three distinct artifacts: the runtime (core engine, Zig), the interface (UI layer, Zig), and the operators (individual shared libraries, C++ compiled by Zig). The operator API is a C ABI contract that bridges the Zig runtime and C++ operators.

```
vivid/
├── build.zig                 # Build system entry point
├── build.zig.zon             # Zig package manifest
├── deps/                     # Third-party source (compiled by Zig)
│   ├── wgpu/  ├── glfw/  ├── miniaudio/  ├── stb/  └── yyjson/
├── src/
│   ├── runtime/              # Core engine (Zig)
│   │   ├── main.zig          # Entry point, window, main loop
│   │   ├── graph.zig         # JSON graph loading, node management
│   │   ├── scheduler.zig     # Frame scheduling, domain threads
│   │   ├── spreads.zig       # Spread type, broadcasting
│   │   ├── simulation.zig    # Simulation Zone state
│   │   ├── bridges.zig       # Control↔GPU, Control↔Audio
│   │   ├── params.zig        # Parameter store
│   │   ├── gpu_context.zig   # wgpu device, queue, surface
│   │   ├── audio_context.zig # miniaudio device, buffers
│   │   ├── hot_reload.zig    # File watch, compile, swap
│   │   └── export.zig        # Standalone build logic
│   ├── interface/            # UI layer (Zig)
│   │   ├── widgets/          # Panel, Button, Slider, Knob, etc.
│   │   ├── layout.zig        # Application layout
│   │   ├── input.zig         # GLFW event → widget events
│   │   ├── renderer.zig      # Widget → wgpu draw calls
│   │   ├── theme.zig         # Visual style (§6.6)
│   │   └── text.zig          # Text rendering (stb_truetype)
│   └── operator_api/         # C ABI contract
│       ├── operator.h        # Header operators #include
│       ├── spread.h          # Spread types for C/C++
│       └── types.h           # Shared type definitions
├── operators/                # Built-in operators (each a directory)
│   ├── gpu/
│   │   ├── noise/    { noise.cpp, noise.wgsl }
│   │   ├── blur/     { blur.cpp, blur.wgsl }
│   │   └── ...
│   ├── audio/
│   │   ├── oscillator/  { oscillator.cpp }
│   │   └── ...
│   └── control/
│       ├── lfo/      { lfo.cpp }
│       └── ...
├── projects/                 # Example projects
│   └── demo_reactive/
│       ├── graph.json
│       ├── assertions.json
│       └── operators/        # Project-local operators
└── docs/
```

Each operator is a directory containing its .cpp source and, for GPU operators, its .wgsl shader(s). This structure supports hot-reload (watch one directory per operator), scaffolding (create a directory with boilerplate), and the library system (§5.17).

**Operator search path (priority order):**

1. **Project-local** — `my_project/operators/` — operators specific to this patch
2. **User global** — `~/.vivid/operators/` — personal operators shared across projects
3. **Installed libraries** — `~/.vivid/libraries/*/operators/` — third-party packages (§5.17)
4. **Built-in** — `vivid/operators/` — ships with Vivid

When two operators share the same name, earlier in the path wins. This lets users fork a library operator into their project to customize it.

## 5.16 Export: Standalone Builds

**Decision: Export compiles the graph and its operators into a single standalone binary.** During development, operators are separate .dylib files loaded via dlopen so they can hot-reload independently. For export, those same C++ source files are compiled as static .o files and linked into one binary alongside the Zig runtime.

Zig handles this naturally — it can compile C++ and link statically in the same build step. The C ABI boundary between runtime and operators is identical whether resolved via dlopen at runtime or at link time. The graph JSON is embedded as a compile-time resource.

**Tree-shaking:** exported builds compile only the operators the graph actually references. The build system reads the graph JSON, resolves operator types to source directories via the search path, and compiles only those. A graph using three operators produces a binary containing three operators, not the entire built-in set.

The exported binary includes: the Zig runtime, wgpu-native, miniaudio, the referenced operators, and the embedded graph. It does not include: the editor interface, GLFW, hot-reload machinery, or the LLM perception system. For windowed output (e.g., a projection application), a minimal GLFW window is included; for headless output (e.g., an LED wall media server), no window is needed.

## 5.17 Operator Libraries

**Decision: Third-party operator libraries are GitHub repositories installed from source and compiled locally by Zig.** No pre-built binaries, no CI pipeline required, no platform-specific distribution. Zig IS the C/C++ toolchain, so every user who has Vivid can compile any library.

A library is a repository with a manifest and operator directories:

```
awesome-particles/
├── vivid-library.json        # Manifest
├── operators/
│   ├── gpu/
│   │   ├── fluid_sim/  { fluid_sim.cpp, fluid_sim.wgsl }
│   │   └── voronoi/    { voronoi.cpp, voronoi.wgsl }
│   └── audio/
│       └── granular/   { granular.cpp }
└── README.md
```

The manifest is minimal:

```json
{
  "name": "awesome-particles",
  "version": "0.2.0",
  "vivid": ">=0.1.0",
  "operators": ["gpu/fluid_sim", "gpu/voronoi", "audio/granular"]
}
```

**Install flow:** `vivid install github.com/user/awesome-particles` → clones to `~/.vivid/libraries/awesome-particles/` → Zig compiles all operators → they appear in the operator palette. Compilation is fast (single operators compile in under a second).

**Library template:** a template GitHub repository provides the directory structure, a starter operator with boilerplate, and the vivid-library.json manifest. Unlike the previous Vivid, no GitHub Actions are needed for CI builds — Zig compiles from source on the user's machine, eliminating the need to maintain per-platform binary distribution.

**For export:** the build system follows the same search path to find operator source files. If a graph uses fluid_sim from an installed library, the export compiles that library's source directly into the standalone binary.

**Constraints:** libraries may only depend on the Vivid operator API and standard C/C++. External C library dependencies (OpenCV, FFTW) are not managed by the library system — users who need them are responsible for making them available to the build. This keeps the package manager from becoming a general-purpose build system.
