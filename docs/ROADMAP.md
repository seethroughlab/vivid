# Roadmap

Every phase is a checkpoint: it either works or it doesn't. Each phase ends with a concrete verification — something you can see, hear, or use. Phases are small enough to complete in days to a couple weeks, not months. If something is going to blow up, you find out early.

The phases are grouped into tiers. Each tier represents a qualitative shift in what Vivid can do.

### The North Star Demo

Every architectural decision in Vivid is evaluated against this scenario:

> You open Vivid and add a Clock operator. You ask the LLM to generate a chord progression — it scaffolds a MIDI pattern node. You connect the Clock to the pattern, add a Polysynth, and immediately hear chords playing. You plug in a MIDI controller, map knobs to synth parameters, and experiment with different timbres. You add an LFO to automate one parameter, and an Envelope operator for per-note amplitude shaping. Then you create a Spread of rectangles on screen and connect the Polysynth's per-voice envelope output to the rectangle colors. The result: you hear chords and see rectangles changing color in sync with the music.

This scenario exercises every layer of Vivid's architecture: three-domain data flow, Spreads, cross-domain bridges, MIDI input, polyphonic audio, LLM-assisted operator creation, and audio-driven visuals. The roadmap builds toward it piece by piece. By the end of Tier 4, every component is in place.

---

## Tier 1: Can It Run?

The goal is to validate every risky integration before building anything on top. If Dawn doesn't link, or operator dylibs crash on dlopen, or audio clicks and pops — you want to know now, not after building a UI.

### Phase 1: Window — DONE

Set up CMake, vendor dependencies (Dawn, GLFW, miniaudio, stb, yyjson), create a GLFW window with a Metal surface via Dawn.

**Verify:** `cmake --build build && ./build/vivid` opens a window cleared to `#16191D`. Nothing else.

**Why it matters:** Validates the build system and Dawn integration — CMake finding Dawn's pre-built libraries, GLFW creating a Metal surface, Dawn rendering to it. Everything downstream depends on this working.

**Implementation notes:**
- Started in Zig (`3419be3`), restarted in C++ (`3914775`) — Zig's WebGPU bindings were too immature
- Dawn via eliemichel's WebGPU-distribution (CMake FetchContent, pre-built binaries), not Google's Dawn repo directly
- GLFW added as a git submodule; `glfw3webgpu` bridge library handles Metal surface creation
- Window clears to `#16191D` as specified

### Phase 2: First Operator — DONE

Define the operator contract (`operator.h`, `types.h`). Build a single Control operator (LFO) as a .dylib. Load it via dlopen, read its parameter metadata, call its process function.

**Verify:** Runtime loads `lfo.dylib`, prints parameter descriptors, ticks the LFO, prints oscillating values to stdout.

**Why it matters:** Validates the hot-reload architecture — compiling a single .cpp to .dylib, loading it at runtime via `extern "C"` entry points, and calling across the dlopen boundary without crashing. This is the foundation for every operator.

**Implementation notes:**
- `operator.h` defines `OperatorBase` with `collect_params()`, `collect_ports()`, `process()` virtual methods
- `VIVID_REGISTER(ClassName)` macro generates `extern "C"` entry points for dlopen loading
- `Param<float>`, `Param<int>`, `Param<bool>` typed wrappers over `ParamBase` for type-safe parameter declaration
- LFO built as `operators/control/lfo/lfo.cpp` → compiled to `lfo.dylib`
- Commit: `2ecc4df`

### Phase 3: Graph + Data Flow — DONE

Build the graph loader (yyjson), parameter store, and a synchronous control-domain scheduler that evaluates nodes in topological order. Add Clock and Math operators.

**Verify:** Load a JSON graph with Clock → Math ← LFO. Scheduler ticks. Values flow through connections. Printed to stdout: you can see the Math operator combining Clock and LFO outputs correctly.

**Why it matters:** This is the first time data flows through the system the way the architecture describes. The JSON graph schema is real — not a spec in a document but a file the runtime actually reads and executes. Every feature from here forward is built on this graph.

**Implementation notes:**
- `Graph` class loads JSON via yyjson, stores nodes and connections
- `Scheduler` performs topological sort, ticks control-domain nodes in dependency order
- Generation-based cooking: nodes skip re-processing when inputs haven't changed
- Clock, Math, LFO operators all working
- Demo graph: `graphs/demo.json` (Clock → LFO → Noise scale param)
- Commit: `3e1c17e`

