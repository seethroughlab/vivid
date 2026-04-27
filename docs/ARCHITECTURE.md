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

## 5.3 Operator Families

Vivid operators fall into three families based on the kind of data they work with and where they appear in the UI. These are distinct from the runtime's two **cadences** (frame-rate ~60 Hz, audio-rate ~48 kHz), which determine *when* an operator executes. See the [runtime architecture](vivid-runtime-architecture.md) for the cadence model.

- **Control** — floats, ints, bools, events, strings, lane arrays. Most control operators run at audio cadence; frame-rate consumers (GPU shaders, UI) read their outputs through `AudioFrameBridge` transparently. A handful of pure-frame control operators still exist where audio-rate execution buys nothing (e.g. mouse, keyboard).
- **Audio** — sample buffers at audio cadence (48 kHz, configurable 128/256/512/1024-sample buffers). Runs on a real-time audio thread managed by miniaudio. Operators produce a buffer every callback, even if silence. Sample rate remains fixed at `48000`; buffer size is a persisted app preference that triggers a runtime rebuild when changed.
- **GPU** — textures, shaders, meshes, compute buffers. Runs at frame cadence on the main thread. Operators execute as Dawn/WebGPU render/compute passes.

## 5.4 Execution Model: Dual-Cadence Pull

Both cadences are pull-based — frame-rate is driven by the display refresh, audio-rate by the audio device callback. Frame and audio executors process their respective nodes in topological order each tick/buffer. Operators are single-cadence — they implement either `process_frame` or `process_audio`, not both. Cross-cadence data flows through `AudioFrameBridge` using lock-free double-buffered snapshots, so neither cadence ever waits on the other. Cross-cadence edges require an explicit `"bridge": true` field in the graph JSON.

### Graph Compilation (7-Pass Pipeline)

The `GraphCompiler` transforms a `Graph` (pure data model) into a `CompiledGraph` (live execution state). Topology changes trigger a full recompile — the `CompiledGraph` is never mutated during execution. The pipeline runs 7 passes:

| Pass | Name | Purpose |
|------|------|---------|
| 1 | Create CompiledNodes | Instantiate operators via `dlopen`, determine each node's cadence (frame or audio) |
| 2 | Resolve edges | Map JSON connections to `CompiledEdge` structs with resolved port indices |
| 2.6 | Lane-set propagation | Walk nodes in topological order, propagating lane-set provenance through edges (see §5.9). Pointwise nodes inherit their input lane set; Structural nodes mint a fresh `lane_set_id`; Reduction nodes emit scalar. Mismatched non-scalar inputs on a Pointwise node are a compile error. |
| 3 | Topological sort | Produce `frame_order` and `audio_order` execution sequences |
| 4 | Audio channel negotiation | Resolve channel counts: explicit declarations → propagated from upstream → planner fallback. Includes sub-passes 4c/4d for lane execution strategy planning (see §5.9.6). |
| 5 | Audio buffer allocation | Pre-allocate per-node planar audio buffers sized to the negotiated channel count |
| 6 | Partition edges | Classify each edge as frame-Direct, audio-Direct, or Snapshot (cross-cadence via `AudioFrameBridge`) |
| 7 | Finalize | Collect errors, emit diagnostics |

Source: `src/runtime/graph/graph_compiler.cpp`, with planning in `graph_compiler_planning.cpp`.

### Graph Metronome

Each graph carries a metronome (`GraphMetronomeDef`: `bpm`, `beats_per_bar`). The runtime maintains a `LiveMetronomeState` with an anchor timestamp, computing `beat_phase` (0–1 sawtooth per beat) and `bar_phase` (0–1 per bar) each frame. These are passed to operators via the frame/audio context, enabling beat-quantized recall, clock-synced LFOs, and sequencer timing without an explicit Clock node. Operators choose whether to sync to the metronome or free-run independently.

## 5.5 Cadence Bridges

**Decision: Cross-cadence data flows through two bidirectional bridges.** Audio and GPU never communicate directly — everything routes through the frame-rate side. This simplifies the architecture to two boundary mechanisms:

### Frame ↔ Audio (`AudioFrameBridge`)
- **Frame → Audio:** `ParamSnapshot` double-buffer with atomic index swap. Lane-bearing data uses `BridgeLaneSlot` — pre-allocated flat buffers wired during graph build, with capacity up to `kDefaultLaneCapacity` (1024). The audio thread reads lane data directly from the bridge slot (zero-copy). Latency: ~5ms at 256 samples / 48kHz.
- **Audio → Frame:** `AnalysisSnapshot` double-buffer carries RMS, peak, waveform, and per-port `BridgeLaneSlot` data back to the frame side.

### CPU ↔ GPU
- **CPU → GPU:** parameter store updated per frame. GPU operators upload lane data via `wgpuQueueWriteBuffer` into storage buffers. Latency: ~16ms at 60fps.
- **GPU → CPU:** async readback from GPU staging buffers. Frame-rate nodes receive data when it lands. Latency: 1–2 frames.

**Why not direct Audio ↔ GPU?** Audio and frame-rate operators both live on the CPU. The "hop" through the frame side is just a CPU-side buffer copy (nanoseconds), followed by the same CPU→GPU upload that would happen regardless. The only real boundary is CPU↔GPU, and that crossing happens exactly once no matter how the data is routed.

### Bridge Capacity Limits

