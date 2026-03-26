# System Architecture

## 5.1 Two-Tier Interaction Model

**Fast path** — parameter adjustments and connection routing are instantaneous. No compilation, just reconfiguring the live graph. This layer is entirely declarative (parameters, topology), making it inherently LLM-friendly.

**Slow path** — editing an operator's C++ implementation. The user right-clicks a node and selects "Edit in IDE," which opens the operator's source file in their configured external editor (VS Code, CLion, etc.). On save, Vivid detects the file change, builds just that operator's shared library, and hot-swaps it into the running graph. Expected latency: 1–3 seconds. For new operators, Vivid scaffolds the boilerplate C++ with the correct base class and port declarations before opening the file.

## 5.2 Language and Toolchain

**Decision: C++ throughout.** The runtime, interface, and operators are all C++. This eliminates any ABI translation layer between the runtime and operators — they share types, headers, and conventions directly. The only boundary is the `extern "C"` interface used for `dlopen`-based hot-reload during development (see §5.8).

C++ was chosen over Zig and Rust for one overriding reason: library integration. Vivid is a creative technology tool that will integrate specialized libraries on client timelines — OpenCV, ONNX Runtime, NDI, Syphon, libtorch, CEF, and others. All of these are C or C++ libraries. In C++, integration is just "link and include." In Zig or Rust, every library requires FFI bindings, wrappers, and ongoing maintenance — friction that compounds over time.

**Build system: CMake.** The standard for C++ projects. Every library Vivid might depend on supports CMake. Dependencies are managed via git submodules or CMake FetchContent — no package manager.

**Compiler: System clang on macOS** (Xcode Command Line Tools). This is a one-time install (`xcode-select --install`) that virtually every developer on macOS already has. Cross-platform builds will use the platform's native compiler (MSVC on Windows, GCC or Clang on Linux).

**Operator compilation for hot-reload:** during development, operators are compiled by invoking the system C++ compiler as a subprocess. Vivid detects which compiler is available and invokes it directly. For future zero-friction onboarding (no system compiler required), a bundled compiler option can be added later — Zig's `zig c++` command is a single-binary C++ compiler that could serve this role without requiring Vivid's runtime to be written in Zig.

## 5.3 Three Domains

The system has three execution domains, each with distinct timing and resource characteristics:

- **Control** — floats, ints, bools, events, strings, buffers. Updated at arbitrary rates. Runs on the main thread or a dedicated control thread. No fixed timing — values propagate immediately on change.
- **Audio** — sample buffers at a fixed rate (48kHz typical). Runs on a real-time audio thread managed by miniaudio. Operators produce a buffer every callback, even if silence.
- **GPU** — textures, shaders, meshes, compute buffers. Runs at display refresh rate. Operators execute as Dawn/WebGPU render/compute passes.

## 5.4 Execution Model: Hybrid Push/Pull

Control is push-based — events propagate forward immediately. Audio and GPU are pull-based — driven by their respective hardware clocks. When a Control change reaches the boundary of an Audio or GPU subgraph, it updates the parameter store. The next pull cycle picks it up. No domain ever waits on another.

## 5.5 Domain Bridges: Control as Hub

**Decision: Control sits at the center of a star topology.** Audio and GPU never communicate directly — everything routes through Control. This simplifies the architecture from six specialized bridges to two bidirectional mechanisms:

### Control ↔ Audio
- **Control → Audio:** lock-free ring buffer or atomic. Audio callback reads at next block boundary. Latency: ~5ms at 256 samples / 48kHz. (Implemented: `ParamSnapshot` double-buffer with atomic index swap. Spread data uses `SpreadSnapshot` — a fixed-size 64-element struct copied alongside scalar params, avoiding audio-thread heap allocation.)
- **Audio → Control:** audio analysis operators write results into a lock-free queue. Control nodes poll at whatever rate they like. (Implemented: `AnalysisSnapshot` double-buffer carries RMS, peak, waveform, and per-port `SpreadSnapshot` data back to the control domain.)

### Control ↔ GPU
- **Control → GPU:** atomics or double-buffered parameter store. GPU render loop picks up changes next frame. Latency: ~16ms at 60fps.
- **GPU → Control:** async readback from GPU staging buffers. Control emits events when data lands. Latency: 1–2 frames.

**Why not direct Audio ↔ GPU?** Audio and Control both live on the CPU. The "hop" through Control is just a CPU-side buffer copy (nanoseconds), followed by the same CPU→GPU upload that would happen regardless. The only real domain boundary is CPU↔GPU, and that crossing happens exactly once no matter how the data is routed.

**Backup approach:** If six explicit per-pair bridges prove to share enough machinery during implementation, a unified port abstraction may emerge naturally from the bottom up.

## 5.6 Port Type System

The type system serves three consumers: the graph runtime (bridge selection), the UI (valid connection enforcement), and the LLM (compatibility reasoning).

Six built-in port types reflect the runtime's routing mechanisms:

- `VIVID_PORT_FLOAT` — scalar float (control values: floats, ints, bools all route identically). Updated at no fixed rate.
- `VIVID_PORT_AUDIO` — a 256-sample buffer at 48kHz. Always continuous — producing a buffer every callback, even if silence. Mono throughout; stereo is two ports (left/right).
- `VIVID_PORT_SPREAD` — variable-length float array with broadcast semantics.
- `VIVID_PORT_STRING` — UTF-8 string.
- `VIVID_PORT_STRING_SPREAD` — variable-length string array.
- `VIVID_PORT_TEXTURE` — 2D RGBA8 `WGPUTextureView` with per-node configurable resolution (default 800×600).