*North Star progress: Clock operator works. Graph connections work. 2 of 8 pieces.*

---

## Tier 2: Can You See and Hear It?

The runtime works. Now make it produce output a human can perceive.

### Phase 4: GPU Rendering — DONE

Build the GPU operator pipeline: runtime provides Dawn device/queue/output texture to operators. First GPU operator (Noise) with a WGSL shader. Fullscreen blit to the window.

**Verify:** Window shows animated procedural noise. A control value (hardcoded, not yet wired) changes the noise scale.

**Why it matters:** First visual output. Proves the GPU operator model works — C++ host code dispatching WGSL compute/render passes via Dawn's WebGPU API. Also validates that the window's render loop can present GPU operator output at frame rate.

**Implementation notes:**
- `GpuContext` wraps Dawn device/queue/surface/output texture
- `FullscreenBlit` renders operator output texture to the window
- Noise GPU operator: WGSL compute shader, writes to output texture
- Shape GPU operator: WGSL rendering of n-sided polygon/star shapes (bonus — not in original plan)
- GPU state passed to operators via `ctx->gpu` pointer (`VividGpuState`)
- Commit: `a23ac87`

### Phase 5: Control Drives Visuals — DONE

Wire the Control→GPU bridge: control-domain parameter changes are picked up by the GPU render loop on the next frame. Connect the LFO from Phase 2 to the Noise operator's scale parameter via the graph JSON.

**Verify:** Load a graph with LFO → Noise. The noise visibly oscillates — smooth, no stuttering. Change the LFO frequency in the JSON, reload, oscillation speed changes.

**Why it matters:** First cross-domain data flow. This is the architectural thesis in miniature: a control signal driving a visual parameter through the graph, with the JSON as the single source of truth. If the bridge introduces stutter or latency, the parameter store design needs revision.

**Implementation notes:**
- Wires can target params (not just input ports) via `Wire::targets_param` flag
- Connection resolution: if target port name matches a param name (not an input port), wire writes to `param_values[]`
- `graphs/demo.json`: LFO → Noise/scale works as a cross-domain param wire
- Commit: `18b2db5`

*North Star progress: LFO → visual parameter works. This is the same path that will later drive rectangle color from envelope values.*

### Phase 6: Audio Output — DONE

Build the audio context (miniaudio device, 48kHz, 256 samples), pull-based audio scheduling, and the Control→Audio bridge (double-buffered parameter snapshot). First audio operators: Oscillator and Gain.

**Verify:** Sine tone plays through speakers. Gain is controllable. Change oscillator frequency via JSON reload — pitch changes.

**Why it matters:** Audio threading is where most creative coding frameworks accumulate bugs. The lock-free bridge between control and audio must work under real conditions — no clicks, no pops, no blocking. Test with a small buffer (64 samples) to surface races early.

**Implementation notes:**
- `AudioEngine` class wraps miniaudio device (48kHz, 256 frames/buffer)
- Pull-based audio callback runs on a dedicated thread
- Double-buffered `ParamSnapshot` for lock-free control→audio parameter bridge
- `CrossDomainWire` maps control output ports to audio param indices
- Scheduler skips audio-domain nodes during control tick (they run on the audio thread)
- Audio operators: Oscillator (sine/saw/square/tri), Gain, Drum (percussive synth), Envelope (control-rate)
- Demo graphs: `graphs/audio_demo.json` (Oscillator → Gain), `graphs/av_sync_demo.json` (Clock → Envelope → Shape/radius + Clock → Drum/phase)
- Note: the AV sync demo uses Clock as a shared trigger — true audio→control feedback (Phase 7) is not yet built
- Commits: `eceb89b`, `3a664f7`

### Phase 7: Audio Drives Visuals

Build the Audio→Control bridge (lock-free queue). Audio operators write analysis results (RMS level) to the control domain. Wire audio RMS to a visual parameter.

**Verify:** Audio plays. Noise brightness responds to audio volume in real time. Turn audio input up → visuals get brighter. Visible correlation, low latency.

