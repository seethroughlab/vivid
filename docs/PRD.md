# Vivid — Product Requirements Document

## 1. Vision

In the entire creative coding ecosystem, audio is structurally second-class. TouchDesigner's CHOPs exist, but nobody calls it an audio authoring environment. openFrameworks' ofSoundPlayer is a thin wrapper. Unity and Unreal treat audio as playback, not a creative medium. The result: in practice, the visual team builds the installation and then someone drops in a soundtrack. The audio doesn't *respond* to the same data the visuals respond to.

Meanwhile, in music production, Bitwig Studio has shown what happens when audio is treated as a medium you sculpt in real time rather than a timeline you arrange. Its modulation system — where any signal can modulate any parameter with visible feedback directly on the control — demonstrates that cross-domain routing can be immediate, visual, and explorable. Vivid takes this philosophy and extends it across the audio-visual boundary.

Vivid is the first general-purpose creative coding platform where audio and visuals are equal peers — authored in the same graph, driven by the same data, with the same level of expressive control. It combines the inspect-anywhere philosophy of TouchDesigner, the extensibility of openFrameworks, the modulation routing of Bitwig, the inline output of Jupyter notebooks, and the immediacy of Strudel — but with plain C++ that LLMs can read and write.

> **Vivid's core innovation:** an LLM that populates exploration spaces, not just writes code. The user never stares at a blank canvas. They describe a direction, the LLM generates a field of possibilities, and the user navigates that field through direct manipulation.

Vivid's value is the environment, not the operators. In the age of LLM-assisted development, writing code that does exactly what you want is cheap. What's expensive is making experimentation and discovery possible and productive. Vivid ships a minimal set of seed operators — just enough to validate each domain and serve as examples for the LLM — and relies on LLM-generated operators for everything else. The framework is the product; the operators are emergent.

**Core Admission Policy (package-first):** New operators go into domain packages by default (`vivid-sequencers`, `vivid-sampler`, etc.), not core. An operator belongs in core only if it (a) is a fundamental building block that most graphs depend on (e.g. LFO, Clock, Math, Gain), or (b) provides an essential example pattern for the LLM that no package operator can substitute. Specialized or domain-specific operators — even widely used ones — belong in packages. This keeps the core minimal and the package ecosystem rich.

### 1.1 What Vivid Is For

- Live audiovisual performances
- Museum and gallery installations
- Branded interactive experiences
- Interactive art and generative art
- Creative prototyping and experimentation

### 1.2 What Vivid Is Not For

- Game engines (use Unity, Unreal, Godot)
- Traditional desktop applications
- Web applications
- Film/video post-production (linear composition tools)

**The scope boundary:** Vivid is for real-time, interactive audiovisual experiences. If the output is a window (or multiple windows, or a projector, or an LED wall) showing real-time graphics with real-time audio, and the system is reactive to some input (performer, sensor, data, time), Vivid is the right tool.

---

## 2. Core Principles

Ordered by importance. Principles 1–4 define *what Vivid is*. Principles 5–8 define *how it works*. Principles 9–11 define *how it's built*.

### 2.1 Audio-Visual Parity

The thesis. Why Vivid exists. Audio-visual parity means three things simultaneously:

- **Equal ease of creation:** a drum machine should be as easy to create as a particle system. The interface, documentation, and operator library treat both domains as first-class.
- **Equal breadth of options:** for any visual technique the creator wants to explore (feedback, particle systems, shaders, compositing), there should be an equally rich set of audio tools (synthesis, sequencing, effects, analysis) available in the same environment.
- **Easy cross-domain interaction:** this is the one existing tools miss entirely. When you create an interactive installation that responds to a camera tracking people, the audio and visual responses should be authored in the same graph, driven by the same data, with connections between domains as easy to make as connections within a single domain.

The cross-domain interaction point is critical. TouchDesigner has CHOPs and TOPs, but connecting audio analysis to visual parameters requires manual bridging between separate operator families in separate editor panes. Max/MSP separates audio (MSP) from video (Jitter) into different namespaces. In Vivid, audio, visual, and control operators coexist in a single unified graph. A wire from an FFT analysis node to a particle system's size parameter is the same gesture as a wire between two visual effects.

### 2.2 Temporal Plurality

**Vivid has no master timeline. It has clocks.** Clocks are temporal contexts that can be driven by anything — wall time, musical tempo, sensor data, spectral energy, walking speed, each other. Patterns, state machines, and animations all bind to clocks, not to "the" time.

A single master timeline implies an experience has a beginning, a duration, and an end. An installation running 12 hours a day in a museum lobby has no beginning. It has states, cycles, reactions, and rhythms, but no timeline. Forcing it onto one is like putting a river on a ruler.