- `kDefaultLaneCapacity = 1024` — lane data crossing the `AudioFrameBridge` is written into pre-allocated `BridgeLaneSlot` buffers (capacity set at graph build time). Lane arrays exceeding the slot capacity are clamped with a rate-limited diagnostic.
- `CustomPortSnapshot::kMaxBytes = 256` — custom port types using `VIVID_PORT_TRANSPORT_CUSTOM_VALUE` must fit within 256 bytes when crossing cadence boundaries. Larger payloads should use `VIVID_PORT_TRANSPORT_CUSTOM_REF` (opaque pointer via the shared handle registry).
- `RecordingTap::kRingSize = 960000` — lock-free mix recording ring buffer holds ~10 seconds at 48 kHz stereo interleaved.
- `AudioNodeState::error_message[256]` — audio-thread error messages are fixed-size 256-char buffers (no heap allocation). Messages exceeding 255 characters are silently truncated.
- `AnalysisSnapshot::kWaveformSamples = 1024` — per-node waveform data returned from audio to frame side.

## 5.6 Port Type System

The type system serves three consumers: the graph runtime (bridge selection), the UI (valid connection enforcement), and the LLM (compatibility reasoning).

Six built-in port types reflect the runtime's routing mechanisms:

- `VIVID_PORT_SCALAR` — scalar float (control values: floats, ints, bools all route identically). Updated at no fixed rate.
- `VIVID_PORT_AUDIO_BUFFER` — a 256-sample buffer at 48kHz. Always continuous — producing a buffer every callback, even if silence. Mono throughout; stereo is two ports (left/right).
- `VIVID_PORT_LANE_ARRAY` — variable-length float array with broadcast semantics.
- `VIVID_PORT_STRING` — UTF-8 string.
- `VIVID_PORT_STRING_LANES` — variable-length string array.
- `VIVID_PORT_TEXTURE` — 2D RGBA8 `WGPUTextureView` with per-node configurable resolution (default 1280×720).

### Custom Port Types and the Port Type Registry

Operators can define custom port types for exchanging arbitrary typed data (GPU buffers, media streams, meshes, compute dispatches) through the graph. Custom type IDs are generated at compile time via `vivid_port_type<T>()`, which combines `vivid_type_id<T>()` (FNV-1a hash of the C++ type name via `__PRETTY_FUNCTION__`) with a high-bit marker (`| 0x80000000u`). Because the hash includes the fully-qualified type name, it produces a stable, deterministic ID across separate translation units and `dlopen` boundaries.

Each custom type has an associated `VividPortTransport` that describes how the payload crosses domain boundaries:

- `VIVID_PORT_TRANSPORT_CUSTOM_VALUE` — small structs (≤256 bytes) copied by value through a snapshot buffer.
- `VIVID_PORT_TRANSPORT_CUSTOM_REF` — opaque pointer routed via the shared handle registry. Used for large or non-copyable objects like media streams.

The `VIVID_CUSTOM_PORT` macro declares a custom port in a single line:

```cpp
VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer)
```

Operator dylibs register their custom types by exporting `vivid_describe_custom_types()`, which returns a static array of `VividPortTypeInfo` records. The `VIVID_DESCRIBE_REF_TYPE(T)` convenience macro handles this for single-type `CUSTOM_REF` operators. The runtime calls this export after `dlopen` and registers each type in the global port type registry. Re-registering the same type with identical fields is idempotent; mismatched fields trigger a fatal error.

When the user draws a connection in the graph editor, the runtime compares custom type IDs on both ends. Mismatched IDs (e.g. connecting a `VividNoteBuffer` output to a `MeshBufferV1` input) are rejected — the connection is never created. This prevents silent `void*` misinterpretation.

### Semantic Tags (Advisory)
Port types can carry optional semantic tags: normalized (0–1), bipolar (-1 to 1), frequency_hz, decibels, midi_note, etc. **Tags are advisory hints, not enforced by the runtime.** When connecting ports with mismatched ranges, the graph editor suggests inserting a visible Remap node with the mapping pre-configured. No silent auto-mapping.

### Param Storage and Widgets

`VividParamType` intentionally stays primitive: float, int, bool, file, and text. Params own persisted configuration values; they do not own executable behavior or arbitrary structured blobs. Compound inspector controls are modeled as widgets over runs of primitive params via optional `widget_id` and `widget_span` descriptor metadata.

Built-in widgets such as ADSR, LFO preview, color, XY pad, and step sequencer use the same widget registry path as package-defined widgets. Existing `display_hint` values remain supported as compatibility metadata. Package operators may tag a primitive param run with a custom widget id and render that run from the operator's custom inspector callback. Graph JSON, presets, variations, locks, MIDI mapping, and `set_param` / `set_string_param` continue to address the primitive params by name.

Envelope, oscillator, and LFO controls in inspectors are presentation over primitive params unless they need executable private state. Executable host-local behavior belongs in an owned `ChildOp<T>`; graph-visible data belongs on ports, including custom port types when a package needs a new wire payload.

## 5.7 Operator API Contract

Each operator is a self-contained compilation unit — a shared library (`.dylib`) with a known interface. The runtime and operators share C++ types via common headers, but the hot-reload boundary uses `extern "C"` functions for `dlopen` stability:

```cpp
#include "operator_api/operator.h"

struct MyEffect : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "MyEffect";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> intensity{"intensity", 0.5f, 0.0f, 1.0f};
    vivid::Param<int>   mode{"mode", 0, {"Normal", "Inverted"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out = {&intensity, &mode};
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out = {{"input",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT},
               {"output", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT}};
    }
    void process_frame(const VividFrameContext* ctx) override {
        float in = ctx->input_values[0];
        ctx->output_values[0] = in * intensity.value;
    }
};
VIVID_REGISTER(MyEffect)
```