**Why it matters:** This is Vivid's thesis: audio and visuals in the same graph, driven by the same data. Phase 7 is the first time you can point at the screen and say "the visuals are responding to the audio." It's also the first round-trip through all three domains: Audio → Control → GPU.

*North Star progress: Audio→Control→GPU pipeline proven. The bridge that will carry envelope Spreads to rectangle colors works for scalar values.*

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

### Phase 10: MIDI Input

Build the MIDI input operator using RtMidi (or platform-native CoreMIDI wrapper). MIDI CC messages become Control-domain values. MIDI note-on/note-off become Control events. Device enumeration and selection via parameter. CC learn mode: wiggle a physical knob, the operator auto-assigns the CC number to a named output port.

**Verify:** Plug in a MIDI controller. Add a MidiInput operator via REPL. Map a CC knob to the LFO frequency — turn the physical knob, LFO speed changes, visuals respond. Press a MIDI key — note-on event prints to stdout. CC learn: wiggle knob → auto-maps.

**Why it matters:** MIDI is how hardware enters the Vivid graph. Without it, the system is screen-only. This phase validates that external hardware events flow through the same Control domain as internal operators — a MIDI CC driving a shader parameter is the same gesture as an LFO driving it. CC learn is essential for live performance: you shouldn't need to look up CC numbers.

*North Star progress: MIDI controller → parameter mapping works. You can now plug in hardware and tweak synth parameters by hand.*

### Phase 11: Minimal UI — Node Graph + Inspector

Retained-mode UI renderer (Dawn/WebGPU draw calls), text rendering (stb_truetype, monospace, 2–3 sizes), GLFW input dispatch to widget tree. Node graph view: rectangles with labels, lines for connections. Click a node → inspector panel shows parameter sliders. Drag a slider → parameter changes instantly.

**Scope boundary:** The UI is a viewer and parameter tweaker. No draggable nodes, no interactive wire creation, no context menus, no undo/redo. Structural changes still happen via REPL or JSON. This boundary is deliberate — a usable viewer is achievable; a full graph editor is a multi-month project.

**Verify:** Window shows the node graph laid out with live labels. Click any node → inspector appears with sliders for all parameters. Drag slider → output changes in real time.

**Why it matters:** This is the first time Vivid looks like a creative tool instead of a terminal program. Even without interactive graph editing, seeing the node layout and tweaking parameters via sliders is a qualitatively different experience from typing REPL commands. The retained-mode renderer built here is the foundation for every future UI element (patchbay, session grid, chat panel).

### Phase 12: Live Thumbnails

GPU texture thumbnails on every GPU node in the graph view (zero-copy blit — same Dawn context). Audio waveform rendering on audio nodes. Control nodes show current value + sparkline.

**Verify:** Every node in the graph shows its live output: GPU nodes show textures, audio nodes show waveforms, control nodes show values. The "See Every Step" principle is now real.

**Why it matters:** Thumbnails are why the UI is native rather than web-based. They're the payoff for the architectural decision in §6.1 — zero-copy GPU texture blitting with no readback, no encoding, no transport. If 6+ thumbnails at frame rate cause performance problems, the on-hover fallback mode needs to be implemented. This is also what makes Vivid's graph view genuinely useful for debugging — you can see where a signal goes wrong by scanning the chain.

---

## Tier 4: Can It Do the Thing?

The tool works. Now make it do what no other tool does: audio-reactive visuals through Spreads, polyphonic audio, LLM-assisted creative exploration, and the North Star Demo.

### Phase 13: Spreads

Build the Spread type (contiguous array + length, broadcasting logic). FFT Analysis operator (512-bin, simple radix-2 in C++, no FFTW). Bars operator (GPU bar graph visualization sized by Spread input). Spread data crossing the Control→GPU bridge as a GPU storage buffer.

**Verify:** Audio input → FFT → 512 bins → bar heights. Real-time audio-reactive bars flowing through the graph, with Spreads carrying the data.

**Why it matters:** Spreads are the most impactful data model decision in the architecture. This phase proves they work end-to-end: an audio operator produces a Spread, it flows through the control domain, it crosses into the GPU domain, and it drives 512 independent visual elements. If broadcasting or the GPU storage buffer path has problems, you find out on a concrete, visually obvious test case. The Spread plumbing validated here is what Phase 15 will use for per-voice envelope → per-rectangle color.

### Phase 14: Polyphonic Audio

