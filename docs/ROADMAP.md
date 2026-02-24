# Roadmap

Every phase is a checkpoint: it either works or it doesn't. Each phase ends with a concrete verification — something you can see, hear, or use. Phases are small enough to complete in days to a couple weeks, not months. If something is going to blow up, you find out early.

The phases are grouped into tiers. Each tier represents a qualitative shift in what Vivid can do.

---

## Tier 1: Can It Run?

The goal is to validate every risky integration before building anything on top. If Dawn doesn't link, or operator dylibs crash on dlopen, or audio clicks and pops — you want to know now, not after building a UI.

### Phase 1: Window ✓

Set up CMake, vendor dependencies (Dawn, GLFW, miniaudio, stb, yyjson), create a GLFW window with a Metal surface via Dawn.

**Verify:** `cmake --build build && ./build/vivid` opens a window cleared to `#16191D`. Nothing else.

**Why it matters:** Validates the build system and Dawn integration — CMake finding Dawn's pre-built libraries, GLFW creating a Metal surface, Dawn rendering to it. Everything downstream depends on this working.

<details>
<summary><strong>Implementation Notes</strong></summary>

**WebGPU backend:** Uses [eliemichel/WebGPU-distribution](https://github.com/eliemichel/WebGPU-distribution) via FetchContent (`main` branch), which provides **wgpu-native v24** pre-built binaries — not Dawn. wgpu-native implements the same WebGPU C API over Metal on macOS. The pre-built distribution avoids a 30+ minute Dawn source build.

**API version:** wgpu-native v24 uses the 2024+ WebGPU C spec, which differs from older tutorials:
- Labels are `WGPUStringView` (struct with `data` + `length`), not `const char*`
- Adapter/device requests use `WGPURequestAdapterCallbackInfo` / `WGPURequestDeviceCallbackInfo` structs with `WGPUCallbackMode`, returning `WGPUFuture`
- `wgpuSurfaceGetPreferredFormat` does not exist — use `wgpuSurfaceGetCapabilities` instead
- Error/device-lost callbacks are set via fields on `WGPUDeviceDescriptor` (`deviceLostCallbackInfo`, `uncapturedErrorCallbackInfo`), not standalone setters
- Surface texture status uses `SuccessOptimal` / `SuccessSuboptimal` (no plain `Success`)
- `wgpuDevicePoll` (in `webgpu/wgpu.h` extension header) replaces Dawn's `wgpuDeviceTick`

**Dependencies (Phase 1 only):**
- GLFW 3.4 — git submodule at `deps/glfw`
- glfw3webgpu — git submodule at `deps/glfw3webgpu`, provides `glfwCreateWindowWGPUSurface()`
- miniaudio, stb, yyjson deferred to later phases

**Surface format:** Reports `BGRA8Unorm` (format 24) on macOS/Apple Silicon. Clear color uses raw unorm values (`0.0863, 0.0980, 0.1137`); sRGB formats would use linearized values instead.

</details>

### Phase 2: First Operator

Define the operator contract (`operator.h`, `types.h`). Build a single Control operator (LFO) as a .dylib. Load it via dlopen, read its parameter metadata, call its process function.

**Verify:** Runtime loads `lfo.dylib`, prints parameter descriptors, ticks the LFO, prints oscillating values to stdout.

**Why it matters:** Validates the hot-reload architecture — compiling a single .cpp to .dylib, loading it at runtime via `extern "C"` entry points, and calling across the dlopen boundary without crashing. This is the foundation for every operator.

### Phase 3: Graph + Data Flow

Build the graph loader (yyjson), parameter store, and a synchronous control-domain scheduler that evaluates nodes in topological order. Add Clock and Math operators.

**Verify:** Load a JSON graph with Clock → Math ← LFO. Scheduler ticks. Values flow through connections. Printed to stdout: you can see the Math operator combining Clock and LFO outputs correctly.

**Why it matters:** This is the first time data flows through the system the way the architecture describes. The JSON graph schema is real — not a spec in a document but a file the runtime actually reads and executes. Every feature from here forward is built on this graph.

---

## Tier 2: Can You See and Hear It?

The runtime works. Now make it produce output a human can perceive.

### Phase 4: GPU Rendering

Build the GPU operator pipeline: runtime provides Dawn device/queue/output texture to operators. First GPU operator (Noise) with a WGSL shader. Fullscreen blit to the window.

**Verify:** Window shows animated procedural noise. A control value (hardcoded, not yet wired) changes the noise scale.

**Why it matters:** First visual output. Proves the GPU operator model works — C++ host code dispatching WGSL compute/render passes via Dawn's WebGPU API. Also validates that the window's render loop can present GPU operator output at frame rate.

### Phase 5: Control Drives Visuals

Wire the Control→GPU bridge: control-domain parameter changes are picked up by the GPU render loop on the next frame. Connect the LFO from Phase 2 to the Noise operator's scale parameter via the graph JSON.

**Verify:** Load a graph with LFO → Noise. The noise visibly oscillates — smooth, no stuttering. Change the LFO frequency in the JSON, reload, oscillation speed changes.

**Why it matters:** First cross-domain data flow. This is the architectural thesis in miniature: a control signal driving a visual parameter through the graph, with the JSON as the single source of truth. If the bridge introduces stutter or latency, the parameter store design needs revision.

### Phase 6: Audio Output

Build the audio context (miniaudio device, 48kHz, 256 samples), pull-based audio scheduling, and the Control→Audio bridge (double-buffered parameter snapshot). First audio operators: Oscillator and Delay.

**Verify:** Sine tone plays through speakers. Delay is audible. Change oscillator frequency via JSON reload — pitch changes.

**Why it matters:** Audio threading is where most creative coding frameworks accumulate bugs. The lock-free bridge between control and audio must work under real conditions — no clicks, no pops, no blocking. Test with a small buffer (64 samples) to surface races early.

### Phase 7: Audio Drives Visuals

Build the Audio→Control bridge (lock-free queue). Audio operators write analysis results (RMS level) to the control domain. Wire audio RMS to a visual parameter.

**Verify:** Audio plays. Noise brightness responds to audio volume in real time. Turn audio input up → visuals get brighter. Visible correlation, low latency.

**Why it matters:** This is Vivid's thesis: audio and visuals in the same graph, driven by the same data. Phase 7 is the first time you can point at the screen and say "the visuals are responding to the audio." It's also the first round-trip through all three domains: Audio → Control → GPU.

---

## Tier 3: Can You Use It?

Output works. Now make it interactive — something you can actually work with, not just watch.

### Phase 8: Hot-Reload

File watching (kqueue) on operator directories. On save: recompile the changed operator via system clang, dlclose the old library, dlopen the new one, re-create the operator instance. Parameters survive (they live in the parameter store, outside the operator).

**Verify:** Edit noise.wgsl in your editor. Save. Within 1–3 seconds, the visual output changes. LFO frequency (a parameter) is unchanged. No crash, no restart.

**Why it matters:** Hot-reload is what makes the development loop tolerable. Without it, every operator change requires restarting the app and re-loading the graph. With it, you can iterate on shaders and DSP in real time. This also validates the architectural decision that parameters live outside operators.

### Phase 9: REPL

Text input at the bottom of the window (minimal: just a text field, not a full chat UI). Build the Runtime API as an internal C++ interface. REPL commands map to Runtime API calls. Topology changes buffered and applied between frames; parameter changes immediate.

**Verify:** `set lfo1/frequency 4.0` → instant pitch change. `add Blur blur1` + `connect noise1/output blur1/input` → blur appears in the output. `save` → JSON written to disk. `reload` → graph reloads from JSON.

**Why it matters:** The REPL is the first way to modify the running graph without editing JSON and restarting. It exercises the Runtime API that MCP and the chat panel will later wrap. Building it now means you can use the system productively while the UI is still being developed. It also proves the Runtime API design — if `addNode` or `connect` feel wrong from the REPL, they'll feel wrong from MCP too.

### Phase 10: Minimal UI — Node Graph + Inspector

Retained-mode UI renderer (Dawn/WebGPU draw calls), text rendering (stb_truetype, monospace, 2–3 sizes), GLFW input dispatch to widget tree. Node graph view: rectangles with labels, lines for connections. Click a node → inspector panel shows parameter sliders. Drag a slider → parameter changes instantly.

**Scope boundary:** The UI is a viewer and parameter tweaker. No draggable nodes, no interactive wire creation, no context menus, no undo/redo. Structural changes still happen via REPL or JSON. This boundary is deliberate — a usable viewer is achievable; a full graph editor is a multi-month project.

**Verify:** Window shows the node graph laid out with live labels. Click any node → inspector appears with sliders for all parameters. Drag slider → output changes in real time.

**Why it matters:** This is the first time Vivid looks like a creative tool instead of a terminal program. Even without interactive graph editing, seeing the node layout and tweaking parameters via sliders is a qualitatively different experience from typing REPL commands. The retained-mode renderer built here is the foundation for every future UI element (patchbay, session grid, chat panel).

### Phase 11: Live Thumbnails

GPU texture thumbnails on every GPU node in the graph view (zero-copy blit — same Dawn context). Audio waveform rendering on audio nodes. Control nodes show current value + sparkline.

**Verify:** Every node in the graph shows its live output: GPU nodes show textures, audio nodes show waveforms, control nodes show values. The "See Every Step" principle is now real.

**Why it matters:** Thumbnails are why the UI is native rather than web-based. They're the payoff for the architectural decision in §6.1 — zero-copy GPU texture blitting with no readback, no encoding, no transport. If 6+ thumbnails at frame rate cause performance problems, the on-hover fallback mode needs to be implemented. This is also what makes Vivid's graph view genuinely useful for debugging — you can see where a signal goes wrong by scanning the chain.

---

## Tier 4: Can It Do the Thing?

The tool works. Now make it do what no other tool does: audio-reactive visuals through Spreads, and LLM-assisted creative exploration.

### Phase 12: Spreads + FFT

Build the Spread type (contiguous array + length, broadcasting logic). FFT Analysis operator (512-bin, simple radix-2 in C++, no FFTW). Bars operator (GPU bar graph visualization sized by Spread input). Spread data crossing the Control→GPU bridge as a GPU storage buffer.

**Verify:** Audio input → FFT → 512 bins → bar heights. The canonical Vivid demo: real-time audio-reactive visuals flowing through the graph, with Spreads carrying the data.

**Why it matters:** Spreads are the most impactful data model decision in the architecture. This phase proves they work end-to-end: an audio operator produces a Spread, it flows through the control domain, it crosses into the GPU domain, and it drives 512 independent visual elements. If broadcasting or the GPU storage buffer path has problems, you find out on a concrete, visually obvious test case.

### Phase 13: MCP Server

stdio JSON-RPC server (`vivid mcp` subcommand), exposing the Runtime API as MCP tools: `inspect_graph`, `set_param`, `add_node`, `remove_node`, `connect`, `disconnect`, `scaffold_operator`, `save_graph`, `load_graph`.

**Verify:** Claude Code connects via MCP. `inspect_graph` returns the full graph JSON. `set_param lfo1/frequency 8.0` changes the live output. `scaffold_operator gpu MyShader` creates the directory with boilerplate and opens it in the editor.

**Why it matters:** MCP is the bridge between Vivid and external LLM tools. Once this works, Claude Code can inspect and modify a running Vivid instance — which means you can develop operators and iterate on patches from your terminal. It also validates the Runtime API design under real external use: if the tool interface is awkward for Claude Code, it needs revision before the built-in chat wraps the same API.

### Phase 14: Built-in Chat Panel

Collapsible chat panel in the UI. HTTP client for Anthropic API (libcurl or curl subprocess). LLM receives graph JSON as context, Runtime API as tool definitions. Streaming response rendering in a scrollable text widget.

**Verify:** Type "add a blur after noise" in the chat panel → LLM calls `add_node` + `connect` via tools → blur appears in the graph and output. Type "make the LFO faster" → LLM calls `set_param` → oscillation speed increases.

**Why it matters:** This is the primary creative workflow: the user stays in Vivid, describes intent in natural language, and the LLM modifies the graph. It's also the most complex UI widget (scrollable, mixed content, streaming text) — building it last means the retained-mode renderer and text system are already battle-tested from the inspector and REPL.

---

## Tier 5: Experimentation Interfaces

The core tool is complete. Now build the interfaces that make Vivid's exploration model unique.

### Phase 15: Patchbay / Connection Matrix

Matrix view where rows are outputs and columns are inputs. Click an intersection to create/remove a connection. Each intersection can hold a scaling factor. LLM generates matrix configurations ("8 variations from subtle to aggressive").

**Verify:** Switch to patchbay view. Audio analysis outputs on rows, visual parameters on columns. Click intersections to route. See visual changes in real time. Ask LLM to "fill the matrix with an interesting configuration."

**Why it matters:** The patchbay is the highest-discovery interface — N×M possible connections explored spatially rather than by dragging wires one at a time. It's the first interface that's genuinely unique to Vivid (no other tool has a cross-domain connection matrix). It also validates the "multiple views of the same graph" architecture — patchbay and node graph must stay synchronized.

### Phase 16: Session / Variation Grid

Grid where columns are parameter snapshots (complete graph configurations). Click a column to switch. LLM populates rows with variations. Drag to reorder. A/B comparison between columns.

**Verify:** Save current state as column A. Ask LLM to "generate 4 variations." Click through columns — output changes instantly. Reorder. The creative workflow of "explore a space of possibilities" is now real.

**Why it matters:** The session grid is what makes LLM-generated variation useful. Without it, the LLM generates one thing at a time and the user evaluates sequentially. With it, the user sees a field of options and navigates spatially — the core innovation described in the vision statement.

### Phase 17: Pattern Algebra

Composable pattern operators in the control domain (not a DSL — patterns are Control operators that emit time-varying sequences). Pattern combinators: sequence, stack, alternate, euclidean, transform. Patterns bindable to any parameter.

**Verify:** Create a pattern that alternates two values on the beat. Apply it to particle size. Combine two patterns (polyrhythm). The output has rhythmic, musical structure driven by pattern composition.

**Why it matters:** Patterns are what make Vivid temporal — not just reactive to live input, but generative of its own rhythmic behavior. They're extremely LLM-friendly (short, composable, text-representable) and connect directly to the TidalCycles and Strudel communities' insight that pattern composition is a creative medium.

### Phase 18: State Machines

Named states (intro, build, drop, ambient) with per-state parameter configurations. Transition conditions (manual trigger, clock-driven, threshold-based). Crossfade and hard-cut transition modes.

**Verify:** Define 3 states with different visual/audio character. Trigger transitions manually and via clock. The output has macro-level structure — not just reactive variation but composed arc.

**Why it matters:** State machines are how a Vivid piece becomes a *piece* — with structure, narrative, and arc. They're also the mechanism for installation scenarios (idle state → active state on sensor trigger → cool-down state → idle). Combined with the session grid, they allow composing complete performances.

---

## Tier 6: Production Readiness

### Phase 19: Export / Standalone Builds

Static linking of operators into a single binary. Tree-shaking (only compile referenced operators). Graph JSON embedded as compile-time resource. Headless mode for LED wall / projection servers.

### Phase 20: Operator Library System

`vivid install github.com/user/library` → clone → compile from source. Library manifest. Template repo. Search path resolution (project-local → user global → libraries → built-in).

### Phase 21: LLM Perception System

Full perception loop: per-node introspection (texture metrics, audio metrics), analysis tools (color harmony, AV reactivity, parameter sweeps), assertions (JSON quality gates, CI/CD validation, installation monitoring).

### Phase 22: WebSocket API

External process integration. Same Runtime API over WebSocket. Python, Max/MSP, show control systems can drive Vivid. OSC/MIDI bridge applications.

---

## Explicitly Deferred (No Phase Assigned)

These are acknowledged as important but depend on decisions that can only be made during implementation:

- **Subpatches** — depends on Spreads + Simulation Zones interaction
- **Simulation Zones** — frame-to-frame feedback, design depends on how GPU state management works in practice
- **Draggable graph editing** — full interactive node editor (create, move, wire, delete via mouse). Significant UX engineering; REPL + MCP handle structural changes until this is built
- **Multi-window / multi-monitor** — output undocking for projector/LED wall
- **Accessibility** — keyboard navigation, screen reader, high-contrast
- **Library version pinning** — lockfile vs vendoring
- **Project file format** — single JSON vs directory with assets
- **Bundled compiler** — optionally shipping a C++ compiler (e.g., Zig's `zig c++`) so users don't need Xcode CLI tools. Packaging problem, not architecture problem.

---

## The Guiding Principle

The LLM populates the exploration space; the user navigates it. Every architectural decision should be evaluated against this principle. If a decision makes it harder for the LLM to generate options, or harder for the user to evaluate and combine them in real time, it's the wrong decision.