### Custom Port Types and the Port Type Registry

Operators can define custom port types for exchanging arbitrary typed data (GPU buffers, media streams, meshes, compute dispatches) through the graph. Custom type IDs are generated at compile time via `vivid_port_type<T>()`, which combines `vivid_type_id<T>()` (FNV-1a hash of the C++ type name via `__PRETTY_FUNCTION__`) with a high-bit marker (`| 0x80000000u`). Because the hash includes the fully-qualified type name, it produces a stable, deterministic ID across separate translation units and `dlopen` boundaries.

Each custom type has an associated `VividPortTransport` that describes how the payload crosses domain boundaries:

- `VIVID_PORT_TRANSPORT_CUSTOM_VALUE` — small structs (≤256 bytes) copied by value through a snapshot buffer.
- `VIVID_PORT_TRANSPORT_CUSTOM_REF` — opaque pointer routed via the shared handle registry. Used for large or non-copyable objects like media streams.

The `VIVID_CUSTOM_PORT` macro declares a custom port in a single line:

```cpp
VIVID_CUSTOM_PORT("media_stream", VIVID_PORT_INPUT, vivid::MediaStreamV1, VIVID_PORT_TRANSPORT_CUSTOM_REF)
```

Operator dylibs register their custom types by exporting `vivid_describe_custom_types()`, which returns a static array of `VividPortTypeInfo` records. The `VIVID_DESCRIBE_REF_TYPE(T)` convenience macro handles this for single-type `CUSTOM_REF` operators. The runtime calls this export after `dlopen` and registers each type in the global port type registry. Re-registering the same type with identical fields is idempotent; mismatched fields trigger a fatal error.

When the user draws a connection in the graph editor, the scheduler compares custom type IDs on both ends. Mismatched IDs (e.g. connecting a `MediaStreamV1` output to a `MeshBufferV1` input) are rejected — the connection is never created. This prevents silent `void*` misinterpretation.

### Semantic Tags (Advisory)
Port types can carry optional semantic tags: normalized (0–1), bipolar (-1 to 1), frequency_hz, decibels, midi_note, etc. **Tags are advisory hints, not enforced by the runtime.** When connecting ports with mismatched ranges, the graph editor suggests inserting a visible Remap node with the mapping pre-configured. No silent auto-mapping.

## 5.7 Operator API Contract

Each operator is a self-contained compilation unit — a shared library (`.dylib`) with a known interface. The runtime and operators share C++ types via common headers, but the hot-reload boundary uses `extern "C"` functions for `dlopen` stability:

```cpp
#include "operator_api/operator.h"

struct MyEffect : vivid::ControlOperatorBase {
    static constexpr const char* kName = "MyEffect";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> intensity{"intensity", 0.5f, 0.0f, 1.0f};
    vivid::Param<int>   mode{"mode", 0, {"Normal", "Inverted"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out = {&intensity, &mode};
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out = {{"input",  VIVID_PORT_FLOAT, VIVID_PORT_INPUT},
               {"output", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT}};
    }
    void process(const VividProcessContext* ctx) override {
        float in = ctx->input_values[0];
        ctx->output_values[0] = in * intensity.value;
    }
};
VIVID_REGISTER(MyEffect)
```

Three domain-specific base classes exist: `vivid::ControlOperatorBase` (implements `process(const VividProcessContext*)`), `vivid::AudioOperatorBase` (implements `process_audio(const VividAudioContext*)`), and `vivid::GpuOperatorBase` (implements `process_gpu(const VividGpuContext*)`). The `VIVID_REGISTER` macro generates `extern "C"` entry points (`vivid_abi_version`, `vivid_descriptor`, `vivid_create`, `vivid_destroy`, and domain-specific dispatch functions). It infers the operator's domain from its base class (falling back to port-type inference for `OperatorBase` subclasses), and emits a `vivid_abi_version()` function returning `VIVID_OPERATOR_ABI_VERSION` (currently 9). The runtime checks this on `dlopen` to reject stale dylibs left over from a previous build — it is not a cross-version compatibility contract. Operators always compile against the current headers.

For statically linked export builds (§5.16), the `extern "C"` boundary is unnecessary — everything links together as one C++ binary. The macro handles both cases.

The simpler this contract, the better everything downstream works: auto-generated UI knobs, confident LLM generation, and fast compilation of small self-contained units.

### 5.7.1 Composite Operators (ChildOp)

Control operators can embed other operators as persistent member variables using `ChildOp<T>` (defined in `src/operator_api/child_op.h`). This enables internal modulation chains — e.g. an LFO driving a gain stage — without exposing child operators as separate graph nodes.

**Embeddable operators** must satisfy one of two supported shapes:
- `header-only embeddable`: every concrete definition needed by `ChildOp<T>` is available from the header
- `composable-support-backed embeddable`: the operator keeps plugin-facing code in `.cpp`, and registers a `name_composable.cpp` support unit through `vivid_composable_ops` for any out-of-line destructor, virtual method implementation, thumbnail hook, or other non-inline definition needed by `ChildOp<T>` consumers

**Composable support files** are minimal embedded-use glue only. They must not contain:
- `VIVID_REGISTER(...)`
- plugin export macros such as `VIVID_THUMBNAIL(...)`
- full plugin-only thumbnail, inspector, or runtime wrapper behavior

**Current embeddables:**
- header-only: `LFO` (`control/lfo/lfo.h`)
- composable-support-backed: `Smooth` (`control/smooth/smooth.h` + `smooth_composable.cpp`), `Envelope` (`control/envelope/envelope.h` + `envelope_composable.cpp`)