Three domain-specific mix-in interfaces exist: `vivid::FrameProcessable` (implements `process_frame(const VividFrameContext*)`), `vivid::AudioProcessable` (implements `process_audio(const VividAudioContext*)`), and `vivid::GpuProcessable` (implements `process_gpu(const VividGpuContext*)`). All operators inherit `vivid::OperatorBase` and exactly one of these interfaces — operators are single-cadence. The `VIVID_REGISTER` macro generates `extern "C"` entry points (`vivid_abi_version`, `vivid_descriptor`, `vivid_create`, `vivid_destroy`, and domain-specific dispatch functions). It emits a `vivid_abi_version()` function returning `VIVID_OPERATOR_ABI_VERSION`, with the source-of-truth value defined in `src/operator_api/types.h`. The runtime checks that value on `dlopen` to reject stale dylibs left over from a previous build — it is not a cross-version compatibility contract. Operators always compile against the current headers.

For statically linked export builds (§5.16), the `extern "C"` boundary is unnecessary — everything links together as one C++ binary. The macro handles both cases.

**Crash isolation:** The runtime wraps every `process_frame` / `process_audio` / `process_gpu` call in a `CrashGuard` RAII guard that tracks the current operator name in a thread-local variable. If an operator triggers a fatal signal (`SIGSEGV`, `SIGBUS`, `SIGABRT`, `SIGFPE`), the signal handler prints the operator name to stderr before re-raising for a core dump. This turns "Vivid crashed" into "Vivid crashed in MyBrokenOp" — essential for diagnosing third-party operator failures. See `src/runtime/core/crash_guard.h`.

The simpler this contract, the better everything downstream works: auto-generated UI knobs, confident LLM generation, and fast compilation of small self-contained units.

### 5.7.1 Owned Child Operators

Operators can embed control-domain operators as persistent member variables using `ChildOp<T>` (defined in `src/operator_api/child_op.h`). This enables internal modulation chains — e.g. an LFO driving a gain stage — without exposing child operators as separate graph nodes.

**Embeddable operators** must satisfy one of two supported shapes:
- `header-only embeddable`: every concrete definition needed by `ChildOp<T>` is available from the header
- `support-backed embeddable`: the operator keeps plugin-facing code in `.cpp`, and registers a `name_embeddable.cpp` support unit through `vivid_embeddable_op_support` for any out-of-line destructor, virtual method implementation, thumbnail hook, or other non-inline definition needed by `ChildOp<T>` consumers

**Embeddable support files** are minimal embedded-use glue only. They must not contain:
- `VIVID_REGISTER(...)`
- plugin export macros such as `VIVID_THUMBNAIL(...)`
- full plugin-only thumbnail, inspector, or runtime wrapper behavior

**Current embeddables:**
- header-only: `LFO` (`control/lfo/lfo.h`)
- support-backed: `Smooth` (`control/smooth/smooth.h` + `smooth_embeddable.cpp`), `Envelope` (`control/envelope/envelope.h` + `envelope_embeddable.cpp`)

**When to choose which style:**
- prefer header-only for lightweight, dependency-light modulation cores
- use embeddable support when the operator should still be embeddable but its plugin-facing thumbnail or other virtual behavior is better kept out-of-line

**Usage pattern:**

```cpp
#include "operator_api/child_op.h"
#include "control/lfo/lfo.h"
#include "control/smooth/smooth.h"

struct MyOp : vivid::OperatorBase, vivid::FrameProcessable {
    vivid::ChildOp<LFO>    lfo_;
    vivid::ChildOp<Smooth> smoother_;

    void process_frame(const VividFrameContext* ctx) override {
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
- `process()` builds a child `VividFrameContext` inheriting `time`, `delta_time`, and `frame` from the parent context.
- Children maintain persistent state across frames (e.g. LFO phase), just like top-level operators.
- The parent's `collect_params` / `collect_ports` only expose the parent's params — child params are internal.
- Child params should be mirrored as ordinary parent params or a compound param widget when the user needs to edit them.

**Domain restriction:** ChildOp is for owned control-domain behavior. Audio operators need per-sample buffer processing on the audio thread, and GPU operators run as shader pipelines — neither maps to the `ChildOp` call-and-read pattern.

All 7 GPU operators (Particles, InstancedShapes, Flocking, Trails, Fluid, ReactionDiffusion, CellularAutomata) use `ChildOp<T>` for internal modulation — typically LFO or Envelope instances driven by `<role>_`-prefixed host params (e.g., `envelope_attack`, `scale_rate`). These child operators are private implementation details with no separate metadata layer.

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

## 5.7.3 2D Drawable Pipeline

**Decision: A split drawable-pipeline sits alongside the legacy texture-chain for 2D GPU work.** Instead of every 2D operator rendering to its own texture with a fullscreen fragment pass, the drawable pipeline moves a lightweight `VividDrawable2D` record between operators and terminates at `Render2D`, which rasterises all accumulated drawables in one pass. This unlocks TD-grade instancing (10K+ shapes in a single draw call), cross-operator batching, and compute-shader-driven content — things the texture-chain model structurally can't do.

**`VividDrawable2D` is a tagged-union record** (312 bytes, ABI-padded) transported via `VIVID_PORT_TRANSPORT_CUSTOM_REF`. Its `type` field selects one of SHAPE (SDF primitive), SPRITE (textured quad), TEXT (glyph run), MESH (future), or CUSTOM. A drawable carries its transform, color, blend mode, optional `z_layer`, and either a single-instance fallback or a pointer to an `instance_buffer` holding an `InstanceData2D[N]` array. An `InstanceArray2D` is a separate custom-ref bundle (`{data, count}`) produced by generators and consumed by `Instancer2D`.

**The pipeline shape is Emitter → Modifier → Instancer → Render.** Types of operator:

| Role | Operators | Produces / Consumes |
|------|-----------|---------------------|
| Emitter (source) | `ShapeEmitter`, `SpriteEmitter`, `TextLabel`, `Particles2D`, `Flocking2D` | Emit a `VividDrawable2D`. `TextLabel` carries a persistent glyph atlas + per-glyph buffer; `Particles2D`/`Flocking2D` own a compute-shader instance buffer. |
| Layout generator | `InstanceGrid2D`, `InstancesFromLanes2D` | Emit an `InstanceArray2D` (grid/circle/line positions, or packed from lane arrays). |
| Modifier | `InstanceNoise2D`, `Transform2D` | Accept a bundle (or drawable), add jitter or compose a TRS transform, emit the modified bundle/drawable. |
| Combiner | `DrawableMerge` | Merge multiple drawables into one bundle for ordered rendering. |
| Instancer | `Instancer2D` | Attach an `InstanceArray2D` to a drawable template — N instances of one shape in one draw call. |
| Terminal | `Render2D` | Rasterises drawables into a texture. Output is a standard `gpu_texture` port. |

**Canonical recipes:**

```
# One shape on screen.
ShapeEmitter → Render2D → video_out