This is the architectural foundation that makes Vivid equally suited for performances (where one clock happens to be musical tempo) and installations (where there's no privileged clock and everything is reactive or cyclical).

### 2.3 General-Purpose Positioning

Vivid is a general-purpose creative coding platform — not just a performance tool. The A/V parity thesis isn't about live AV artists alone; it's about the entire creative coding ecosystem where audio is structurally second-class across installations, branded experiences, museum exhibits, and any context where visuals and sound should respond to the same data.

### 2.4 Experimentation First

Most creative coding time is spent *trying things* — not performing and not installing. Vivid's interface treats experimentation as the primary mode of use, not a stepping stone to a finished piece. The distance between an idea and seeing/hearing its result should be as short as possible. The framework's interface is designed around the experimental loop of tweak → perceive → tweak, with both audio and visual feedback simultaneous and immediate.

When LLMs can write operators on demand, construction is no longer the bottleneck — exploration is. The value of a creative coding tool shifts from "what operators does it ship?" to "how fast can I try an idea, hear it, see it, and try the next one?" Every design decision in Vivid optimizes for this loop.

### 2.5 Text Is the Source of Truth

The canonical representation of a Vivid project is text — diffable, versionable, LLM-readable. The visual graph is a view of that text, not a replacement for it.

### 2.6 See Every Step

Every point in the processing chain is inspectable. A creator debugging an installation in the studio sees the same chain-health information as anyone working with the system in real time. The LLM and developer get structured data (JSON) for the same state.

### 2.7 Hot Reload Everything

Changes to parameters and routing propagate within the same frame. Changes to operator implementations recompile and hot-swap within 1–3 seconds. The system never requires a restart. Non-negotiable.

### 2.8 LLM-Native Workflow

Every layer of Vivid's architecture is designed for LLM interaction: the routing graph is serializable JSON the LLM can read and write, the operator API is constrained enough for confident generation, and the experimentation interfaces are designed to be populated by the LLM, not just built by the user.

The primary LLM workflow is operator authoring, not graph wiring. The user describes what they want ("a GPU operator that makes particles orbit a central point with velocity proportional to their FFT bin value"), the LLM writes it as a self-contained C++ file, hot-reload compiles it in under a second, and the user sees the result immediately. This scaffold → edit → reload loop is what makes Vivid's minimal operator set viable: any operator the user needs can be generated on demand.

### 2.9 Keep the Core Minimal

The Vivid runtime is small. The core handles graph execution, domain bridging, and the operator plugin interface. Vivid ships seed operators — a small set per domain that validate the architecture and serve as examples the LLM learns from — not a comprehensive operator library. Most operators are generated by the LLM during a session, authored for the specific project, and kept in the project directory. The operator contract is the most important API surface in the system because every LLM-generated operator is written against it.

### 2.10 Creator Tools vs Developer Tools

Built-in UI handles what external tools can't: real-time GPU/audio feedback, parameter manipulation, connection routing, live preview. External tools handle what Vivid shouldn't rebuild: code editing, file management, version control. Decision framework: "Would a creator need it during a live session, while prototyping in the studio, or while running an installation?"

Operator code editing happens in the user's native IDE (VS Code, CLion, etc.), not inside Vivid. Vivid provides scaffolding — right-click in the graph to create a new operator, and Vivid generates the boilerplate C++ file with the correct base class and opens it in the external editor. On save, Vivid detects the change and hot-reloads the operator into the palette.

### 2.11 Don't Reinvent the Wheel

The creative coding ecosystem has 20 years of solved problems. Vivid leverages existing tools aggressively. The value is in the integration and the experimentation layer, not in rebuilding synthesizers, shader libraries, or UI frameworks from scratch.

---

## 3. Experimentation & Interface Design

### 3.1 What Experimentation Actually Requires

#### The Perception-Action Loop Must Be Under 50ms
The threshold between "I changed something and I see/hear the result" and "I'm waiting for something to happen." Parameter tweaks, connection changes, and preset switches must be instant. Kernel/shader recompilation is the only acceptable source of latency (50–200ms for kernel swaps, 1–3s for full operator hot-reload).

#### Combinatorial Discovery Requires a Palette
Experimentation isn't just tweaking parameters — it's combining different building blocks. The palette of available operators, connections, and configurations must be easy to browse, easy to populate (both by user and LLM), and easy to extend.

#### Happy Accidents Require Branching
When something unexpected and good happens, there must be a low-cost way to save the current state, continue exploring, and return to it later. Undo/redo is not enough — it's linear. What's needed is branching: "save this as a variation, keep going."

#### Visible Options Produce More Exploration
When alternatives are visible (a grid of visual variations, a set of connection configurations), users explore more combinations than when they must imagine alternatives. The interface should make options spatial, not just sequential.

#### Randomization and Mutation Are Legitimate
"Try something different" is a valid creative operation. Randomizing parameters within intelligent ranges, mutating existing configurations, and LLM-generated "what if" variations are all part of the exploration toolkit.

### 3.2 The Six Experimentation Interfaces

> **1.0 Status:** Two of the six interfaces shipped: the node graph (strong) and the session/variation surface (functional as a linear variation strip with save/recall/queue/quantize/reorder/branch). The remaining four — live REPL, parameter space explorer, pattern algebra, and state machine — are deferred past 1.0. See `docs/ROADMAP.md` for the deferred list.

Each interface below is a lens on the same underlying patch — different views of the same data, optimized for different exploration modes.

#### The Node Graph

The graph is the canonical structural view: operators as nodes, connections as wires, parameters as values on nodes. This is how the user understands what exists and how it's connected. It's the foundation, but not the primary experimentation tool — it's too low-level for rapid exploration.

Discovery potential: low (structural, not parametric). Latency: instant for connections, 50–200ms for adding operators.

#### The Session / Variation Grid

A grid where columns are different configurations of the same structural patch, and rows might represent audio variations, visual variations, or complete A/V states. The user can click through columns to audition, drag to reorder, and ask the LLM to "fill this row with 8 variations of the particle behavior." Think Ableton Session View but for audiovisual configurations rather than clip launching.

Discovery potential: very high. Latency: instant for parameter snapshots, 50–200ms for kernel swaps.

#### The Live REPL

A command input that executes graph mutations against the running state. Each command modifies the live output: adding nodes, changing connections, setting parameter values. Previous commands are visible as toggleable history. The TidalCycles and Hydra communities demonstrate deeply exploratory text-based live coding. The LLM can suggest next commands based on REPL history. The REPL operates on the same JSON graph representation that everything else uses.

Discovery potential: high. Latency: instant for parameter changes, 50–200ms for structural changes.

#### The Parameter Space Explorer

Every exposed parameter as a manipulable control (slider, knob, XY pad, color picker). Parameters can be grouped, MIDI-mapped, randomized, and interpolated between saved presets. Once a structure exists, this finds the sweet spot. The LLM annotates parameters with semantic meaning and suggests values based on intent ("make it feel tense and dark"). The innovation: controls span both audio and visual domains simultaneously.

Discovery potential: moderate (high for refinement). Latency: instant.

#### The Pattern Algebra

Time-varying behavior through composable pattern expressions (TidalCycles model). Patterns are first-class: nested, combined, transformed, and applied to any parameter. Combining two simple patterns often produces complex, musically interesting polyrhythms. Pattern expressions are extremely LLM-friendly: short, text-based, compositional. No existing tool applies pattern algebra to audiovisual parameter mapping.

Discovery potential: very high. Latency: instant (interpreted scheduling instructions).

#### The State Machine

Macro-level structure as named states (intro, build, drop, ambient, climax) with transition conditions. Each state has its own parameter configuration, active modules, and pattern set. Transitions can be crossfades, hard cuts, or morphs. State machines are among the most LLM-friendly structures — small, discrete, easy to reason about.

Discovery potential: moderate (high for composition). Latency: 50–200ms for kernel swaps.

### 3.3 The Audio/Visual Exploration Asymmetry

A critical design constraint: the preceding interfaces implicitly treat audio and visual exploration as if they have the same feedback characteristics. They do not.

Visual exploration is spatial and instantaneous — you can glance at a grid of 8 visual variations and evaluate all of them in under a second. Audio exploration is temporal and sequential — you cannot "glance" at a sound. You have to listen to it unfold over at least a bar or two. The evaluation bandwidth asymmetry is roughly 65×: visual permits ~8 options per second; audio permits ~0.12 options per second (~8 seconds per variation).

When exploring audio and visual together, the audio's temporal constraint dominates. Visual exploration gets slowed to audio speed, wasting the spatial advantage that makes visual experimentation so productive.

### 3.4 The Tiered Exploration Model

The resolution is not to treat audio and visual exploration identically, but to design for their different speeds. Crucially, the interface does not impose an order. A creator might start with audio and explore visuals against it, start with visuals and add audio later, or iterate on both from rough drafts. The strategies below are what the interface supports, not steps it enforces.

#### Strategy: Anchor One Domain, Explore the Other

The most productive cross-domain exploration comes from holding one domain stable while freely exploring the other. Which domain to anchor is the creator's choice:

- **Audio-first:** select or compose an audio element, then rapidly explore visual responses against it. The audio loops; visual changes are instant. The variation/session surface and parameter exploration tools are at their most powerful here.
- **Visual-first:** build a visual scene, then explore how different audio elements interact with it. This is common for installations where the visual identity is primary and audio is a reactive layer.

The interface assists whichever domain is being explored: when exploring audio, it offers rich metadata, short-loop auditioning, and A/B comparison to compensate for audio's slower evaluation. When exploring visuals, it offers spatial grids, instant switching, and LLM-generated mapping variations.

#### Strategy: Coupled Refinement

Fine-tuning the relationship between audio and visual once both are roughly in place. This operates at audio speed (evaluation per bar/phrase) and is the most LLM-assisted phase: the LLM observes the current state and suggests adjustments, the user saves successful coupled configurations to the session grid as complete A/V states.

#### Audio Exploration Aids

Because audio evaluates slower than visual, the interface provides tools to narrow the gap:

- **LLM annotations:** each audio variation tagged with semantic descriptions ("more syncopated," "darker harmonic quality"). The user scans descriptions first, auditions only promising ones.
- **Visual audio previews:** spectrograms, rhythm grids, harmonic maps — giving audio some of visual's spatial evaluation quality.
- **Short-loop auditioning:** 2-beat excerpts rather than full 4-bar phrases. Cuts audition time by 75%.
- **Comparative playback:** A/B toggle switching between variations on the beat boundary.

### 3.5 LLM Role by Exploration Strategy

| Strategy | LLM Role | Example |
|----------|----------|---------|
| Audio Exploration | Annotator + pre-filter | "Variation B has a deeper swing feel with a Neapolitan chord in bar 3." |
| Visual Exploration | Variation generator | "Here are 6 mapping configurations ranging from minimal to heavy audio-reactivity." |
| Coupled Refinement | Critic + advisor | "Particle density peaks are lagging the beat by ~100ms. Try routing onset detection with 0 smoothing." |

---

## 4. The LLM Execution Bridge

### 4.1 The Representation Problem

Vivid's canonical representation must simultaneously serve three masters: human readability (understand the flow, find the parameter to change), LLM readability and writability (parse, understand, generate valid modifications without hallucinating), and execution efficiency (compile to real-time frame rates with GPU-scale element counts).

### 4.2 The Two-Layer Architecture

The routing layer (what connects to what, parameter values) is interpreted and hot. Changes propagate within the same frame. It's a lightweight data structure: a graph of nodes with typed ports, connection lists, and parameter values. Serializable to JSON. An LLM reads and writes it trivially. This is what the user directly manipulates during experimentation.

The computation layer (per-element logic, GPU kernels, audio DSP) is compiled and fast. When the user or LLM modifies per-element behavior, this triggers a scoped compilation step. Only the changed kernel recompiles. The routing layer keeps running while the kernel compiles; the new kernel swaps in atomically when ready.

This is what Max/MSP's Gen~ does for audio, what TouchDesigner's GLSL TOPs do for visuals, and what Faust does for audio DSP. The graph is interpreted and instant; the inner computations are compiled and fast.

### 4.3 The Orchestration Layer

The routing graph is JSON, and JSON is the complete description. All automation, timing, and logic are handled by visible Control operators in the graph (LFO, Clock, Sequencer, Pattern, Envelope, Math, Logic, Gate, Smooth). There is no separate scripting or DSL layer. The answer to "how does this work?" is always "look at the graph."

This is the most LLM-friendly design: the LLM reads JSON, writes JSON. No code generation for the orchestration layer. Parameter transitions, pattern-driven modulation, and conditional logic are all visible as nodes rather than hidden in scripts.

External integration via WebSocket: Vivid's internal model is always JSON graph + control operators. A WebSocket API accepts graph mutations from external processes — a Python script, a JS orchestrator, a Max patch, or any other tool can generate JSON and feed it to Vivid. This provides scripting-level power for users who want it, without introducing a language dependency into the core.

Build toward visual graph equivalence on top of the same JSON representation. Every graph has a canonical visual representation and a canonical JSON representation. They are isomorphic. The LLM generates JSON (its strength); the visual graph exists for direct manipulation. The escape hatch to raw WGSL/C++ exists for power users writing operator internals.

### 4.4 The Four LLM Roles

**Operator layer — LLM as author (primary role).** The user describes intent, the LLM writes a self-contained C++ operator, hot-reload compiles it in under a second. "Write a GPU operator that makes particles orbit a central point with velocity proportional to their FFT bin value." This is the core workflow. Because operator authoring is cheap and fast, Vivid doesn't need to ship a comprehensive operator library — the LLM generates what the project needs, and the operator contract (§5.7) is designed to make that generation reliable.

**Routing layer — LLM as architect.** "Build me a patch with 3 audio analysis bands driving 3 visual layers with independent particle systems." The LLM generates graph structure as JSON that the user then explores. This is the scaffolding role — often combined with operator authoring when the scaffold requires new operators that don't yet exist.

**Experimentation layer — LLM as variation generator.** "Generate 8 different particle behavior variations." "Fill this session column with alternate mappings and parameter moods." The user evaluates and selects. The LLM produces breadth; the user provides taste.

**Reflective layer — LLM as critic and analyst.** "What's happening harmonically in the audio right now?" "The visual rhythm isn't syncing with the beat — what's wrong?" The LLM observes the current state and helps the user understand and refine.

### 4.5 LLM Integration Architecture

The LLM connects to Vivid through two complementary paths, both built on a shared Runtime API.

The Runtime API is an internal interface exposing all LLM-relevant operations: inspect graph structure, read and write parameters, capture frames and audio, run analysis tools, evaluate assertions, scaffold operators, and modify graph topology. This is the single source of truth for what the LLM can do. Both integration paths below call into the same API.

**Path 1: Built-in chat.** A collapsible chat panel inside Vivid's interface (§6.4) calls the Anthropic API directly and invokes the Runtime API in-process. This is the primary workflow for creative exploration: "make the particles react more to the bass," "generate 5 variations of this feedback loop," "why is the output so dark?" Zero latency between the LLM's intent and its effect on the graph. The user stays in Vivid.

**Path 2: Python MCP bridge.** A separate Python MCP bridge process exposes the same Runtime API as MCP tools by connecting to the running Vivid instance over the local HTTP control server. Claude Code, Claude.ai, or any MCP-capable LLM connects to that bridge externally. This is the power-user workflow for operator development: scaffolding C++ operators, debugging compilation errors, running test assertions from the terminal, and automating batch operations. It also enables non-interactive use cases: CI pipelines running assertions, scripts generating patch variations, installation monitors watching for drift.

> **1.0 Status:** The Python MCP bridge is the shipped 1.0 LLM integration path (57 tools covering graph manipulation, introspection, packages, variations, checks, and more). The built-in chat panel is deferred past 1.0. The Runtime API and HTTP control server (61 endpoints) are fully implemented; the MCP bridge wraps most but not all of them (notable gap: `analyze_output` and `compare_outputs` are HTTP-only and should be added to the MCP bridge as a near-term 1.0 item).

Both paths are Phase 1 in the original design. The built-in chat handles creative workflows where immediacy matters. The Python MCP bridge handles development workflows where the user is already in their IDE or terminal. The underlying Runtime API is implemented once; the chat panel and MCP bridge are thin layers on top.

**Future path:** WebSocket API (Phase 3) exposes the same Runtime API over WebSocket for non-LLM external processes — Python scripts, Max/MSP, show control systems. The Python MCP bridge and WebSocket API may share transport infrastructure but serve different audiences.

---

## 5. System Architecture

### 5.1 Two-Tier Interaction Model

**Fast path** — parameter adjustments and connection routing are instantaneous. No compilation, just reconfiguring the live graph. This layer is entirely declarative (parameters, topology), making it inherently LLM-friendly.

**Slow path** — editing an operator's C++ implementation. The user right-clicks a node and selects "Edit in IDE," which opens the operator's source file in their configured external editor (VS Code, CLion, etc.). On save, Vivid detects the file change, builds just that operator's shared library via the bundled compiler, and hot-swaps it into the running graph. Expected latency: 1–3 seconds. For new operators, Vivid scaffolds the boilerplate C++ with the correct base class and port declarations before opening the file.

### 5.2 Language and Toolchain

**Decision: C++ throughout.** The runtime, interface, and operators are all C++. This eliminates any ABI translation layer between the runtime and operators — they share types, headers, and conventions directly. The only boundary is the `extern "C"` interface used for dlopen-based hot-reload during development.

C++ was chosen over Zig and Rust for one overriding reason: library integration. Vivid is a creative technology tool that will integrate specialized libraries on client timelines — OpenCV, ONNX Runtime, NDI, Syphon, libtorch, CEF, and others. All of these are C or C++ libraries. In C++, integration is just "link and include." In Zig or Rust, every library requires FFI bindings, wrappers, and ongoing maintenance — friction that compounds over time.

**Build system:** CMake. The standard for C++ projects. Every library Vivid might depend on supports CMake. Dependencies are managed via git submodules or CMake FetchContent — no package manager.

**Compiler:** System clang on macOS (Xcode Command Line Tools). This is a one-time install (`xcode-select --install`) that virtually every developer on macOS already has. Cross-platform builds will use the platform's native compiler (MSVC on Windows, GCC or Clang on Linux).

**Operator compilation for hot-reload:** during development, operators are compiled by invoking the system C++ compiler as a subprocess. Vivid detects which compiler is available and invokes it directly. For future zero-friction onboarding (no system compiler required), a bundled compiler option can be added later — Zig's `zig c++` command is a single-binary C++ compiler that could serve this role without requiring Vivid's runtime to be written in Zig.

### 5.3 Three Domains

**Control** — event-driven and low-rate continuous data. MIDI, OSC, DMX, serial input, keyboard input, API responses, LLM output, timers, clocks. The nervous system of the graph. Runs push-based: events propagate forward immediately.

**GPU** — renders frames on the graphics card. Textures and buffers. Runs pull-based, driven by vsync / the render loop.

**Audio** — renders sample blocks in a real-time audio thread. Buffers at 44.1/48kHz. Runs pull-based, driven by the audio callback. Never allocates, locks, or blocks.

MIDI, OSC, DMX, and similar protocols are all forms of control data. Under the Control umbrella, a MIDI CC can drive a shader parameter and a synth filter cutoff simultaneously with no special treatment.

### 5.4 Execution Model: Hybrid Push/Pull

Control is push-based — events propagate forward immediately. Audio and GPU are pull-based — driven by their respective hardware clocks. When a Control change reaches the boundary of an Audio or GPU subgraph, it updates the parameter store. The next pull cycle picks it up. No domain ever waits on another.

### 5.5 Domain Bridges: Control as Hub

**Decision:** Control sits at the center of a star topology. Audio and GPU never communicate directly — everything routes through Control. This simplifies the architecture from six specialized bridges to two bidirectional mechanisms:

**Control ↔ Audio**

- **Control → Audio:** lock-free ring buffer or atomic. Audio callback reads at next block boundary. Latency: ~5ms at 256 samples / 48kHz.
- **Audio → Control:** audio analysis operators write results into a lock-free queue. Control nodes poll at whatever rate they like.

**Control ↔ GPU**

- **Control → GPU:** atomics or double-buffered parameter store. GPU render loop picks up changes next frame. Latency: ~16ms at 60fps.
- **GPU → Control:** async readback from GPU staging buffers. Control emits events when data lands. Latency: 1–2 frames.

**Why not direct Audio ↔ GPU?** Audio and Control both live on the CPU. The "hop" through Control is just a CPU-side buffer copy (nanoseconds), followed by the same CPU→GPU upload that would happen regardless. The only real domain boundary is CPU↔GPU, and that crossing happens exactly once no matter how the data is routed.

Backup approach: If six explicit per-pair bridges prove to share enough machinery during implementation, a unified port abstraction may emerge naturally from the bottom up.

### 5.6 Port Type System

The type system serves three consumers: the graph runtime (bridge selection), the UI (valid connection enforcement), and the LLM (compatibility reasoning).

Seven canonical port types reflect the runtime's routing mechanisms:

- `VIVID_PORT_FLOAT` — scalar float (control values: floats, ints, bools all route identically). Updated at no fixed rate.
- `VIVID_PORT_AUDIO` — a 256-sample buffer at 48kHz. Always continuous — producing a buffer every callback, even if silence. Mono throughout; stereo is two ports (left/right).
- `VIVID_PORT_SPREAD` — variable-length float array with broadcast semantics.
- `VIVID_PORT_STRING` — UTF-8 string.
- `VIVID_PORT_STRING_SPREAD` — variable-length string array.
- `VIVID_PORT_TEXTURE` — 2D RGBA8 `WGPUTextureView` with per-node configurable resolution (default 800×600).
- `VIVID_CUSTOM_PORT(id)` — custom port types using either `CUSTOM_REF` (opaque pointer) or `CUSTOM_VALUE` (inline blob) transport. Type-safe via named type registry with `transport`, `type_name`, and `payload_size`. Used for GPU buffers, meshes, compute dispatches, media streams, MIDI, and package-defined types.

**Semantic Tags (Advisory)**

Port types can carry optional semantic tags: `normalized` (0–1), `bipolar` (-1 to 1), `frequency_hz`, `decibels`, `midi_note`, etc. Tags are advisory hints, not enforced by the runtime. When connecting ports with mismatched ranges, the graph editor suggests inserting a visible Remap node with the mapping pre-configured. No silent auto-mapping.

### 5.7 Operator API Contract

Each operator is a self-contained compilation unit — a shared library with a known C ABI interface. The graph runtime introspects inputs, outputs, and parameter declarations:

```cpp
#include "operator_api/operator.h"

struct MyEffect : vivid::ControlOperatorBase {
    static constexpr const char* kName = "MyEffect";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> intensity{"intensity", 0.5f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out = {&intensity};
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out = {{"input",  VIVID_PORT_FLOAT, VIVID_PORT_INPUT},
               {"output", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT}};
    }
    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0] * intensity.value;
    }
};
VIVID_REGISTER(MyEffect)
```

This contract is the most important API surface in the system. Three domain-specific base classes exist: `vivid::ControlOperatorBase` (`process()`), `vivid::AudioOperatorBase` (`process_audio()`), and `vivid::GpuOperatorBase` (`process_gpu()`). The `VIVID_REGISTER` macro generates `extern "C"` entry points and infers domain from the base class. Because the LLM generates most operators on demand rather than wiring together pre-built ones, every friction point in writing an operator — unclear types, boilerplate, implicit conventions — is a direct tax on the core workflow. The simpler this contract, the better everything downstream works: auto-generated UI knobs, confident LLM generation, fast compilation of small self-contained units, and reliable hot-reload.

### 5.8 Hot-Reload Behavior

**Decision:** Parameters survive, internal state resets. Since parameters live outside the operator in the graph's Control-layer parameter store, they are untouched by a reload. The operator's private internal state reinitializes fresh. This avoids serialize/deserialize complexity and matches creative workflows where the user is iterating on behavior.

### 5.9 Spreads: Implicit Vectorization

**Decision:** Every wire in the graph implicitly carries a Spread — an ordered collection of values. A single number is a Spread of length 1. An FFT output is a Spread of length 512. When a Spread-producing output connects to a single-value input, the operation automatically vectorizes across all elements. No explicit loop nodes are needed for the common case.

This is the single most impactful design decision for Vivid's data model. It resolves the instantiation problem that plagues every visual programming environment for creative work: "how do I make 500 particles?" In Vivid, the answer is "connect a Spread of 500 positions to a rendering operator." Where that Spread came from — a grid generator, an FFT, a MIDI controller, a Spread literal — doesn't matter. The operator processes all elements.

Precedent: vvvv's Spreads, Houdini's per-point attribute operations, and Blender Geometry Nodes' Fields all validate this pattern. The systems that handle instantiation best all converge on the same insight: the right primitive for creative work is not an object with methods but an element with attributes, operated on in parallel.

**Key properties:**

- **Broadcasting:** when two Spreads of different lengths connect to the same operator, the shorter one repeats (wraps) to match the longer. A Spread of 3 colors applied to a Spread of 512 particles cycles through the 3 colors.
- **Cross-domain:** a Spread of Control values (e.g., 512 FFT bins) can connect directly to a GPU operator's parameter, producing 512 visual elements driven by audio. No explicit bridging required — the existing Control→GPU bridge handles the data; Spreads handle the cardinality.
- **LLM-friendly:** describing Spread-based operations in natural language is natural. "Create 512 particles in a circle, sized by the FFT, colored by frequency" maps directly to a chain of operations on Spreads.
- **Port types:** `Spread<VIVID_PORT_FLOAT>`, `Spread<VIVID_PORT_TEXTURE>`, `Spread<VIVID_PORT_AUDIO>` are all valid. The Spread is orthogonal to the domain type system.

### 5.10 Simulation Zones: Frame-to-Frame State

**Decision:** Simulation Zones provide explicit, visible frame-to-frame feedback. A Simulation Zone is a marked region of the graph whose output at frame N becomes an additional input at frame N+1. This is the mechanism for all persistent, evolving state: particle motion, video feedback, envelope followers, accumulators, counters.

In a normal dataflow graph, everything is stateless — each frame computes from scratch. But creative behaviors need memory: a particle's position at frame 42 depends on its position at frame 41 plus its velocity. Video feedback takes the previous frame's output, transforms it, and composites it with new input. An envelope follower smooths a signal by blending with its previous value.

The Simulation Zone makes this feedback explicit and visible in the graph, unlike TouchDesigner's implicit Feedback TOP where the feedback path is invisible. Inside the zone, a special "Previous State" input carries whatever the zone output last frame. The user wires up transformations — apply forces, decay opacity, blend with new input — and the output both leaves the zone for downstream use and loops back to become next frame's Previous State.

**Domain applications:**

- **GPU — video feedback:** previous frame's texture → Blur → Displace → Composite with new input. The classic generative feedback loop, now debuggable because every step is visible.
- **GPU — particle state:** the Previous State is a Spread of particle positions/velocities/colors. Inside: apply forces, update positions, kill dead particles, spawn new ones. The Spread output is both renderable data and state for next frame.
- **Audio — envelope follower:** previous smoothed value blended with new raw value by a coefficient. Output is the smoothed value.
- **Control — accumulators:** previous count incremented on each beat event. Running totals, state machines, event counters.

**Spread-compatible:** the state inside a Simulation Zone can be a Spread. "500 particles each with their own evolving state" is a Simulation Zone operating on a Spread of 500 elements. Each element carries its own position, velocity, color, and lifetime — updated in parallel every frame.

**JSON representation:** a Simulation Zone is a node with a feedback connection from its output to a designated state input. The runtime knows to buffer the previous frame's output and provide it as input on the next frame. The exact visual representation — whether a visible bounding box around grouped nodes or a single Feedback operator with an internal graph — is a UX question to be resolved during prototyping.

### 5.11 JSON Graph Schema

The JSON graph is the single source of truth for the entire system. Every operator, connection, parameter value, and structural relationship is captured in this format. The LLM reads and writes it directly. Below is a representative example:

```json
{
  "schema_version": 1,
  "vivid_version": "0.1.0",
  "meta": {
    "id": "audio_reactive_demo",
    "title": "Audio Reactive Demo",
    "tags": ["audio", "reactive"],
    "domains": ["gpu", "audio"]
  },
  "nodes": {
    "clock1": {
      "type": "Clock",
      "params": { "bpm": 120.0 },
      "layout": { "x": 30.0, "y": 200.0 }
    },
    "noise1": {
      "type": "Noise",
      "params": { "speed": 1.0 }
    },
    "vout": { "type": "video_out" }
  },
  "connections": [
    { "from": "clock1/beat_phase", "to": "noise1/speed" },
    { "from": "noise1/texture", "to": "vout/input" }
  ]
}
```

**Design principles:**

- **Node IDs as object keys:** fast lookup, prevents duplicates. IDs are user-readable strings ("fft1", "particles1"), not UUIDs.
- **Params carry current values only:** parameter metadata (min, max, default, semantic tags) is declared in the operator's C++ code and introspected at load time. The JSON stores only the user's current values. This keeps the JSON compact and avoids dual source-of-truth.
- **Connections are source/target pairs:** `"from": "node/port"` and `"to": "node/port"`. The operator declares its ports; the JSON just names them.
- **Spread-aware:** a connection from `fft1/spectrum` (`Spread<float>` of 512) to `particles1/scale` (`float`) implicitly fans out. The JSON doesn't need to represent this — the runtime infers cardinality from port types.
- **No per-node domain field:** domain is inferred from port types and base class at load time, not stored in the JSON. Nodes carry optional `"layout"` positions and optional `"pkg"` provenance for package version tracking.

### 5.12 Platform Target

**Decision: macOS first.** Phase 1 targets macOS exclusively. This eliminates cross-platform build/test complexity and matches the primary development environment. The architecture does not paint into a corner — Dawn, GLFW, and miniaudio all support Linux and Windows, so cross-platform is a matter of build configuration, not redesign.

### 5.13 Windowing: GLFW

**Decision: GLFW 3.4** for window creation and input. GLFW creates the OS window, provides the Metal surface for Dawn, and handles keyboard/mouse input events. It is minimal (~200KB source), mature, and has proven WebGPU integration.

Alternatives considered: SDL3 provides file dialogs, pen/tablet pressure, touch input, and a structured event queue, but adds ~2MB of surface area and capabilities that are not needed for Phase 1. Raw Cocoa (NSWindow + CAMetalLayer) provides maximum control but is macOS-only with no migration path.

GLFW does not provide file open/save dialogs or pen/tablet pressure. File dialogs will be added via tinyfiledialogs (single-header C library) or a small Cocoa shim when save/load is implemented. Tablet pressure support is a Phase 2+ concern and can be added via platform-specific input handling without replacing the windowing library.

### 5.14 Dependency Manifest

**Decision:** Seven dependencies, most of which are small C libraries. CMake manages the build. No external package manager required.

Key dependencies (managed via git submodules, vendored source, or CMake FetchContent):

- **wgpu-native** (pinned fork): GPU abstraction (WebGPU over Metal). Uses eliemichel's WebGPU-distribution adapter with a Seethrough Lab fork for Metal interop.
- **GLFW** (3.4) + **glfw3webgpu**: window creation, input events, WebGPU surface bridge.
- **miniaudio** (0.11.x): audio device I/O. Single-header C library.
- **stb_truetype** + **stb_image**: font rasterization and image loading. Single-header C libraries.
- **yyjson**: JSON parsing for graph files and project files. ~40KB source.
- **RtMidi**: MIDI I/O (CoreMIDI backend on macOS).
- **oscpack**: OSC message serialization and UDP transport.
- **Syphon**: GPU texture sharing between applications (macOS-only).
- **Snappy**: fast compression for HAP video codec.
- **IXWebSocket**: HTTP server powering the runtime control server endpoint.
- **CLI11**: command-line argument parsing.
- **Sparkle**: macOS app auto-update framework.

**Compiler requirement:** Xcode Command Line Tools on macOS (`xcode-select --install`). Provides clang, libc++, and Metal framework headers.

### 5.15 Project Directory Structure

**Decision:** Single C++ codebase with a four-level operator search path. The runtime and operators are all C++. Operators compile as individual shared libraries for hot-reload during development.

**Source tree:**

```
vivid/
├─ CMakeLists.txt                # Top-level build
├─ deps/                         # Third-party (submodules or FetchContent)
│  ├─ dawn/  ├─ glfw/  ├─ miniaudio/  ├─ stb/  └─ yyjson/
├─ src/
│  ├─ runtime/                   # Core engine (C++)
│  │  ├─ main.cpp                # Entry point, window, main loop
│  │  ├─ graph.cpp/.h            # JSON graph loading, node management
│  │  ├─ scheduler.cpp/.h        # Frame scheduling, domain threads
│  │  ├─ spreads.cpp/.h          # Spread type, broadcasting
│  │  ├─ simulation.cpp/.h       # Simulation Zone state
│  │  ├─ bridges.cpp/.h          # Control↔GPU, Control↔Audio
│  │  ├─ params.cpp/.h           # Parameter store
│  │  ├─ gpu_context.cpp/.h      # Dawn device, queue, surface
│  │  ├─ audio_context.cpp/.h    # miniaudio device, buffers
│  │  ├─ hot_reload.cpp/.h       # File watch, compile, swap
│  │  └─ export.cpp/.h           # Standalone build logic
│  ├─ interface/                 # UI layer (C++)
│  │  ├─ widgets/                # Panel, Button, Slider, Knob, etc.
│  │  ├─ layout.cpp/.h           # Application layout
│  │  ├─ input.cpp/.h            # GLFW event → widget events
│  │  ├─ renderer.cpp/.h         # Widget → Dawn/WebGPU draw calls
│  │  ├─ theme.cpp/.h            # Visual style (§6.6)
│  │  └─ text.cpp/.h             # Text rendering (stb_truetype)
│  └─ operator_api/             # Shared headers for operator contract
│     ├─ operator.h              # Base classes, Param<T>, VIVID_REGISTER
│     ├─ spread.h                # Spread types
│     └─ types.h                 # Shared type definitions
├─ operators/                    # Built-in operators (each a directory)
│  ├─ gpu/
│  │  ├─ noise/ { noise.cpp, noise.wgsl }
│  │  ├─ blur/ { blur.cpp, blur.wgsl }
│  │  └─ ...
│  ├─ audio/
│  │  ├─ oscillator/ { oscillator.cpp }
│  │  └─ ...
│  └─ control/
│     ├─ lfo/ { lfo.cpp }
│     └─ ...
├─ projects/                     # Example projects
│  └─ demo_reactive/
│     ├─ graph.json              # The patch
│     ├─ assertions.json         # Quality gates
│     └─ operators/              # Project-local operators
└─ docs/
```

> **Note:** The actual directory structure has evolved significantly. See `docs/ARCHITECTURE.md` §5.15 for the current layout. Key differences: `src/interface/` is now `src/ui/`, the runtime directory contains ~40 modules (not the 10 shown above), and the project includes top-level directories for `tests/`, `filters/`, `mcp/`, `site/`, `fonts/`, `assets/`, `platform/`, and `scripts/`.

Each operator is a directory containing its .cpp source and, for GPU operators, its .wgsl shader(s). This structure supports hot-reload (watch one directory per operator), scaffolding (create a directory with boilerplate), and the library system (§5.17).

**Operator search path (priority order):**

1. **Project-local** — `my_project/operators/` — operators specific to this patch
2. **User global** — `<config_dir>/operators/` — personal operators shared across projects
3. **Installed libraries** — `<config_dir>/packages/*/operators/` — third-party packages (§5.17)
4. **Seed operators** — `vivid/operators/` — minimal set shipped with Vivid, primarily serving as LLM examples and domain validation

When two operators share the same name, earlier in the path wins. This lets users fork a library operator into their project to customize it.

### 5.16 Export: Standalone Builds

**Decision:** Export compiles the graph and its operators into a single standalone binary. During development, operators are separate .dylib files loaded via dlopen so they can hot-reload independently. For export, those same C++ source files are compiled as static .o files and linked into one binary.

CMake handles this with a separate build target that compiles operators as static libraries instead of shared libraries and links everything together. The `extern "C"` functions from `VIVID_REGISTER` are resolved at link time instead of via dlopen. The graph JSON is embedded as a compile-time resource.

**Tree-shaking:** exported builds compile only the operators the graph actually references. The build system reads the graph JSON, resolves operator types to source directories via the search path, and compiles only those. A graph using three operators produces a binary containing three operators, not the entire seed set.

The exported binary includes: the runtime, Dawn, miniaudio, the referenced operators, and the embedded graph. It does not include: the editor interface, GLFW, hot-reload machinery, or the LLM perception system. For windowed output (e.g., a projection application), a minimal GLFW window is included; for headless output (e.g., an LED wall media server), no window is needed.

### 5.17 Operator Libraries

**Decision:** Third-party operator libraries are GitHub repositories installed from source and compiled locally. No pre-built binaries, no CI pipeline required, no platform-specific distribution.

Libraries are the sharing mechanism for operators that prove useful beyond a single project. The typical lifecycle: the LLM generates an operator for a specific session, the user refines it, and if it's general enough, they publish it as a library for others to install. The library system exists to make this sharing frictionless, not to replace LLM generation as the primary way operators come into existence.

A library is a repository with a manifest and operator directories:

```
awesome-particles/
├─ vivid-library.json
├─ operators/
│  ├─ gpu/
│  │  ├─ fluid_sim/ { fluid_sim.cpp, fluid_sim.wgsl }
│  │  └─ voronoi/ { voronoi.cpp, voronoi.wgsl }
│  └─ audio/
│     └─ granular/ { granular.cpp }
└─ README.md
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

**Install flow:** `vivid install github.com/user/awesome-particles` → clones to `<config_dir>/packages/awesome-particles/` (macOS: `~/Library/Application Support/Vivid/packages/awesome-particles/`) → compiles all operators → they appear in the operator palette. Compilation is fast (single operators compile in under a second with clang).

**Development workflow:** `vivid link /path/to/my-package` creates a symlink from the packages directory to the developer's source tree instead of copying. Operators are compiled in-place — the `build/` directory lives inside the original source. After editing operator source, `vivid rebuild my-package` recompiles without re-copying. `vivid unlink my-package` removes the symlink without touching the source. This mirrors the `npm link` workflow: link once during development, rebuild after changes, unlink when done.

**Library template:** a template GitHub repository provides the directory structure, a starter operator with boilerplate, and the `vivid-library.json` manifest. No GitHub Actions needed for CI builds — operators compile from source on the user's machine.

**For export:** the build system follows the same search path to find operator source files. If a graph uses `fluid_sim` from an installed library, the export compiles that library's source directly into the standalone binary.

**Constraints:** libraries may only depend on the Vivid operator API and standard C/C++. External C library dependencies (OpenCV, FFTW) are not managed by the library system — users who need them are responsible for making them available to the build. This keeps the package manager from becoming a general-purpose build system.

---

## 6. Interface Architecture

Section 3 describes what the experimentation interfaces are. This section describes how they are built — the technology, rendering model, toolkit, layout, and thumbnail strategy.

### 6.1 GUI Technology: Native Rendering

**Decision:** The interface runs natively in the same GPU context as the Vivid runtime. This is constrained by a single non-negotiable requirement: the "See Every Step" principle demands live GPU texture thumbnails for every node in the chain, potentially 20+ simultaneously at frame rate.

A web-based interface (React/Svelte + WebSocket) was eliminated because GPU→CPU readback, encoding, and transport don't scale to 20+ thumbnails at 30fps. A hybrid approach using Chromium Embedded Framework was eliminated after direct implementation experience — texture sharing between Dawn's GPU context and Chromium's compositor proved unworkable, and the module added ~200MB of dependency for a fragile integration.

Native rendering gives zero-copy texture thumbnails (every intermediate texture is a handle that can be blitted directly), no process boundary, no IPC overhead, and sub-frame latency for parameter changes. The tradeoff is slower UI iteration compared to web technologies.

### 6.2 Rendering Mode: Retained

**Decision: Retained-mode UI, not immediate mode.** In immediate mode (Dear ImGui), the application redraws the entire UI every frame with no persistent widget objects. In retained mode, widgets are objects that persist between frames and manage their own state: a slider knows it's being dragged, a panel knows which child has focus, a list knows its scroll position.

Vivid's experimentation interfaces are inherently stateful — a session grid cell knows its variation and playback state, a parameter control tracks its MIDI mapping and drag state, and a node remembers selection, pinning, and inline editing state. Retained mode handles this naturally. Immediate mode would require maintaining all interaction state in parallel data structures, manually synchronized with draw calls every frame.

> **Implementation note:** The actual UI uses a hybrid approach. The node graph is the primary interface, rendered directly via WebGPU using `renderer_2d.cpp` for 2D drawing primitives. There is no separate retained-mode widget library — the node graph, inspector, and overlays are purpose-built drawing code in `src/ui/node_graph.cpp` (~5000 lines) with overlay layout logic in `overlay_layouts.cpp`.

### 6.3 Toolkit: Custom Purpose-Built Widgets

**Decision:** Build a purpose-built retained-mode widget set directly on the existing Dawn/WebGPU rendering context. Not a general-purpose UI framework — just the 10–15 widget types Vivid's experimentation interfaces actually need.

Alternatives evaluated and rejected: Dear ImGui (already in the repo, good for prototyping, but immediate-mode and limited aesthetic ceiling), Qt Quick/QML (mature but ~100MB+ dependency, GPL licensing complexity, two GPU contexts to coordinate), Slint (modern but young ecosystem with unproven custom texture integration).

The custom approach gives zero-copy texture thumbnails trivially (same GPU context), total control over look and interaction, no external dependencies, and purpose-built widgets the LLM can generate. The scope is bounded: rows, columns, fixed/flex sizing, scroll containers, and absolute positioning for the node graph. The required widget set:

- **Core:** Panel, Button, Slider, Knob, Dropdown, TextInput, Toggle
- **Specialized:** NodeGraph, SessionGrid, TexturePreview, Waveform/Meter

> **Implementation note:** No general-purpose widget library was built. The UI is purpose-built around the node graph with `renderer_2d.cpp` providing WebGPU 2D drawing (rounded rects, text, lines, bezier curves) and `node_graph.cpp` handling all interaction (node dragging, wire creation, selection, zoom/pan). Inspector panels are overlay layouts, not standalone widgets.

### 6.4 Application Layout

**Decision:** Output preview pinned right, tabbed workspace center-left, context-sensitive inspector below, transport strip at bottom, collapsible chat/REPL. This is the default fixed layout; the output preview can undock to a separate window for multi-monitor setups.

The visibility hierarchy driving this layout:

- **Always visible:** output preview (the perception-action loop), active parameters (context-sensitive to selection), transport/clock.
- **Primary workspace:** node graph as the central editor, with variation/session surfaces layered around it. Switching should feel like changing exploration mode, not navigating to a different application.
- **On-demand (collapsible):** LLM chat, live REPL, pattern editor, state machine editor. Brought up when needed, don't consume space during direct manipulation.
- **External:** operator code editing happens in the user's IDE, not inside Vivid.

The main workspace interaction pattern is centered on the node graph for structure and wiring, with the session/variation surface managing branching and alternate states. Parameter exploration and modulation overlays should live close to the graph rather than requiring a separate connection matrix view.

> **Implementation note:** The actual layout centers on the node graph as the primary workspace. The inspector is an overlay panel (not a separate pane). The session surface (toggled with V) provides variation branching, drag reorder, context menus (rename/duplicate/delete/branch), quantized switching, and five distinct card states (active/queued/dirty/selected/inactive). The output preview is the selected GPU node's texture, displayed in the node graph itself via live thumbnails. Transport/clock information appears as an overlay. File dialogs use native macOS sheets (`src/ui/file_dialog.mm`).

### 6.5 Node Thumbnails

**Decision:** Always-on small thumbnails, with on-hover fallback for large graphs. Every node in the graph displays a live texture thumbnail at all times, matching the existing Vivid chain visualizer and TouchDesigner's behavior. This directly serves the "See Every Step" principle — maximum inspectability. If GPU cost becomes a problem at high node counts (20+), a user toggle switches to on-hover mode where nodes are compact by default and expand on selection.

### 6.6 Visual Style

**Aesthetic: dark steel with colored accents.** Vivid's interface is a professional tool, not a consumer application. The visual language draws from hardware audio equipment and HUD displays — dark, high-contrast, precise, content-forward. Sharp geometry, monospace type, thin borders. More Elektron Digitakt than Apple Human Interface Guidelines.

**Core principles:**

- **Content is the star.** The interface chrome recedes; the live previews, waveforms, and values dominate. Node containers are minimal dark steel rectangles — as invisible as possible so the preview content takes focus.
- **Identity lives in the preview, not the container.** Operators across all three domains share the same container shape (sharp-cornered rectangles, uniform size). A thin accent-color bar at the top and small domain badge are the only container-level indicators. The preview content inside is where domain identity becomes unmistakable.
- **Three-color domain system.** GPU operators use cyan (#4ECDC4) for accent color. Audio operators use amber (#F0A030). Control operators use light gray (#C0C8D0). These colors appear in accent bars, port indicators, wire colors, and inspector highlights. Background and chrome use dark steel grays (#16191D background, #1A1D21 panels, #22262B containers, #2A2E33 borders).
- **Monospace type throughout.** Reinforces the tool aesthetic and ensures values, labels, and code all align cleanly. Sans-serif body text would feel like a website.

**Domain preview treatments:**

- **GPU nodes:** the texture IS the preview. A full-bleed live thumbnail fills the node body. This is the dominant visual element — you see the output of every processing step.
- **Audio nodes:** waveform display (time domain), spectrum analyzer (frequency domain), and a thin level meter strip. You "see the sound" through its visual signatures. Waveform and spectrum update in real time.
- **Control nodes:** compact data display. Current value in large type, sparkline showing recent history, small metadata (frequency, channel, etc.). Intentionally smaller than GPU/Audio nodes — control data is compact by nature.

**Interface chrome:**

- **Workspace grid.** A subtle grid underlays the node graph — very low opacity, in the GPU accent color. Provides structure and snap targets without visual noise.
- **Wires.** Thin (1px), in the domain color of the source port, low opacity (40%). Cross-domain wires (Control→GPU, Control→Audio) are dashed to indicate the bridge crossing. Wires should never visually compete with node content.
- **Inspector.** Dark background, parameters as horizontal rows. Slider tracks are dark with a domain-colored fill. Modulation range overlays (Bitwig-inspired) appear as subtle highlights showing the modulated range. Modulation source is indicated by a small tag next to the parameter.
- **Transport bar.** Minimal. Beat position as filled/unfilled dots. BPM as a number. Current state name. No unnecessary decoration.

**What this is NOT:**

- Not soft or rounded — sharp corners, no border-radius, no blur effects on chrome
- Not colorful — the three domain colors are the only chromatic accents against neutral gray
- Not decorative — every visual element serves a functional purpose
- Not MaxMSP — not esoteric or diagrammatic, the live content dominates over the wiring
- Not Notch — no irregular node shapes, no visual complexity in the containers themselves

---

## 7. Roadmap

The original 25-phase roadmap has been superseded by milestone-based planning in `docs/ROADMAP.md`. See the roadmap's "Shipped" section for the full list of delivered capabilities.

**Completed highlights:** Three-domain data flow, Spreads, hot-reload, 71 operators across 3 domains, Python MCP bridge (57 tools), MIDI/OSC input, data-driven WGSL filter framework, package ecosystem (install/link/scaffold/publish/test), movie playback (MovieLoaded trio), standalone export, operator versioning, first-class GPU port types (buffer/mesh/compute), multiple output ports, output analyzer (audio/visual/AV metrics + comparison), capture/recording, variations/presets, undo/redo, introspection/diagnostics/checks. North Star validation completed (see `docs/internal/NORTH-STAR-VALIDATION.md`).

**In progress:** Core stability verification (M1 exit gate), operator creation modal (M11), solo mode (M12), semantic tag rollout (M13), launch prep (M14).

**Deferred past 1.0:** Subpatches, simulation zones, multi-window, Windows/Linux, bundled compiler, WebSocket API, built-in chat panel, live REPL, parameter space explorer, pattern algebra interface, state machine interface.

### The North Star Demo

You open Vivid and add a Clock operator. You ask the LLM to generate a chord progression — it scaffolds a MIDI pattern node. You connect the Clock to the pattern, add a Polysynth, and immediately hear chords playing. You plug in a MIDI controller, map knobs to synth parameters, and experiment with different timbres. You add an LFO to automate one parameter, and an Envelope operator for per-note amplitude shaping. Then you create a Spread of rectangles on screen and connect the Polysynth's per-voice envelope output to the rectangle colors. The result: you hear chords and see rectangles changing color in sync with the music.

This scenario exercises every layer of Vivid's architecture: three-domain data flow, Spreads, cross-domain bridges, MIDI input, polyphonic audio, LLM-assisted operator creation, and audio-driven visuals.

---

## 8. Open Questions

**Cross-Domain Connection UX**

When a user drags a wire between nodes in different domains, should the system block and offer to insert a bridge node (explicit), auto-insert a visually distinct bridge node (semi-explicit), or silently handle bridging (implicit)? To be resolved during prototyping.

> **Resolved:** Cross-domain connections are implicit. The runtime handles bridging automatically based on port types and semantic tags. No explicit bridge nodes are inserted.

**Semantic Tag Depth**

How many semantic tags to define initially, and whether to formalize a standard set or let it grow organically as operators are built.

**GPU Operator Model**

Whether GPU operators are primarily C++ host code dispatching compute shaders / WGSL, or C++ all the way down. Affects the operator API and what the build system compiles.

> **Resolved:** GPU operators are C++ host code (`GpuOperatorBase::process_gpu()`) dispatching WGSL shaders via WebGPU. Additionally, a data-driven WGSL filter framework allows pure-WGSL filters with no C++ code (see ARCHITECTURE.md §5.18).

**Graph Serialization Format**

The declarative graph representation enabling LLM-driven patching. Needs to capture node types, connections, parameter values, and semantic tags. This is the single source of truth for the entire system.

> **Resolved:** JSON with `schema_version`, `vivid_version`, `meta` block, `nodes`, `connections`, and optional `variations`/`midi_mappings`/`filters` arrays. See ARCHITECTURE.md §5.11.

**Control Operator Sufficiency**

The decision to handle all automation and logic as visible Control operators (no scripting layer) requires a sufficient set of seed control operators for the LLM to use as examples and building blocks. What is the minimum viable seed set? LFO, Clock, Math, and Envelope are likely sufficient — the LLM generates specialized control operators (Sequencer, Pattern, Gate, Random, Smooth/Lerp) on demand when needed. The question is whether any common automation patterns are awkward to express as node graphs even with LLM-generated operators.

> **Resolved:** 20+ core control operators shipped including LFO, Clock, Math, Envelope, Gate, Random, Smooth, MIDI Input, OSC In/Out, Keyboard, Mouse, Logic, FFT Analysis, Stack, Alternate, StepCounter, and more. Specialized operators (Euclidean, PatTransform, Sequencer, PhaseToMidi) live in the `vivid-sequencers` package; audio DSP operators (GranularSynth, Vocoder, SpectralFreeze) live in `vivid-sampler`.

**WebSocket API Scope**

What mutations should the WebSocket API support? Parameter changes only, or full graph topology changes (adding/removing nodes, connections)? How to handle conflicts between WebSocket mutations and direct UI manipulation?

**Audio/Visual Session Grid Interaction**

The exact UX for the exploration strategies — how audio columns, visual columns, and mapping columns behave differently within the same grid widget.

---

## 9. LLM Perception System

The LLM cannot see the screen. When it generates a graph, adjusts parameters, or scaffolds operators, it works blind unless it has structured instruments that turn pixels and waveforms into numbers it can reason about. The perception system is what closes the loop between LLM generation and creative quality.

### 9.1 The Perception Loop

LLM-assisted development in Vivid follows a feedback cycle: capture the current output (frame, audio buffer, or both), extract structured metrics, evaluate whether the result matches intent, modify the graph or parameters, and capture again to verify. This loop is the runtime equivalent of a human watching the screen while turning knobs. Without it, the LLM's role collapses from "collaborator" to "one-shot generator."

### 9.2 Three Perception Layers

**Layer 1: Introspection**

The LLM's eyes. Structured readout of what the graph is actually producing at every point in the processing chain.

- **Per-node output analysis:** for every GPU node, extract texture metrics (brightness, contrast, entropy, edge density, color temperature, clipping). For every Audio node, extract signal metrics (RMS, spectrum, crest factor, onset density, LUFS). For Control nodes, current values and recent history.
- **Chain tracing:** when the final output has a problem (too dark, clipping, frozen), the LLM inspects metrics at each node in the upstream chain to find where the problem originates. If brightness is healthy at node 3 and gone at node 4, node 4 is the culprit.
- **Solo mode:** isolate any node's output, bypassing everything downstream. In a graph (not a chain), this means rendering the selected node and its upstream dependencies only.
- **Performance metrics:** per-node timing, GPU memory, audio thread load. Identifies bottlenecks.

**Layer 2: Analysis**

The LLM's judgment. Higher-level evaluation that goes beyond raw metrics to assess perceptual and aesthetic quality.

> **1.0 Status — what shipped:** The output analyzer (`src/runtime/output_analyzer.cpp`) implements: audio metrics (RMS, peak, spectral centroid, spectral brightness, spectral flatness), visual metrics (mean brightness, contrast, motion magnitude), AV reactivity (energy-brightness correlation over configurable time windows), and structured comparison with direction-aware semantic labels (louder/quieter, brighter/darker, more_motion/less_motion, more_reactive/less_reactive). Available via `analyze_output` and `compare_outputs` HTTP endpoints.
>
> **What remains aspirational:** Color harmony scoring, symmetry measurement, spatial balance, EBU R128 loudness compliance, pitch detection, stereo imaging, onset response rate, reactivity latency, per-band correlation, and A/B parameter sweeps. These are post-1.0 analysis enhancements.

- **Visual analysis:** color harmony scoring (complementary, analogous, triadic), bilateral and rotational symmetry measurement, spatial balance (rule of thirds, center of mass, quadrant distribution).
- **Audio analysis:** loudness standards compliance (EBU R128), spectral character (brightness, flatness, rolloff), dynamic range, pitch detection, stereo imaging.
- **Audio-visual reactivity:** this is core to Vivid's thesis. Measures how well visuals respond to audio: correlation between audio energy and visual brightness/motion, onset response rate (what fraction of beats produce visual change), reactivity latency (how many milliseconds between an audio event and the visual response), per-band correlation (does bass drive one thing and treble drive another).
- **Comparison tools:** A/B frame comparison (semantic diffs: brightness change, contrast change, sharpness change), A/B audio comparison (spectral diff, loudness diff), parameter sweeps (capture output across a parameter range to find optimal values).

**Layer 3: Assertions**

The LLM's memory of intent. Codified quality gates that persist across sessions and can be checked automatically.

Assertions are JSON declarations that bind a metric path to a comparison: "output brightness must be between 0.2 and 0.8," "audio RMS must be above 0.01," "AV onset response rate must exceed 0.5." They serve multiple purposes:

- **CI/CD:** run the graph headlessly and validate that all assertions pass. Prevents regressions when operators are modified.
- **Intent preservation:** when the user says "make it brighter," the LLM can add an assertion that brightness stays above a threshold. Future changes that violate this assertion are flagged.
- **Installation monitoring:** for long-running installations, assertions detect drift (frozen output, silence, loss of audio reactivity) and alert or trigger recovery.
- **Conditional assertions:** guards allow assertions to apply only when relevant. "Bass energy should be high, but only when the kick operator is active."

### 9.3 Temporal and Cross-Domain Metrics

Single-frame analysis is insufficient for a real-time system. The perception system must also measure temporal behavior (is the animation frozen? is there unwanted flicker? has a feedback loop converged or diverged? is the output looping?) and cross-domain relationships (does visual motion correlate with audio energy? how much latency exists between an audio onset and the visual response?).

These temporal and cross-domain metrics require multi-sample capture: the system records output over a time window (typically 1–3 seconds) and computes statistics across the sample set. This is more expensive than single-frame analysis and is triggered on demand rather than running continuously.

### 9.4 Design Principle

The perception system is not a debugger bolted onto the side. It is a core architectural component — the mechanism through which the LLM iterates on creative output. Every operator should expose metrics. Every domain bridge should be measurable. The JSON graph format should support assertion definitions alongside node and connection definitions. When the perception system works well, the LLM becomes a genuine collaborator: it can see what it built, evaluate whether it's good, and fix what's wrong.

---

## 10. To Be Determined

The following topics are acknowledged as important but are not yet designed. They will be addressed as the project progresses, most during or after Phase 1 implementation.

**Subpatches**

Equivalent to TouchDesigner's Bases or Max/MSP's subpatchers. A subpatch collapses a group of operators into a single node with defined inputs and outputs. Essential for managing graph complexity, enabling operator reuse, and supporting LLM scaffolding (the LLM generates a subpatch, not a flat graph). The design depends on how Spreads and Simulation Zones interact with encapsulation boundaries. A subpatch may also serve as the unit of session grid variation — swapping a cell swaps a subpatch.

**Project File Format**

Is a Vivid project a single .json file, or a directory containing the graph JSON, operator source files, assets (textures, audio samples), and assertion definitions? How does save/load work? How are assets referenced — absolute paths, relative paths, or embedded?

**Performance Targets**

60fps at what node count? Audio at what buffer size / sample rate? Maximum acceptable hot-reload time? GPU memory budget? These become acceptance criteria for Phase 1.

**Error Handling and Recovery**

What happens when an operator's hot-reload fails to compile? When an operator segfaults? When an audio operator misses its deadline? When a GPU shader fails validation? The graph must keep running. Strategies: per-operator error isolation, fallback to last-known-good, visual error indicators on failed nodes, audio silence on missed deadlines.

> **Largely resolved:** `crash_guard.h` provides per-operator crash isolation. Shader compilation errors fall back to last-known-good pipeline with visual error indicators. Hot-reload compilation failures keep the previous .dylib loaded. Audio operators that miss deadlines produce silence.

**Spread Visual Representation**

How do Spreads appear in the graph? Wire thickness proportional to cardinality? A small badge showing count? Color intensity? How does the user know they're looking at a Spread of 512 vs. a Spread of 1? This is a UX design problem to be resolved through prototyping.

> **Resolved:** Spread wires display a small badge showing the spread count. The inspector shows spread data as a list of values. Wire thickness does not vary by cardinality.

**Simulation Zone Visual Representation**

Whether Simulation Zones are a visible bounding box around grouped nodes (like Blender) or a single Feedback operator with an internal graph (like a subpatch). The former is more explicit and inspectable; the latter is simpler for the JSON schema.

**Accessibility**

Keyboard navigation, screen reader support, high-contrast mode. Important but not blocking Phase 1.

**Multi-Window / Multi-Monitor**

Output preview undocking to a separate window for projector/LED wall output. Multiple graph views. Phase 2 concern.

**Library Version Pinning**

Should the project record which library versions were used? A lockfile (`vivid-lock.json`) that pins `awesome-particles@0.2.0` would ensure reproducibility, especially for installations that need to rebuild months later. Alternatively, the project could simply vendor library source into its own directory. The tradeoff is reproducibility vs. simplicity.