**When to choose which style:**
- prefer header-only for lightweight, dependency-light modulation cores
- use composable support when the operator should still be embeddable but its plugin-facing thumbnail or other virtual behavior is better kept out-of-line

**Usage pattern:**

```cpp
#include "operator_api/child_op.h"
#include "control/lfo/lfo.h"
#include "control/smooth/smooth.h"

struct MyOp : vivid::ControlOperatorBase {
    vivid::ChildOp<LFO>    lfo_;
    vivid::ChildOp<Smooth> smoother_;

    void process(const VividProcessContext* ctx) override {
        lfo_.set_param("frequency", 2.0f);
        lfo_.process(ctx);                       // inherits time/frame from parent

        smoother_.set_input("input", lfo_.output("value"));
        smoother_.process(ctx);

        float mod = smoother_.output("value");   // use in parent logic
    }
};
```

**Key properties:**
- Each `ChildOp<T>` owns its own param, input, and output arrays — fully isolated from the parent.
- `process()` builds a child `VividProcessContext` inheriting `time`, `delta_time`, and `frame` from the parent context.
- Children maintain persistent state across frames (e.g. LFO phase), just like top-level operators.
- The parent's `collect_params` / `collect_ports` only expose the parent's params — child params are internal.

**Domain restriction:** ChildOp is for control-domain composition only. Audio operators need per-sample buffer processing on the audio thread, and GPU operators run as shader pipelines — neither maps to the `ChildOp` call-and-read pattern.

#### Embedded Operator Slots

Hosts that use `ChildOp<T>` internally can declare **embedded operator slots** — named modulation roles with curated allowed types. This metadata is declared via `collect_embedded_op_slots()` and stored in the operator descriptor as `VividEmbeddedOpSlot` records (defined in `src/operator_api/types.h`):

```cpp
VividEmbeddedOpSlot{
    .role_id       = "envelope",       // semantic name
    .default_type  = "Envelope",       // default operator type
    .param_prefix  = "envelope_"       // namespace for flattened host params
};
```

The **curated slot model** restricts each slot to a small approved set of operator types (e.g., an envelope slot allows `Envelope` or `MSEG`, not arbitrary control operators). This keeps UX simple and avoids rebuilding the complexity of the earlier role-bindings experiment.

**Slot editing is inspector-first:** embedded slots appear as cards in the host's inspector panel with inline param editing. Embedded operators are never exposed as separate graph nodes — they remain owned host-local state.

All 7 GPU operators (Particles, InstancedShapes, Flocking, Trails, Fluid, ReactionDiffusion, CellularAutomata) use this pattern for internal modulation — typically LFO or Envelope instances driven by `<role>_`-prefixed host params.

The architecture uses three mechanisms for three purposes (see also `ARCHITECTURE-GUARDRAILS.md`):
- **Ports** — generic transport between graph nodes
- **Embedded slots** — owned host-local composition using reusable operator code
- **Explicit outputs** — sharing host-local results back into the graph

### 5.7.2 Audio DSP Utilities (Public Operator API)

`src/operator_api/audio_dsp.h` is part of the public operator API for package and project operators.

The following utilities are stable and documented for external use:

- `audio_dsp::WhiteNoise`
- `audio_dsp::PinkNoise`
- `audio_dsp::detect_trigger(float phase, float prev_phase)`
- `audio_dsp::waveform(double phase, int type)`

Semantics:

- `WhiteNoise::next()` returns `[-1, 1]`
- `WhiteNoise::next_unipolar()` returns `[0, 1]`
- `PinkNoise::next()` returns a bounded pink-noise sample (Voss-McCartney)
- `detect_trigger` is phase-wrap detection (`delta < -0.5`)
- `waveform` supports `type`: `0=sine`, `1=saw`, `2=square`, `3=triangle`

Compatibility is enforced by `tests/test_audio_dsp_api.cpp`.

**Canonical example:** `operators/control/modulated_gain/modulated_gain.cpp` — LFO → Smooth → gain modulation.

## 5.8 Hot-Reload Behavior

**Decision: Parameters survive, internal state resets.** Since parameters live outside the operator in the graph's Control-layer parameter store, they are untouched by a reload. The operator's private internal state reinitializes fresh. This avoids serialize/deserialize complexity and matches creative workflows where the user is iterating on behavior.

Hot-reload flow: file system watcher (kqueue on macOS) detects operator source change → invoke system C++ compiler to build `.dylib` → `dlclose` old library → `dlopen` new library → call `vivid_create` with existing parameter values → operator resumes with new behavior, old parameter state intact.

## 5.9 Spreads: Implicit Vectorization

**Decision: Every wire in the graph implicitly carries a Spread — an ordered collection of values.** A single number is a Spread of length 1. An FFT output is a Spread of length 512. When a Spread-producing output connects to a single-value input, the operation automatically vectorizes across all elements. No explicit loop nodes are needed for the common case.

This is the single most impactful design decision for Vivid's data model. It resolves the instantiation problem that plagues every visual programming environment for creative work: "how do I make 500 particles?" In Vivid, the answer is "connect a Spread of 500 positions to a rendering operator." Where that Spread came from — a grid generator, an FFT, a MIDI controller, a Spread literal — doesn't matter. The operator processes all elements.

Precedent: vvvv's Spreads, Houdini's per-point attribute operations, and Blender Geometry Nodes' Fields all validate this pattern. The systems that handle instantiation best all converge on the same insight: the right primitive for creative work is not an object with methods but an element with attributes, operated on in parallel.

**Key properties:**