Build the operators that make Vivid a musical instrument, not just a signal processor:

- **NotePattern** — a Control operator that emits a Spread of note-on/note-off events on each tick. Takes a pattern description (chord progression, sequence, or arpeggio) as parameters. The LLM generates these parameter configurations; the operator interprets them.
- **Polysynth** — an Audio operator with voice allocation. Receives note events from Control domain. Internally manages N voices, each with its own oscillator and ADSR envelope. Mixes to stereo output. Exposes per-voice envelope values back to the Control domain as a Spread — this is the critical output that will drive visuals.
- **Envelope** — a standalone Control operator (ADSR) that can also be used independently of the Polysynth, for per-note or per-event amplitude shaping on any parameter.

**Verify:** Clock → NotePattern → Polysynth. Chord progression plays. Adjust attack/release parameters → timbre changes in real time. The Polysynth's envelope Spread output (one value per active voice) is visible in the REPL: `inspect polysynth1/envelopes` shows a Spread of 4 values rising and falling with each chord.

**Why it matters:** This is where Vivid stops being a demo and becomes a creative audio tool. Voice allocation, per-voice state, and dynamic-length Spreads are the hardest test of the architecture. If a polysynth with 8 voices can produce a Spread of 8 envelope values that flow cleanly through the Control domain at audio rate without glitches, the Spread system works for real musical use. If it can't, the architecture needs revision — better to find out now than after building the LLM integration on top.

*North Star progress: Clock → chord progression → Polysynth works. MIDI knobs control timbre. Envelope Spread is produced. 6 of 8 pieces.*

### Phase 15: The North Star Demo — Audio-Reactive Rectangles

Build the Rects GPU operator: takes a Spread of positions/sizes and a Spread of colors, draws filled rectangles via instanced rendering. Connect the Polysynth's envelope Spread to the color input. Add a layout function (grid, circle, random) as a parameter.

**Verify:** The full North Star scenario works end-to-end:

1. Clock → NotePattern → Polysynth → audio output (you hear chords)
2. Polysynth envelope Spread → Rects color input (rectangles change color with each note)
3. MIDI controller CC → Polysynth parameters (physical knobs change timbre)
4. LFO → Polysynth filter cutoff (automatic parameter modulation)
5. Standalone Envelope → Polysynth amplitude (per-note shaping)

You hear a chord progression. You see rectangles lighting up in sync. You turn a physical knob and the timbre shifts. The LFO sweeps a filter. The envelopes shape each note's attack. Audio and visuals are peers in the same graph.

**Why it matters:** This is the proof. Not a tech demo — a creative workflow. Every architectural decision (three domains, Spreads, cross-domain bridges, JSON graph, operator contract) is validated in a single session. If you can sit in front of this and make something that sounds and looks good by tweaking parameters and connections, Vivid works. If you can't, something fundamental needs to change. Building this before the LLM integration means the pipeline is solid when the chat panel wraps it.

### Phase 16: MCP Server

stdio JSON-RPC server (`vivid mcp` subcommand), exposing the Runtime API as MCP tools: `inspect_graph`, `set_param`, `add_node`, `remove_node`, `connect`, `disconnect`, `scaffold_operator`, `save_graph`, `load_graph`.

**Verify:** Claude Code connects via MCP. `inspect_graph` returns the full graph JSON. `set_param lfo1/frequency 8.0` changes the live output. `scaffold_operator gpu MyShader` creates the directory with boilerplate and opens it in the editor.

**Why it matters:** MCP is the bridge between Vivid and external LLM tools. Once this works, Claude Code can inspect and modify a running Vivid instance — which means you can develop operators and iterate on patches from your terminal. It also validates the Runtime API design under real external use: if the tool interface is awkward for Claude Code, it needs revision before the built-in chat wraps the same API.

### Phase 17: Built-in Chat Panel

Collapsible chat panel in the UI. HTTP client for Anthropic API (libcurl or curl subprocess). LLM receives graph JSON as context, Runtime API as tool definitions. Streaming response rendering in a scrollable text widget.

**Verify:** The North Star scenario, LLM-assisted: Type "make me a chord progression in C minor" → LLM creates a NotePattern node with the right parameters and connects it to the Clock. Type "add a polysynth" → LLM adds and wires it. Type "now make some rectangles that light up with the music" → LLM adds Rects, connects envelope Spread to color. The entire demo, built conversationally.