# N tiled shapes (CPU-driven positions).
InstanceGrid2D → Instancer2D ← ShapeEmitter
                       ↓
                     Render2D → video_out

# Tiled + time-varying jitter.
InstanceGrid2D → InstanceNoise2D → Instancer2D ← ShapeEmitter → Render2D

# Lane-driven placement (per-attribute control sources).
SpreadNoise × N → InstancesFromLanes2D → Instancer2D ← ShapeEmitter → Render2D

# Compute-shader particles.
Particles2D → Render2D → video_out

# Legacy SDF shape-field (self-contained, pre-E pipeline).
ShapeField → Render2D → video_out
```

**Mixing with the texture chain is expected.** `Render2D` outputs a regular `gpu_texture`, so any post-processing operator (`Bloom`, `Feedback`, `TimeMachine`, `LutApply`) continues the chain from there. Operators whose state *is* a texture (Fluid, CellularAutomata) stay texture-chain native — the drawable pipeline doesn't try to absorb them.

**Ordering semantics:** drawables render in traversal order by default; assign a non-NaN `z_layer` to override via stable sort. Depth test is OFF (alpha-blended 2D); `z_layer` is a sort key, not a depth value. Per-drawable `DrawIndexed` keeps the draw loop tight; `Render2D` sorts by pipeline + bindgroup to minimise state switches rather than merging into a single mega-shader.

**Worked demos live in `graphs/gpu/*_demo.json`**: `shape_emitter_intro`, `instancer_2d_grid_demo`, `instancer_2d_noise_demo`, `instances_from_lanes_2d_demo`, `particles_2d_demo`, `flocking_2d_demo`.

Full design history lives under `docs/plans/archive/2d-pipeline/` (master plan + E.1–E.7 detail files).

## 5.8 Hot-Reload Behavior

**Decision: Parameters survive, internal state resets.** Since parameters live outside the operator in the graph's Control-layer parameter store, they are untouched by a reload. The operator's private internal state reinitializes fresh. This avoids serialize/deserialize complexity and matches creative workflows where the user is iterating on behavior.

Hot-reload flow: file system watcher (efsw, cross-platform) detects operator source change → invoke system C++ compiler to build `.dylib` → `dlclose` old library → `dlopen` new library → call `vivid_create` with existing parameter values → operator resumes with new behavior, old parameter state intact.

## 5.9 Lanes: Implicit Vectorization

**Decision: Every value in the graph can carry multiple parallel elements — lanes.** A single number is a one-lane value. An FFT output is a 512-lane value. When a multi-lane output connects to a single-lane input, the operation automatically vectorizes across all lanes. No explicit loop nodes are needed for the common case.

This is the single most impactful design decision for Vivid's data model. It resolves the instantiation problem that plagues every visual programming environment for creative work: "how do I make 500 particles?" In Vivid, the answer is "connect a 500-lane position value to a rendering operator." Where those lanes came from — a grid generator, an FFT, a MIDI controller, a lane source — doesn't matter. The operator processes all elements.

Precedent: vvvv's Spreads, Houdini's per-point attribute operations, and Blender Geometry Nodes' Fields all validate this pattern. The systems that handle instantiation best all converge on the same insight: the right primitive for creative work is not an object with methods but an element with attributes, operated on in parallel.

**Key properties:**

- **Broadcasting:** scalar values broadcast into any lane set. A single control knob modulating a 512-lane particle field applies the same value to every lane.
- **Provenance:** multi-lane values carry lane-set provenance. Two values with the same `lane_set_id` are aligned lane-for-lane. Mismatched non-scalar lane sets require explicit reshape operators (Repeat, Tile, Select).
- **Cross-domain:** lane-bearing control values (e.g., 512 FFT bins) can connect directly to GPU operators, producing 512 visual elements driven by audio. The Control→GPU bridge handles the data; lanes handle the cardinality.
- **LLM-friendly:** describing lane-based operations in natural language is natural. "Create 512 particles in a circle, sized by the FFT, colored by frequency" maps directly to a chain of operations on lane-bearing values.
- **Port types:** `VIVID_PORT_LANE_ARRAY` is the port type for variable-length float lane arrays. `VIVID_PORT_STRING_LANES` is the port type for variable-length string lane arrays. Texture and audio ports don't have a lane-array variant — multiple instances use multiple ports.
- **Cross-domain bridge implementation:** Control↔Audio uses pre-allocated `BridgeLaneSlot` buffers inside the double-buffered `ParamSnapshot`/`AnalysisSnapshot` bridges — no heap allocation on the audio thread. The audio callback reads lane data directly from bridge slots (zero-copy). Control→GPU uses WebGPU storage buffers: the operator uploads lane data via `wgpuQueueWriteBuffer` into a `ReadOnlyStorage` binding that the fragment shader reads as `array<f32>`.
- **Lane identity:** for stateful lane sets (polyphonic voices, persistent simulations), lanes carry stable identity tokens (`lane_id`) that survive reordering and compaction. Operators access per-lane persistent state via `vivid_lane_state()` keyed by `lane_id`, not positional index.

### 5.9.1 Core Value Model

The semantic unit that moves through the graph is:

- **payload kind + lane set**

Those axes are independent:

- **payload kind** answers what each lane carries: scalar float, string, audio buffer, texture, or custom payload.
- **lane set** answers how many parallel elements the value contains and how those elements relate.
- **cadence** answers when the value is produced and consumed. It is separate from both payload kind and multiplicity.

This separation is what lets Vivid say "512 FFT bins driving 512 particles" without inventing a separate collection system for each payload kind or cadence.

### 5.9.2 Lane Behaviors

Operators participate in the lane model in four ways:

- **Pointwise** — preserve the upstream lane set and apply the operator independently per lane.
- **Structural** — create, reshape, remap, or otherwise legalize a new lane arrangement.
- **Reduction** — intentionally collapse many lanes into fewer lanes.
- **Kernel** — read across lanes while still operating inside the same multiplicity system.

These are semantic behaviors, not transport details. They explain how an operator treats multiplicity regardless of whether the underlying payload is floats, strings, or something else.

### 5.9.3 Legality and Provenance

Lane compatibility is stricter than "the counts happen to match."

- Matching lane count is necessary, but not sufficient, for elementwise alignment.
- Lane-set provenance is the default proof that two multi-lane values are aligned lane-for-lane.
- Structural operators are the explicit places where reshaping, remapping, or broadcasting becomes legal.

This is why Vivid can broadcast a scalar into any lane set by default while still requiring explicit reshape operators such as `Repeat`, `Tile`, or `Select` when two non-scalar lane sets do not already share provenance.

### 5.9.4 Lane Identity

Not every lane set needs identity semantics. Vivid distinguishes between:

- **positional lane sets** — only the order and count matter
- **identity-bearing lane sets** — lanes carry stable identity tokens that matter across time

Identity-bearing lane sets are what make polyphonic voices, persistent simulations, and other stateful pointwise systems behave correctly. Reordering or compaction may change positional index, but stable `lane_id` is what preserves per-lane state.

**Cross-cadence lane state rule:** When per-lane persistent state crosses the `AudioFrameBridge` boundary, all per-lane persistent state must be sourced through `vivid_lane_state()` in both the frame-side and audio-side operators when `ctx->lane_state_fn` is present. Any member state in such an operator is a scalar fallback only and must not be used when lane-state services are active.

### 5.9.6 Lane Execution Strategies

Lane behaviors (§5.9.2) describe the *semantic* relationship between an operator and its lanes. **Execution strategies** describe *how the runtime physically processes* those lanes. The compiler chooses a strategy per node during Pass 4c (audio) and Pass 4d (frame):

- **`Scalar`** — `lane_count=1`. Single instance, no lifting. The common case for operators that only receive scalar inputs.
- **`InstancePerLane`** — N cloned operator instances, one per lane. Used for audio-domain Pointwise operators with multi-lane inputs. The executor **deinterleaves** the multi-lane input buffer into N mono buffers, calls `process_audio()` on each instance independently, then **interleaves** the N mono outputs back into a multi-lane output buffer. Each instance maintains its own persistent state (filter memory, oscillator phase, etc.), which is what makes polyphonic audio work — each voice is a genuinely independent operator instance.
- **`LoopBased`** — single instance, runtime-driven loop over lanes. Used for frame-domain Pointwise operators that declare `strategy_independent = true`. The executor loops over lanes, setting `input_values` and collecting `output_values` per iteration. More memory-efficient than `InstancePerLane` but only works for stateless-per-lane operators.

**Scalar-to-lane broadcasting:** When a scalar output connects to a `VIVID_PORT_LANE_ARRAY` input, and the compiler has marked that port as non-scalar (via Pass 2.6), the frame executor broadcasts the scalar value by repeating it to match the lane count of other inputs on the same node. This is the runtime implementation of the broadcasting rule described in §5.9.

**Lane count limits:**
- `max_loop_lanes = 16` — default maximum for `LoopBased` iteration. Exceeding this logs a warning and clamps.
- `kDefaultLaneCapacity = 1024` — default lane buffer size for `VividLaneOutput` builders in control operators.
- `InstancePerLane` has no hardcoded limit but is bounded by available memory (each instance allocates its own audio buffers).

### 5.9.7 Capability Differences, Not Model Differences

Float lanes and string lanes are both first-class lane-bearing values.

- `VIVID_PORT_LANE_ARRAY` is the float-lane transport.
- `VIVID_PORT_STRING_LANES` is the string-lane transport.

The storage, operators, or backend support available to those payload kinds may differ, but those are capability differences. They do not create separate multiplicity models.

## 5.10 Simulation Zones: Frame-to-Frame State

> **Status: Deferred past 1.0.** The design below is retained as planned architecture. In the current implementation, GPU video feedback is handled by the `operators/gpu/feedback/` operator, which maintains its own previous-frame texture buffer internally. General-purpose Simulation Zones are not yet implemented.

**Decision: Simulation Zones provide explicit, visible frame-to-frame feedback.** A Simulation Zone is a marked region of the graph whose output at frame N becomes an additional input at frame N+1. This is the mechanism for all persistent, evolving state: particle motion, video feedback, envelope followers, accumulators, counters.

In a normal dataflow graph, everything is stateless — each frame computes from scratch. But creative behaviors need memory: a particle's position at frame 42 depends on its position at frame 41 plus its velocity. Video feedback takes the previous frame's output, transforms it, and composites it with new input. An envelope follower smooths a signal by blending with its previous value.

The Simulation Zone makes this feedback explicit and visible in the graph, unlike TouchDesigner's implicit Feedback TOP where the feedback path is invisible. Inside the zone, a special "Previous State" input carries whatever the zone output last frame. The user wires up transformations — apply forces, decay opacity, blend with new input — and the output both leaves the zone for downstream use and loops back to become next frame's Previous State.

**Domain applications:**

- **GPU — video feedback:** previous frame's texture → Blur → Displace → Composite with new input. The classic generative feedback loop, now debuggable because every step is visible.
- **GPU — particle state:** the Previous State is a lane-bearing value of particle positions/velocities/colors. Inside: apply forces, update positions, kill dead particles, spawn new ones. The lane output is both renderable data and state for next frame.
- **Audio — envelope follower:** previous smoothed value blended with new raw value by a coefficient. Output is the smoothed value.
- **Control — accumulators:** previous count incremented on each beat event. Running totals, state machines, event counters.

**Lane-compatible:** the state inside a Simulation Zone can be lane-bearing. "500 particles each with their own evolving state" is a Simulation Zone operating on a 500-lane value. Each lane carries its own position, velocity, color, and lifetime — updated in parallel every frame.

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

**Decision: macOS first.** The initial release targets macOS exclusively. This eliminates cross-platform build/test complexity and matches the primary development environment. The architecture does not paint into a corner — Dawn, GLFW, and miniaudio all support Linux and Windows, so cross-platform is a matter of build configuration, not redesign.

## 5.13 Windowing: GLFW

**Decision: GLFW 3.4 for window creation and input.** GLFW creates the OS window, provides the Metal surface for Dawn, and handles keyboard/mouse input events. It is minimal (~200KB source), mature, and has proven WebGPU integration.

Alternatives considered: SDL3 provides file dialogs, pen/tablet pressure, touch input, and a structured event queue, but adds ~2MB of surface area and capabilities that are not needed for the initial release. Raw Cocoa (NSWindow + CAMetalLayer) provides maximum control but is macOS-only with no migration path.

GLFW does not provide file open/save dialogs or pen/tablet pressure. File dialogs will be added via tinyfiledialogs (single-header C library) or a small Cocoa shim when save/load is implemented. Tablet pressure support is deferred past 1.0 and can be added via platform-specific input handling without replacing the windowing library.

## 5.14 Dependency Manifest

**Decision: Focused dependencies, most of which are small C/C++ libraries.** CMake manages the build. No external package manager required.

| Dependency | Purpose | Size | Integration |
|---|---|---|---|
| **wgpu-native** (pinned release) | GPU abstraction (WebGPU over Metal) | ~17MB binary | FetchContent (Rust/Cargo build) |
| **GLFW** (3.4) | Window creation, input events, Metal surface | ~200KB source | git submodule |
| **glfw3webgpu** | GLFW↔WebGPU surface bridge | ~5KB source | git submodule |
| **miniaudio** (0.11.x) | Audio device I/O (not DSP) | single header | vendored |
| **stb_truetype** | Font rasterization for UI text | single header | vendored |
| **stb_image** | Image loading (PNG, JPEG, BMP) | single header | vendored |
| **nlohmann/json** | JSON parsing and serialization (graph files, project files) | header-only | FetchContent |
| **Google Highway** | Portable SIMD substrate for runtime-internal optimized kernels | header-only + static lib | FetchContent |
| **RtMidi** | MIDI I/O (CoreMIDI on macOS) | ~50KB source | vendored |
| **oscpack** | OSC message serialization and UDP transport | ~30KB source | vendored |
| **Syphon** | GPU texture sharing between applications (macOS) | ~100KB source | vendored |
| **Snappy** | Fast compression (used by HAP video codec) | ~50KB source | FetchContent |
| **IXWebSocket** | HTTP server for the runtime control server endpoint | ~200KB source | FetchContent |
| **CLI11** | Command-line argument parsing | header-only | FetchContent |
| **libcurl** | HTTP fetches for package catalog and appcast update checks | system library | `find_package(CURL)` |
| **Midifile** | Standard MIDI file parsing | ~150KB source | pinned FetchContent, custom static library target |
| **efsw** (1.5.1) | Cross-platform file watching for hot reload | ~200KB source | FetchContent |
| **TinyXML-2** (10.0.0) | Lightweight XML parsing (appcast feed) | ~100KB source | FetchContent |
| **Sparkle** (macOS) | App auto-update framework | framework | system framework |

**Note on Dawn:** The original plan called for Google's Dawn WebGPU implementation. The actual integration uses wgpu-native (a Rust-based WebGPU backend) via eliemichel's WebGPU-distribution adapter layer, with a pinned upstream release tag (`gfx-rs/wgpu-native`) providing Metal interop symbols for Syphon texture sharing.

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
│   │   ├── runtime_core.cpp/.h # Graph compilation, frame-rate execution, audio frame bridge
│   │   ├── audio_engine.cpp/.h # miniaudio device, audio callback, ParamSnapshot bridge
│   │   ├── gpu_context.cpp/.h  # WebGPU device, queue, surface
│   │   ├── hot_reload.cpp/.h   # File watch, compile, dlclose/dlopen swap
│   │   ├── file_watcher.cpp/.h # Cross-platform file system monitoring (efsw)
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
│   │   ├── gpu_operator.h      # GpuProcessable, VividGpuContext, VividGpuState
│   │   ├── gpu_types.h         # VividGpuBuffer, VividMesh, VividComputeBuffer
│   │   ├── child_op.h          # ChildOp<T> for operator composition
│   │   ├── wgsl_filter.h       # WgslFilterBase for shader-backed GPU operators
│   │   ├── data_driven_filter.h # WgslOperator with dynamic param/port collection
│   │   ├── audio_dsp.h         # WhiteNoise, PinkNoise, waveform(), detect_trigger()
│   │   └── note_types.h        # VividNoteBuffer / VividNoteEvent — native note transport
│   ├── cli/                    # CLI tooling
│   └── export/                 # Standalone export build
│       └── standalone_main.cpp
├── operators/                  # Built-in operators (each a directory)
│   ├── gpu/                    # noise, shape, text, bloom, composite, feedback,
│   │                           # movie_file, webcam_in, syphon_in, syphon_out,
│   │                           # texture_analysis, time_machine, ...
│   ├── audio/                  # oscillator, gain, delay, reverb, distortion, bitcrush,
│   │                           # spread_adsr, spread_lfo, ...
│   └── control/                # lfo, clock, envelope, math, smooth, gate,
│                               # keyboard, mouse, midi_input, osc_in, osc_out,
│                               # fft_analysis, stack, alternate, step_counter,
│                               # folder_list, string_select, logic, ...
├── filters/                    # Data-driven WGSL filters (auto-discovered, no C++)
├── graphs/                     # Demo graphs organized by category
│   ├── intro/   ├── filters/   ├── gpu/   ├── audio/   └── io/
├── tests/                      # CTest suite
├── mcp/                        # Python MCP bridges (vivid_mcp.py, vivid_opdev_mcp.py)
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

## 5.18 WGSL Shader Operators

**Decision: self-describing `.wgsl` files are first-class operator types. No per-filter C++ code is required, and there is no separate preset/filter subsystem.**

A shader-backed operator is a single `.wgsl` file in a `filters/` directory. A JSON comment block at the top declares its name, parameters, and input ports:

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

3. **`WgslOperator`** (`src/operator_api/data_driven_filter.h`) wraps `WgslFilterBase` with dynamic param and port collection driven by a `WgslOperatorConfig` struct. A single C++ class serves all shader-backed operators.

4. **`OperatorRegistry::scan_shader_operators()`** (`src/runtime/operator_registry.cpp`) scans a `filters/` directory, parses each `.wgsl` header, and registers a concrete operator type per file. Built-in filters, package filters, and project-local filters all use this same path.

**Param metadata** supports type (float/int/bool), min/max/default, enum choices, display hints (`"knob"`, `"xy_pad"`, `"color"`), groups, and column layout — all declared in JSON, all consumed by the inspector UI without per-filter code.

**Persistence model:** graphs store the concrete operator type name (for example `Blur`), and the shader source remains in a real file under core `filters/`, a package `filters/`, or `<graph_dir>/filters/`.

**Reload model:** body-only `.wgsl` edits hot-reload inside `WgslFilterBase`. Header-derived descriptor changes (params, inputs, time-dependence, or operator name) rescan the affected shader directory and rebuild the graph instead of mutating descriptors in place.

**Why this matters:** Adding a new GPU shader operator means writing one `.wgsl` file. No C++ boilerplate, no CMake changes, no registration macro. The file is auto-discovered, hot-reloadable, and its parameters appear in the inspector automatically. This is the path for both built-in filters and user-authored ones.

## 5.19 State Machines & Subgraphs

> **Status: Infrastructure exists, operator not yet implemented.** The graph schema supports state-preset mappings (`StatePresetMapping` in `graph.h`), and the UI can detect StateMachine nodes for preset-per-state wiring. However, the StateMachine operator itself has not been implemented. The design below is retained as planned architecture. Note: module-file-based instruments (§5.24) shipped as a separate encapsulation model; embedded subgraph definitions inside graph JSON remain deferred.

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

**Decision: Patterns are standard control-domain operators with lane-array ports, not a DSL.** Composition happens through normal graph wiring. The lane model (§5.9) provides implicit vectorization — a pattern transformer operates on all lanes transparently.

**Three operator roles:**

- **Generators** produce Spread outputs from parameters or time inputs: `Euclidean` (Bjorklund rhythm patterns), `PatternSeq` (16-step sequences), `NotePattern` (per-step chord specifications), `ChordProgression` (diatonic chords from scale degrees).
- **Transformers** take a Spread input and produce a transformed Spread output: `PatTransform` applies reverse, rotate, scale, offset, and probabilistic element nulling in a fixed chain. Each step is a parameter — no control flow.
- **Combinators** merge multiple Spread inputs: `Stack` (concatenate or interleave up to 4 Spread inputs), `Alternate` (time-driven selection between Spread inputs on beat/bar boundaries).

**Composable chains:**

```
Euclidean → PatTransform → Stack → Arpeggiator
(generate)   (rotate+scale)  (combine w/ melody)  (consume as note sequence)
```

Every intermediate result is a lane-bearing value visible on a wire. Every step is a discrete operator with inspectable parameters. The LLM can reason about pattern composition using the same vocabulary it uses for any other graph operation.

**Why not a DSL:** A pattern language would require its own parser, type system, and error reporting — and would be opaque to the graph editor and LLM. By making patterns ordinary operators, they inherit lane broadcasting, cross-domain bridging, serialization, hot-reload, ChildOp embedding, and inspector UI for free. The cost is verbosity (a chain of 4 operators vs. a one-line expression), but the graph editor makes this visual, not textual.

## 5.22 MCP / LLM Integration

Vivid exposes its runtime and operator-authoring surfaces through two complementary external bridges on top of the same in-process runtime.

**Control Server** (`src/runtime/control_server.cpp`) — an HTTP server (powered by IXWebSocket) running inside the Vivid runtime on `127.0.0.1:9876` by default. It handles OSC messages, MIDI input, and the HTTP JSON-RPC runtime endpoint. Graph inspection and mutation, operator scaffolding and compilation, parameter read/write, capture, analysis, package management, and undo/redo all flow through this surface.

**Python runtime MCP bridge** (`mcp/vivid_mcp.py`) — a Python wrapper that connects to the control server and re-exposes the live runtime/control surface as a standard MCP stdio server. This is the graph/runtime bridge used for graph mutation, inspection, capture, diagnostics, checks, package/runtime management, and starter scaffolding. The running Vivid app owns the live graph and the HTTP port; the Python process owns the MCP stdio layer.

**Python opdev MCP bridge** (`mcp/vivid_opdev_mcp.py`) — a separate Python MCP stdio server focused on operator authoring. It exposes API docs, example operators, capability guidance, and operator-lifecycle helpers that sit alongside the runtime bridge rather than inside it.

**Runtime API** (`src/runtime/runtime_api.cpp/.h`) — the internal C++ API that the control server and related runtime-facing surfaces call into. All graph mutations, operator creation, capture, and analysis operations are implemented here. The Runtime API operates on the same in-process data structures as the runtime and graph — no serialization overhead.

## 5.23 Media Pipeline

Vivid's media pipeline handles video file playback with synchronized audio through one mixed-domain operator:

- **`MovieFile`** (`operators/gpu/movie_file/`) — decodes movie video and audio from one source identity. It exposes `texture`, `audio`, `time`, `duration`, and movie diagnostic outputs from a single node-level playback session. Params include `file`, `play_mode`, `speed`, `volume`, `pitch_preserve`, and `video_phase_offset_ms`.

**AV sync model:** `MovieFile` owns one playback session per graph node. When the source has audio and the `audio` output is active, the audio read head is the authoritative clock. Video presentation follows that session clock plus the explicit `video_phase_offset_ms` presentation offset. When there is no active audio, the session uses a monotonic host-clock transport. The runtime compiles mixed-domain nodes into cadence-specific operator instances that share the same node-level session, so the audio callback remains realtime-safe while frame/GPU execution owns video presentation, texture upload, and telemetry.

## 5.24 Instrument Coherence Platform

> **Status: V1 shipped.** Six additions that make Vivid a better host for instrument-like packages. V2 follow-ons for each step are deferred (see `docs/plans/ROADMAP.md`). Detailed design rationale lives in `docs/archive/instrument-coherence/`.

The guiding principle: build reusable platform pieces that packages leverage, rather than moving synth logic into core. Every step reuses existing graph routing, lane semantics, file-param paths, and serialization patterns.

### Subgraph Instruments (Step 1)

Module definitions live in `.vivid-module.json` as the authored canonical source. Instances behave like single nodes with a curated exposed-control surface. Internal nodes remain part of graph truth at runtime via the existing flatten-before-compile model — no separate execution path.

Exposed controls carry the same metadata as normal operator params: type, default/min/max, choice labels, group/section, display hints, semantic metadata, and description. Module instances are first-class synthetic operators: they show only exposed controls in the inspector, use grouped sections, support module-level factory presets, and provide an "open source module" action for editing internals.

### Composite-Local Modulation (Step 2)

A local modulation assignment layer on module instances. Authors declare named sources (e.g., `env2`, `lfo1`, `macro1`, `velocity`) and named destinations (e.g., `filter_cutoff`, `brightness`, `wt_position`). Users assign sources to destinations with amount, polarity (unipolar/bipolar), and optional curve.

Assignments are lowered into ordinary internal graph routing at compile time (additive: `base_value + source * amount`). Normal wires remain the primary routing model; this does not introduce a new runtime object or a second routing substrate. Lane-aware rules apply: scalar→scalar allowed, scalar→lane-aware allowed (broadcast), lane→lane allowed if provenance-aligned, lane→scalar disallowed in V1.

### DualFilter (Step 3)

A new core audio operator providing dual-stage filtering with four routing modes: `serial_ab` (A→B), `serial_ba` (B→A), `parallel` (blended by balance knob), and `split` (frequency crossover, low→A, high→B, recombined). Each stage exposes enabled, mode, cutoff, resonance, drive, and keytrack params. The existing single-stage `Filter` operator is unchanged.

Lane-aware: per-stage filter memory is keyed by `lane_id` for polyphonic behavior. Scalar CV inputs are shared across voices.

### Asset Library (Step 4)

A generic asset-library and import/index/cache layer with wavetables as the first supported kind. Graphs continue storing canonical file paths (not `asset_id`) in V1. User-imported assets are copied into `<workspace_root>/assets/library/<kind>/<asset_id>/`; package assets are discovered read-only from package directories.

Asset index entries carry: `asset_id`, kind, display name, source scope (package/workspace), canonical path, source hash, timestamps, file metadata, and kind-specific `kind_meta`. The import pipeline copies files into the workspace library, computes metadata eagerly, and regenerates cache only when the source fingerprint or analyzer version changes.

### Per-Note Expression & Performance Pages (Step 5)

Extends `MidiInput` with lane-array outputs for expressive per-note data: `lane_ids`, `pitch_bends`, `pressures`, `slides`, `expressions`, `channels`. A `mode` param selects `poly_shared` (broadcast), `mpe_lower`, or `mpe_upper` (per-channel-to-lane mapping). Scalar outputs `aftertouch` and `expression` are also added.

Performance-surface metadata on exposed params: `performance_page`, `performance_order`, `performance_role` (built-in roles: `macro`, `mod_wheel`, `expression`, `aftertouch`, `xy_x`, `xy_y`). Performance controls are ordinary exposed params — presets, variations, and modulation still apply.

### Graph Content Metadata & Browser (Step 6)

Extends `GraphContentMeta` with instrument-oriented fields: `content_kind` (example/instrument), `category`, `family`, `role` (hero/reference/utility), `playability` (self_playing/midi/hybrid), and `preview_controls[]` (metadata references to node params). The browser gains a top-level kind filter (All / Instruments / Examples) and sorts instrument entries by package → category → family → title. Graph files remain the browseable preset unit — no new preset format.