- **Broadcasting:** when two Spreads of different lengths connect to the same operator, the shorter one repeats (wraps) to match the longer. A Spread of 3 colors applied to a Spread of 512 particles cycles through the 3 colors.
- **Cross-domain:** a Spread of Control values (e.g., 512 FFT bins) can connect directly to a GPU operator's parameter, producing 512 visual elements driven by audio. No explicit bridging required — the existing Control→GPU bridge handles the data; Spreads handle the cardinality.
- **LLM-friendly:** describing Spread-based operations in natural language is natural. "Create 512 particles in a circle, sized by the FFT, colored by frequency" maps directly to a chain of operations on Spreads.
- **Port types:** `VIVID_PORT_SPREAD` is a dedicated port type for variable-length float arrays. Texture and audio ports don't have a spread variant — multiple instances use multiple ports. String arrays use the separate `VIVID_PORT_STRING_SPREAD` type.
- **Cross-domain bridge implementation:** Control↔Audio uses `SpreadSnapshot` (fixed 64-element struct) inside the double-buffered `ParamSnapshot`/`AnalysisSnapshot` bridges — no heap allocation on the audio thread. Control→GPU uses WebGPU storage buffers: the operator uploads Spread data via `wgpuQueueWriteBuffer` into a `ReadOnlyStorage` binding that the fragment shader reads as `array<f32>`.

## 5.10 Simulation Zones: Frame-to-Frame State