**Why it matters:** This is the primary creative workflow: the user stays in Vivid, describes intent in natural language, and the LLM modifies the graph. The North Star Demo built by hand in Phase 15 can now be built by conversation in Phase 17. The delta between those two experiences — manual wiring vs. "make me a chord progression" — is what makes Vivid's LLM integration meaningful rather than a novelty.

---

## Tier 5: Experimentation Interfaces

The core tool is complete. Now build the interfaces that make Vivid's exploration model unique.

### Phase 18: Patchbay / Connection Matrix

Matrix view where rows are outputs and columns are inputs. Click an intersection to create/remove a connection. Each intersection can hold a scaling factor. LLM generates matrix configurations ("8 variations from subtle to aggressive").

**Verify:** Switch to patchbay view. Audio analysis outputs on rows, visual parameters on columns. Click intersections to route. See visual changes in real time. Ask LLM to "fill the matrix with an interesting configuration."

**Why it matters:** The patchbay is the highest-discovery interface — N×M possible connections explored spatially rather than by dragging wires one at a time. It's the first interface that's genuinely unique to Vivid (no other tool has a cross-domain connection matrix). It also validates the "multiple views of the same graph" architecture — patchbay and node graph must stay synchronized.

### Phase 19: Session / Variation Grid

Grid where columns are parameter snapshots (complete graph configurations). Click a column to switch. LLM populates rows with variations. Drag to reorder. A/B comparison between columns.

**Verify:** Save current state as column A. Ask LLM to "generate 4 variations." Click through columns — output changes instantly. Reorder. The creative workflow of "explore a space of possibilities" is now real.

**Why it matters:** The session grid is what makes LLM-generated variation useful. Without it, the LLM generates one thing at a time and the user evaluates sequentially. With it, the user sees a field of options and navigates spatially — the core innovation described in the vision statement.

### Phase 20: Pattern Algebra

Composable pattern operators in the control domain (not a DSL — patterns are Control operators that emit time-varying sequences). Pattern combinators: sequence, stack, alternate, euclidean, transform. Patterns bindable to any parameter.

**Verify:** Create a pattern that alternates two values on the beat. Apply it to particle size. Combine two patterns (polyrhythm). The output has rhythmic, musical structure driven by pattern composition.

**Why it matters:** Patterns are what make Vivid temporal — not just reactive to live input, but generative of its own rhythmic behavior. They're extremely LLM-friendly (short, composable, text-representable) and connect directly to the TidalCycles and Strudel communities' insight that pattern composition is a creative medium.

### Phase 21: State Machines

Named states (intro, build, drop, ambient) with per-state parameter configurations. Transition conditions (manual trigger, clock-driven, threshold-based). Crossfade and hard-cut transition modes.

**Verify:** Define 3 states with different visual/audio character. Trigger transitions manually and via clock. The output has macro-level structure — not just reactive variation but composed arc.

**Why it matters:** State machines are how a Vivid piece becomes a *piece* — with structure, narrative, and arc. They're also the mechanism for installation scenarios (idle state → active state on sensor trigger → cool-down state → idle). Combined with the session grid, they allow composing complete performances.

---

## Tier 6: Production Readiness

### Phase 22: Export / Standalone Builds

Static linking of operators into a single binary. Tree-shaking (only compile referenced operators). Graph JSON embedded as compile-time resource. Headless mode for LED wall / projection servers.

### Phase 23: Operator Library System

`vivid install github.com/user/library` → clone → compile from source. Library manifest. Template repo. Search path resolution (project-local → user global → libraries → built-in).

### Phase 24: LLM Perception System

Full perception loop: per-node introspection (texture metrics, audio metrics), analysis tools (color harmony, AV reactivity, parameter sweeps), assertions (JSON quality gates, CI/CD validation, installation monitoring).

### Phase 25: WebSocket API

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
- **OSC input** — same pattern as MIDI Input (Phase 10), deferred because MIDI is more immediately useful for the North Star scenario

---

## The Guiding Principle

The LLM populates the exploration space; the user navigates it. Every architectural decision should be evaluated against this principle. If a decision makes it harder for the LLM to generate options, or harder for the user to evaluate and combine them in real time, it's the wrong decision.