> **Status: Deferred past 1.0.** The design below is retained as planned architecture. In the current implementation, GPU video feedback is handled by the `operators/gpu/feedback/` operator, which maintains its own previous-frame texture buffer internally. General-purpose Simulation Zones are not yet implemented.

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
  "schema_version": 1,
  "vivid_version": "0.1.0",
  "meta": {
    "id": "audio_reactive_demo",
    "title": "Audio Reactive Demo",
    "description": "FFT-driven visual effects.",
    "tags": ["audio", "reactive"],
    "difficulty": "intermediate",
    "domains": ["gpu", "audio"]
  },
  "nodes": {
    "clock1": {
      "type": "Clock",
      "params": { "bpm": 120.0 },
      "layout": { "x": 30.0, "y": 200.0 }
    },
    "fft1": {
      "type": "FFTAnalysis",
      "params": {}
    },
    "noise1": {
      "type": "Noise",
      "params": { "speed": 1.0 },
      "layout": { "x": 430.0, "y": 200.0 }
    },
    "vout": {
      "type": "video_out"
    }
  },
  "connections": [
    { "from": "clock1/beat_phase", "to": "fft1/trigger" },
    {
      "from": "fft1/rms",
      "to": "noise1/scale",
      "from_min": 0.0, "from_max": 1.0,
      "to_min": 1.0, "to_max": 8.0,
      "clamp": true
    },
    { "from": "noise1/texture", "to": "vout/input" }
  ],
  "viewport": { "pan_x": 0.0, "pan_y": 0.0, "zoom": 1.0 }
}
```

**Design principles:**

- **Node IDs as object keys:** fast lookup, prevents duplicates. IDs are user-readable strings ("fft1", "particles1"), not UUIDs.
- **Params carry current values only:** parameter metadata (min, max, default, semantic tags) is declared in the operator's C++ code and introspected at load time. The JSON stores only the user's current values. This keeps the JSON compact and avoids dual source-of-truth.
- **Connections are source/target pairs:** "from": "node/port" and "to": "node/port". The operator declares its ports; the JSON just names them.
- **Spread-aware:** a connection from fft1/spectrum (Spread\<float\> of 512) to particles1/scale (float) implicitly fans out. The JSON doesn't need to represent this — the runtime infers cardinality from port types.
- **No per-node domain field:** domain is inferred from port types and base class at load time, not stored in the JSON. The `meta.domains` array at the graph level is informational only (for catalog/search).
- **Layout is optional:** each node can carry `"layout": {"x": ..., "y": ...}` for node positions. Nodes without layout use auto-placement.
- **Connection remapping:** connections can carry `from_min`/`from_max`/`to_min`/`to_max`/`clamp` fields for inline value rescaling. Connections without these fields pass values through unchanged.
- **Package provenance:** nodes from installed packages carry `"pkg": {"name": "...", "version": "..."}` for version mismatch diagnostics at load time. Core operators omit this.
- **Parameter lock flags:** nodes can carry `"param_lock_flags": {"param_name": flags}` to protect individual parameters from variation recall (`PARAM_LOCK_PRESETS`) or wire-driven changes (`PARAM_LOCK_WIRES`).

## 5.12 Platform Target

**Decision: macOS first.** Phase 1 targets macOS exclusively. This eliminates cross-platform build/test complexity and matches the primary development environment. The architecture does not paint into a corner — Dawn, GLFW, and miniaudio all support Linux and Windows, so cross-platform is a matter of build configuration, not redesign.

## 5.13 Windowing: GLFW

**Decision: GLFW 3.4 for window creation and input.** GLFW creates the OS window, provides the Metal surface for Dawn, and handles keyboard/mouse input events. It is minimal (~200KB source), mature, and has proven WebGPU integration.

Alternatives considered: SDL3 provides file dialogs, pen/tablet pressure, touch input, and a structured event queue, but adds ~2MB of surface area and capabilities that are not needed for Phase 1. Raw Cocoa (NSWindow + CAMetalLayer) provides maximum control but is macOS-only with no migration path.

GLFW does not provide file open/save dialogs or pen/tablet pressure. File dialogs will be added via tinyfiledialogs (single-header C library) or a small Cocoa shim when save/load is implemented. Tablet pressure support is a Phase 2+ concern and can be added via platform-specific input handling without replacing the windowing library.

## 5.14 Dependency Manifest

**Decision: Seven dependencies, most of which are small C libraries.** CMake manages the build. No external package manager required.

| Dependency | Purpose | Size | Integration |
|---|---|---|---|
| **wgpu-native** (pinned fork) | GPU abstraction (WebGPU over Metal) | ~17MB binary | FetchContent (Rust/Cargo build) |
| **GLFW** (3.4) | Window creation, input events, Metal surface | ~200KB source | git submodule |
| **glfw3webgpu** | GLFW↔WebGPU surface bridge | ~5KB source | git submodule |
| **miniaudio** (0.11.x) | Audio device I/O (not DSP) | single header | vendored |
| **stb_truetype** | Font rasterization for UI text | single header | vendored |
| **stb_image** | Image loading (PNG, JPEG, BMP) | single header | vendored |
| **nlohmann/json** | JSON parsing and serialization (graph files, project files) | header-only | FetchContent |
| **RtMidi** | MIDI I/O (CoreMIDI on macOS) | ~50KB source | vendored |
| **oscpack** | OSC message serialization and UDP transport | ~30KB source | vendored |
| **Syphon** | GPU texture sharing between applications (macOS) | ~100KB source | vendored |
| **Snappy** | Fast compression (used by HAP video codec) | ~50KB source | FetchContent |
| **IXWebSocket** | HTTP server for the runtime control server endpoint | ~200KB source | FetchContent |
| **CLI11** | Command-line argument parsing | header-only | FetchContent |
| **Sparkle** (macOS) | App auto-update framework | framework | system framework |

**Note on Dawn:** The original plan called for Google's Dawn WebGPU implementation. The actual integration uses wgpu-native (a Rust-based WebGPU backend) via eliemichel's WebGPU-distribution adapter layer, with a pinned upstream commit (`gfx-rs/wgpu-native`) providing Metal interop symbols for Syphon texture sharing.

**Compiler requirement:** Xcode Command Line Tools on macOS (`xcode-select --install`). Provides clang, libc++, and Metal framework headers.

## 5.15 Project Directory Structure

**Decision: Single C++ codebase with a four-level operator search path.** The runtime and operators are all C++. Operators compile as individual shared libraries for hot-reload during development.

```
vivid/
├── CMakeLists.txt              # Top-level build
├── deps/                       # Third-party (submodules and vendored)
│   ├── glfw/  ├── glfw3webgpu/  ├── miniaudio/  ├── stb/
│   ├── rtmidi/  ├── oscpack/  ├── syphon/  └── hap/
├── src/
│   ├── runtime/                # Core engine
│   │   ├── main.cpp            # Entry point, window, main loop
│   │   ├── graph.cpp/.h        # JSON graph loading, node management, serialization
│   │   ├── scheduler.cpp/.h    # Frame scheduling, domain dispatch, wire resolution
│   │   ├── audio_engine.cpp/.h # miniaudio device, audio callback, ParamSnapshot bridge
│   │   ├── gpu_context.cpp/.h  # WebGPU device, queue, surface
│   │   ├── hot_reload.cpp/.h   # File watch, compile, dlclose/dlopen swap
│   │   ├── file_watcher.cpp/.h # kqueue-based file system monitoring
│   │   ├── operator_registry.cpp/.h   # Operator type registry, WGSL preset scanning
│   │   ├── operator_loader.cpp/.h     # dlopen/dlclose, ABI version checking
│   │   ├── operator_creator.cpp/.h    # Scaffold + compile new operators
│   │   ├── control_server.cpp/.h      # HTTP/MCP endpoint (OSC, MIDI, REST, MCP tools)
│   │   ├── runtime_api.cpp/.h  # Internal API surface for MCP and chat
│   │   ├── package_manager.cpp/.h     # Install, link, unlink, rebuild packages
│   │   ├── package_compiler.cpp/.h    # Per-operator .dylib compilation
│   │   ├── package_scaffolder.cpp/.h  # Package template generation
│   │   ├── package_catalog.cpp/.h     # Catalog index and discovery
│   │   ├── undo_manager.cpp/.h # Graph mutation undo/redo
│   │   ├── settings.cpp/.h     # User preferences persistence
│   │   ├── system_midi.cpp/.h  # RtMidi wrapper, MIDI device enumeration
│   │   ├── crash_guard.h       # Per-operator crash isolation
│   │   └── ...                 # (+ metal_interop, syphon_output, av_exporter, etc.)
│   ├── ui/                     # UI layer
│   │   ├── node_graph.cpp/.h   # Node graph editor (draw, input, layout)
│   │   ├── renderer_2d.cpp/.h  # WebGPU 2D drawing primitives
│   │   ├── theme_loader.cpp/.h # JSON theme loading, embedded defaults
│   │   ├── ui_style.cpp/.h     # Visual style constants and runtime style
│   │   ├── thumbnail_renderer.cpp/.h  # Zero-copy GPU node thumbnails
│   │   ├── overlay_layouts.cpp/.h     # Inspector, transport, overlays
│   │   └── file_dialog.mm/.h   # Native macOS file open/save dialogs
│   ├── operator_api/           # Public headers for operator contract
│   │   ├── operator.h          # Base classes, Param<T>, VIVID_REGISTER macro
│   │   ├── types.h             # C ABI: enums, descriptors, contexts
│   │   ├── audio_operator.h    # AudioOperatorBase, VividAudioContext
│   │   ├── gpu_operator.h      # GpuOperatorBase, VividGpuContext, VividGpuState
│   │   ├── gpu_types.h         # VividGpuBuffer, VividMesh, VividComputeBuffer
│   │   ├── child_op.h          # ChildOp<T> for control-domain composition
│   │   ├── wgsl_filter.h       # WgslFilterBase for data-driven GPU filters
│   │   ├── data_driven_filter.h # DataDrivenFilter with dynamic param/port collection
│   │   ├── audio_dsp.h         # WhiteNoise, PinkNoise, waveform(), detect_trigger()
│   │   ├── media_stream.h      # MediaStreamV1 cross-domain media type
│   │   ├── media_clock.h       # MediaClockV1 synchronization type
│   │   └── midi_types.h        # VividMidiBuffer type (reserved)
│   ├── cli/                    # CLI tooling
│   │   └── mcp_server.cpp      # MCP tool handlers (~4000 lines)
│   └── export/                 # Standalone export build
│       └── standalone_main.cpp
├── operators/                  # Built-in operators (each a directory)
│   ├── gpu/                    # noise, shape, text, bloom, composite, feedback,
│   │                           # movie_loaded, movie_video_out, webcam_in,
│   │                           # syphon_in, syphon_out, texture_analysis, time_machine, ...
│   ├── audio/                  # oscillator, gain, delay, reverb, distortion, bitcrush,
│   │                           # spread_adsr, spread_lfo, movie_audio_out, ...
│   └── control/                # lfo, clock, envelope, math, random, smooth, gate,
│                               # keyboard, mouse, midi_input, osc_in, osc_out,
│                               # fft_analysis, stack, alternate, step_counter,
│                               # folder_list, string_select, logic, ...
├── filters/                    # Data-driven WGSL filters (auto-discovered, no C++)
├── graphs/                     # Demo graphs organized by category
│   ├── intro/   ├── filters/   ├── gpu/   ├── audio/   └── io/
├── tests/                      # CTest suite
├── mcp/                        # Python MCP server (vivid_mcp.py)
├── site/                       # Website & package catalog
├── fonts/                      # Bundled fonts
├── assets/                     # Demo assets (videos, images)
├── platform/                   # Platform-specific resources (Info.plist, icons)
├── scripts/                    # Build and utility scripts
└── docs/                       # Documentation (PRD, ARCHITECTURE, ROADMAP)
```

Each operator is a directory containing its .cpp source and, for GPU operators, its .wgsl shader(s). This structure supports hot-reload (watch one directory per operator), scaffolding (create a directory with boilerplate), and the library system (§5.17).

**Operator search path (priority order):**

1. **Project-local** — `my_project/operators/` — operators specific to this patch
2. **User global** — `<config_dir>/operators/` — personal operators shared across projects
3. **Installed libraries** — `<config_dir>/packages/*/operators/` — third-party packages (§5.17)
4. **Built-in** — `vivid/operators/` — ships with Vivid

When two operators share the same name, earlier in the path wins. This lets users fork a library operator into their project to customize it.

## 5.16 Export: Standalone Builds

**Decision: Export compiles the graph and its operators into a single standalone binary.** During development, operators are separate .dylib files loaded via dlopen so they can hot-reload independently. For export, those same C++ source files are compiled and statically linked into one binary.

CMake handles this with a separate build target that compiles operators as static libraries instead of shared libraries and links everything together. The `extern "C"` functions from `VIVID_REGISTER` are resolved at link time instead of via `dlopen`.

**Tree-shaking:** exported builds compile only the operators the graph actually references. The build reads the graph JSON, resolves operator types to source directories via the search path, and generates a CMake target containing only those operators. A graph using three operators produces a binary containing three operators, not the entire built-in set.

The graph JSON is embedded as a compile-time resource (e.g., via `xxd` or CMake's file embedding).

The exported binary includes: the runtime, Dawn, miniaudio, the referenced operators, and the embedded graph. It does not include: the editor interface, GLFW (for headless), hot-reload machinery, or the LLM perception system. For windowed output (e.g., a projection application), a minimal GLFW window is included; for headless output (e.g., an LED wall media server), no window is needed.

## 5.17 Operator Libraries

**Decision: Third-party operator libraries are GitHub repositories installed from source and compiled locally.** No pre-built binaries, no CI pipeline required, no platform-specific distribution.

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

**Install flow:** `vivid install github.com/user/awesome-particles` → clones to `<config_dir>/packages/awesome-particles/` (macOS: `~/Library/Application Support/Vivid/packages/awesome-particles/`) → compiles all operators to .dylib → they appear in the operator palette. Compilation is fast (single operators compile in under a second with clang).

**Development workflow:** `vivid link /path/to/my-package` creates a symlink from the packages directory to the developer's source tree instead of copying. Operators are compiled in-place — the `build/` directory lives inside the original source. After editing operator source, `vivid rebuild my-package` recompiles without re-copying. `vivid unlink my-package` removes the symlink without touching the source. This mirrors the `npm link` workflow: link once during development, rebuild after changes, unlink when done.

**Library template:** a template GitHub repository provides the directory structure, a starter operator with boilerplate, and the vivid-library.json manifest. No GitHub Actions needed for CI builds — operators compile from source on the user's machine.

**For export:** the build system follows the same search path to find operator source files. If a graph uses fluid_sim from an installed library, the export compiles that library's source directly into the standalone binary.

**Constraints:** libraries may only depend on the Vivid operator API and standard C/C++. External library dependencies (OpenCV, FFTW) are not managed by the library system — users who need them are responsible for making them available to the build. This keeps the package manager from becoming a general-purpose build system.

## 5.18 Data-Driven WGSL Filter Framework

**Decision: GPU image filters are self-describing `.wgsl` files with embedded JSON metadata, loaded by a generic runtime. No per-filter C++ code is required.**

A filter is a single `.wgsl` file in the `filters/` directory. A JSON comment block at the top declares its name, parameters, and input ports:

```wgsl
/*{
  "name": "Blur",
  "params": [
    {"name": "radius",  "default": 5.0, "min": 0.0, "max": 50.0},
    {"name": "quality", "default": 4.0, "min": 1.0, "max": 16.0}
  ]
}*/
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // shader uses u.radius, u.quality from auto-generated uniform struct
}
```

**Runtime architecture:**

1. **`WgslHeaderParser`** (`src/runtime/wgsl_header_parser.cpp`) extracts the `/*{...}*/` block, parses it with nlohmann/json, and returns a `WgslHeader` struct plus the clean fragment source with the comment stripped.

2. **`WgslFilterBase`** (`src/operator_api/wgsl_filter.h`) is the generic GPU operator base class. On first process, it reads the `.wgsl` file, generates a WGSL preamble containing a fullscreen-triangle vertex shader, a `Uniforms` struct built from the parsed params, bind group layouts, and a sampler — then compiles the preamble + fragment source into a single WebGPU shader module and pipeline. It hot-reloads on file change (checked every 30 frames by mtime).

3. **`DataDrivenFilter`** (`src/operator_api/data_driven_filter.h`) wraps `WgslFilterBase` with dynamic param and port collection driven by a `DataDrivenFilterConfig` struct. A single C++ class serves all WGSL filter presets.

4. **`OperatorRegistry::scan_wgsl_presets()`** (`src/runtime/operator_registry.cpp`) scans the `filters/` directory at startup, parses each `.wgsl` header, and registers a `DataDrivenFilterConfig` per file. The registry maps filter names to configs; instantiation creates a `DataDrivenFilter` initialized with the appropriate config.

**Param metadata** supports type (float/int/bool), min/max/default, enum choices, display hints (`"knob"`, `"xy_pad"`, `"color"`), groups, and column layout — all declared in JSON, all consumed by the inspector UI without per-filter code.

**Why this matters:** Adding a new GPU filter means writing one `.wgsl` file. No C++ boilerplate, no CMake changes, no registration macro. The file is auto-discovered, hot-reloadable, and its parameters appear in the inspector automatically. This is the path for both built-in filters and user-authored ones.

## 5.19 State Machines & Subgraphs

> **Status: Infrastructure exists, operator not yet implemented.** The graph schema supports state-preset mappings (`StatePresetMapping` in `graph.h`), and the UI can detect StateMachine nodes for preset-per-state wiring. However, the StateMachine operator itself has not been implemented. The design below is retained as planned architecture.

### StateMachine Operator

The `StateMachine` is a control-domain metadata emitter that drives macro-level structure — song sections, installation modes, live performance cues. It counts bars by detecting `beat_phase` wraps (the same technique used by NotePattern, ChordProgression, and Arpeggiator), tracks the current state index, and outputs control signals. It is not a state owner — it emits metadata that downstream operators consume.

**Outputs:** `state` (current index 0–7), `progress` (0–1 through current state), `trigger` (fires on transition frame), `bar` (bar count within state), `beat` (phase within current bar).

**Transition modes:** sequential (bar-duration-based auto-advance), manual (rising-edge trigger input), threshold (signal crossing). All modes support beat-quantized transitions — a pending advance defers to the next bar boundary. A per-state duration of 0 means "hold until manually triggered," allowing mixed timed and manual states.

**Integration patterns (without subgraphs):**

- **State → Sequencer:** state index scaled to phase drives a Sequencer, selecting per-state parameter values.
- **Progress → crossfade:** progress output drives Gain levels for fade-in/fade-out or dual-source crossfades.
- **Trigger → Envelope:** transition trigger gates one-shot envelopes for percussive hits at section boundaries.
- **Threshold mode:** sensor input drives state changes for installation scenarios; Logic operators can combine multiple conditions.

### Subgraph Vision

The long-term goal is that each state owns a subgraph — a self-contained patch fragment that activates on entry and deactivates on exit. A song's "chorus" state would contain its own NotePattern, DrumSequencer, and GPU operators, all wired independently from the "verse" subgraph.

**Infrastructure needed:**

1. **Subgraph container** — embedding a group of nodes inside a parent node, with parent-controlled lifecycle.
2. **Activation/deactivation** — active subgraphs process; inactive subgraphs stop (and optionally reset).
3. **Cross-graph routing** — parent inputs flow into the active subgraph; subgraph outputs flow out. Inactive subgraphs produce silence/zero/last-value.
4. **Crossfade transitions** — during a transition window, both outgoing and incoming subgraphs process simultaneously with blended outputs.
5. **Session serialization** — subgraph contents must save/load with the session.

**How the current operator accommodates subgraphs:** The state index output, progress output, and trigger output are designed so that no changes to the StateMachine operator itself will be needed when subgraphs are implemented — only the runtime infrastructure around it. State index drives activation, progress drives the blend factor, trigger fires activation/deactivation, and bar-based durations map directly to "run this subgraph for N bars."

## 5.20 Variation & Session System

**Decision: Variations are complete parameter snapshots with beat-quantized recall.** A `VariationDef` captures the delta between current parameter values and their defaults across every node in the graph. Recalling a variation restores that state — subject to parameter lock flags that protect individual parameters from being overwritten.

**Core data structure:**

```cpp
struct VariationDef {
    std::string name;
    std::unordered_map<std::string,
        std::unordered_map<std::string, float>> params;        // node_id → param → value
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::string>> string_params;
};
```

Only non-default values are stored (delta encoding), keeping snapshots compact.

**Beat-quantized switching:** A designated Clock node's `beat_phase` output drives quantized recall. When a variation is queued with a quantize mode (instant, beat, bar, four-bar), a `PendingVariation` struct counts beat-phase zero-crossings until the target boundary, then applies. This ensures variation switches are always musically aligned.

**Parameter lock flags:** Each parameter carries a `ParamLockFlags` bitmask. `PARAM_LOCK_PRESETS` protects a parameter from variation recall — useful for keeping a specific knob position while switching everything else. `PARAM_LOCK_WIRES` protects from wire-driven changes.

**Apply process (two-phase):** First, all unlocked parameters reset to defaults. Then, the variation's stored deltas are applied. This ensures clean transitions — parameters not captured in the variation return to their defaults rather than retaining stale values from a previous variation.

**Dirty tracking:** A `variation_dirty_` flag indicates when live parameter edits have diverged from the active variation, allowing the UI to show which variation is "out of sync."

This system is the core experimentation mechanism: save what works, explore freely, recall instantly on the beat.

## 5.21 Pattern Algebra

> **Status: Implemented.** All pattern algebra operators are built-in core operators.

**Decision: Patterns are standard control-domain operators with Spread ports, not a DSL.** Composition happens through normal graph wiring. The Spread type system (§5.9) provides implicit vectorization — a pattern transformer operates on all elements transparently.

**Three operator roles:**

- **Generators** produce Spread outputs from parameters or time inputs: `Euclidean` (Bjorklund rhythm patterns), `PatternSeq` (16-step sequences), `NotePattern` (per-step chord specifications), `ChordProgression` (diatonic chords from scale degrees).
- **Transformers** take a Spread input and produce a transformed Spread output: `PatTransform` applies reverse, rotate, scale, offset, and probabilistic element nulling in a fixed chain. Each step is a parameter — no control flow.
- **Combinators** merge multiple Spread inputs: `Stack` (concatenate or interleave up to 4 Spread inputs), `Alternate` (time-driven selection between Spread inputs on beat/bar boundaries).

**Composable chains:**

```
Euclidean → PatTransform → Stack → Arpeggiator
(generate)   (rotate+scale)  (combine w/ melody)  (consume as note sequence)
```

Every intermediate result is a Spread visible on a wire. Every step is a discrete operator with inspectable parameters. The LLM can reason about pattern composition using the same vocabulary it uses for any other graph operation.

**Why not a DSL:** A pattern language would require its own parser, type system, and error reporting — and would be opaque to the graph editor and LLM. By making patterns ordinary operators, they inherit Spread broadcasting, cross-domain bridging, serialization, hot-reload, ChildOp embedding, and inspector UI for free. The cost is verbosity (a chain of 4 operators vs. a one-line expression), but the graph editor makes this visual, not textual.

## 5.22 MCP / LLM Integration

Vivid exposes its full runtime API through two integration surfaces:

**Control Server** (`src/runtime/control_server.cpp`) — an HTTP server (powered by IXWebSocket) running inside the Vivid runtime on `127.0.0.1:9876` by default. It handles OSC messages, MIDI input, and the HTTP JSON-RPC runtime endpoint. Graph inspection and mutation, operator scaffolding and compilation, parameter read/write, capture, analysis, package management, and undo/redo all flow through this surface.

**Python MCP Server** (`mcp/vivid_mcp.py`) — a Python wrapper that connects to the control server and re-exposes its tools as a standard MCP stdio server. This allows any MCP client (Claude Desktop, custom agents) to interact with a running Vivid instance without direct HTTP calls. The running Vivid app owns the live graph and the HTTP port; the Python process owns the MCP stdio layer.

**Runtime API** (`src/runtime/runtime_api.cpp/.h`) — the internal C++ API that both the control server and the built-in chat interface call into. All graph mutations, operator creation, capture, and analysis operations are implemented here. The Runtime API operates on the same in-process data structures as the scheduler and graph — no serialization overhead.

## 5.23 Media Pipeline

Vivid's media pipeline handles video file playback with synchronized audio through a trio of operators:

- **`MovieLoaded`** (`operators/gpu/movie_loaded/`) — decodes video frames from a file using AVFoundation (macOS). Supports HAP (via Snappy decompression + BC GPU upload), HAPQ (YCoCg color space), HAP-alpha, H.264, and HEVC codecs. Outputs a `VIVID_PORT_TEXTURE` (decoded frames) and a custom `CUSTOM_REF` port carrying `MediaStreamV1` for audio sync. Playback modes include loop, ping-pong, and one-shot. Frame decoding runs on a dedicated dispatch queue; the GPU operator receives textures via a double-buffered handoff.

- **`MovieAudioOut`** (`operators/audio/movie_audio_out/`) — receives a `CUSTOM_REF` media stream input from MovieLoaded and outputs decoded audio as `VIVID_PORT_AUDIO` left/right channels. Audio decoding uses AVFoundation's `AVAssetReader` with a lock-free ring buffer bridging the decode thread to the real-time audio callback. Sync is maintained via media clock timestamps — the audio operator tracks the video operator's playback position and resyncs on loop boundaries.

- **`MovieVideoOut`** (`operators/gpu/movie_video_out/`) — the reverse path: encodes GPU textures + audio buffers into a video file via `AVAssetWriter`. Used for recording/export.

The media stream type carries a `MediaStreamV1*` pointer through the graph's custom port (`CUSTOM_REF` transport) plumbing, enabling cross-domain media synchronization without special-case runtime code.
