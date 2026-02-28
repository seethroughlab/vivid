# Roadmap

Every phase is a checkpoint: it either works or it doesn't. Each phase ends with a concrete verification — something you can see, hear, or use. Phases are small enough to complete in days to a couple weeks, not months. If something is going to blow up, you find out early.

The phases are grouped into tiers. Each tier represents a qualitative shift in what Vivid can do.

### The North Star Demo

Every architectural decision in Vivid is evaluated against this scenario:

> You open Vivid and add a Clock operator. You ask the LLM to generate a chord progression — it scaffolds a MIDI pattern node. You connect the Clock to the pattern, add a WavetableSynth, and immediately hear chords playing. You plug in a MIDI controller, map knobs to synth parameters, and experiment with different timbres. You add an LFO to automate one parameter, and an Envelope operator for per-note amplitude shaping. Then you create a Spread of rectangles on screen and connect the WavetableSynth's per-voice envelope output to the rectangle colors. The result: you hear chords and see rectangles changing color in sync with the music.

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

### Phase 7: Audio Drives Visuals — DONE

Build the Audio→Control bridge (lock-free queue). Audio operators write analysis results (RMS level) to the control domain. Wire audio RMS to a visual parameter.

**Verify:** Audio plays. Noise brightness responds to audio volume in real time. Turn audio input up → visuals get brighter. Visible correlation, low latency.

**Why it matters:** This is Vivid's thesis: audio and visuals in the same graph, driven by the same data. Phase 7 is the first time you can point at the screen and say "the visuals are responding to the audio." It's also the first round-trip through all three domains: Audio → Control → GPU.

**Implementation notes:**
- `AnalysisSnapshot` in `audio_engine.h` carries RMS and peak values from the audio thread
- Double-buffered analysis bridge: audio callback writes snapshots, `inject_analysis()` reads them into the scheduler on the main thread
- Waveform analysis mapped per-node via `AnalysisMapping` entries
- Demo graphs: `audio_demo.json`, `fft_bars_demo.json`
- Commit: `a537719`

*North Star progress: Audio→Control→GPU pipeline proven. The bridge that will carry envelope Spreads to rectangle colors works for scalar values.*

---

## Tier 3: Can You Use It?

Output works. Now make it interactive — something you can actually work with, not just watch.

### Phase 8: Hot-Reload — DONE

File watching (kqueue) on operator directories. On save: recompile the changed operator via system clang, dlclose the old library, dlopen the new one, re-create the operator instance. Parameters survive (they live in the parameter store, outside the operator).

**Verify:** Edit noise.wgsl in your editor. Save. Within 1–3 seconds, the visual output changes. LFO frequency (a parameter) is unchanged. No crash, no restart.

**Why it matters:** Hot-reload is what makes the development loop tolerable. Without it, every operator change requires restarting the app and re-loading the graph. With it, you can iterate on shaders and DSP in real time. This also validates the architectural decision that parameters live outside operators.

**Implementation notes:**
- `FileWatcher` (kqueue-based) watches all `.cpp`/`.h`/`.wgsl` files under `operators/`
- `HotReloader` spawns async clang builds, stages dylibs, reports completion
- Audio operators paused during reload, resumed after — no clicks/pops
- Scheduler's `reload_operator()` handles dlclose/dlopen and instance re-creation
- Commit: `b208118`

### Phase 9: REPL — DONE

Text input at the bottom of the window (minimal: just a text field, not a full chat UI). Build the Runtime API as an internal C++ interface. REPL commands map to Runtime API calls. Topology changes buffered and applied between frames; parameter changes immediate.

**Verify:** `set lfo1/frequency 4.0` → instant pitch change. `add Blur blur1` + `connect noise1/output blur1/input` → blur appears in the output. `save` → JSON written to disk. `reload` → graph reloads from JSON.

**Why it matters:** The REPL is the first way to modify the running graph without editing JSON and restarting. It exercises the Runtime API that MCP and the chat panel will later wrap. Building it now means you can use the system productively while the UI is still being developed. It also proves the Runtime API design — if `addNode` or `connect` feel wrong from the REPL, they'll feel wrong from MCP too.

**Implementation notes:**
- `Repl` class with text input, cursor, command history (up/down arrows)
- `RuntimeAPI` as internal C++ interface: `set_param()`, `add_node()`, `connect()`, `disconnect()`, `save()`, `reload()`
- Topology changes buffered via `has_pending()` / `apply_pending()`, applied between frames
- GPU text rendering via `TextRenderer` (stb_truetype, JetBrains Mono)
- Commit: `189d393`

### Phase 10: MIDI Input — DONE

Build the MIDI input operator using RtMidi (or platform-native CoreMIDI wrapper). MIDI CC messages become Control-domain values. MIDI note-on/note-off become Control events. Device enumeration and selection via parameter. CC learn mode: wiggle a physical knob, the operator auto-assigns the CC number to a named output port.

**Verify:** Plug in a MIDI controller. Add a MidiInput operator via REPL. Map a CC knob to the LFO frequency — turn the physical knob, LFO speed changes, visuals respond. Press a MIDI key — note-on event prints to stdout. CC learn: wiggle knob → auto-maps.

**Why it matters:** MIDI is how hardware enters the Vivid graph. Without it, the system is screen-only. This phase validates that external hardware events flow through the same Control domain as internal operators — a MIDI CC driving a shader parameter is the same gesture as an LFO driving it. CC learn is essential for live performance: you shouldn't need to look up CC numbers.

**Implementation notes:**
- `operators/control/midi_input/midi_input.cpp` using RtMidi
- Device enumeration at init; device selection via `device` parameter
- Output ports: note, velocity, gate, trigger, pitch_bend, mod_wheel, cc_value
- CC learn mode via `channel` and `cc_number` parameters
- Commit: `0e1a3f4`

*North Star progress: MIDI controller → parameter mapping works. You can now plug in hardware and tweak synth parameters by hand.*

### Phase 11: Minimal UI — Node Graph + Inspector — DONE

Retained-mode UI renderer (Dawn/WebGPU draw calls), text rendering (stb_truetype, monospace, 2–3 sizes), GLFW input dispatch to widget tree. Node graph view: rectangles with labels, lines for connections. Click a node → inspector panel shows parameter sliders. Drag a slider → parameter changes instantly.

**Scope boundary:** The UI is a viewer and parameter tweaker — no interactive wire creation, no context menus, no undo/redo. Structural changes still happen via REPL or JSON. (Draggable nodes, zoom/pan, and an operator chooser are added in Phases 12a–12c.) This boundary is deliberate — a usable viewer is achievable; a full graph editor is a multi-month project.

**Verify:** Window shows the node graph laid out with live labels. Click any node → inspector appears with sliders for all parameters. Drag slider → output changes in real time.

**Why it matters:** This is the first time Vivid looks like a creative tool instead of a terminal program. Even without interactive graph editing, seeing the node layout and tweaking parameters via sliders is a qualitatively different experience from typing REPL commands. The retained-mode renderer built here is the foundation for every future UI element (patchbay, session grid, chat panel).

**Implementation notes:**
- `NodeGraphUI` class in `node_graph.h/cpp` — retained-mode UI rendered via `TextRenderer`
- `TextRenderer` uses stb_truetype with JetBrains Mono, HiDPI-aware glyph atlas (1024x1024)
- Inspector panel: click node to select, shows all parameters with draggable sliders
- Sugiyama-style auto-layout for initial node placement
- Domain-colored accent bars and wires (orange=audio, cyan=control, teal=GPU)
- Commit: `2fdbf98`

### Phase 12: Live Thumbnails — DONE

GPU texture thumbnails on every GPU node in the graph view (zero-copy blit — same Dawn context). Audio waveform rendering on audio nodes. Control nodes show current value + sparkline.

**Verify:** Every node in the graph shows its live output: GPU nodes show textures, audio nodes show waveforms, control nodes show values. The "See Every Step" principle is now real.

**Why it matters:** Thumbnails are why the UI is native rather than web-based. They're the payoff for the architectural decision in §6.1 — zero-copy GPU texture blitting with no readback, no encoding, no transport. If 6+ thumbnails at frame rate cause performance problems, the on-hover fallback mode needs to be implemented. This is also what makes Vivid's graph view genuinely useful for debugging — you can see where a signal goes wrong by scanning the chain.

**Implementation notes:**
- `ThumbnailCache` manages per-node 140×88 textures; `ThumbnailRenderer` blits them into the graph view
- GPU operators: zero-copy blit from offscreen texture → thumbnail texture (same device/queue)
- CPU operators: `draw_thumbnail()` callback renders to pixel buffer, uploaded via `upload_cpu()`
- Audio nodes show waveform sparklines; control nodes show current value + sparkline history
- Commit: `88314de`

### Phase 12a: Draggable Nodes & Position Persistence — DONE

Extend the existing slider-drag pattern in `NodeGraphUI::update()` to support dragging entire nodes. Click on a node body to start a drag; release commits the new position. Positions are persisted in the graph JSON so layouts survive save/reload.

- New state: `active_node_drag_idx_`, drag offset tracking
- Positions stored in `NodeDef` via optional `layout` JSON field: `{"x": 100, "y": 50}`
- `layout_nodes()` uses saved positions when present, falls back to Sugiyama auto-layout for nodes without positions
- Port positions recomputed relative to node position after drag
- Saved via existing `Graph::save()` → `RuntimeAPI::save()` chain
- Files: `node_graph.h/cpp`, `graph.h/cpp`

**Verify:** Drag a node, save, reload — node stays where you put it. Nodes without saved positions still auto-layout correctly.

**Why it matters:** The graph view has been read-only for layout since Phase 11. Draggable nodes are the smallest step toward making it interactive — they reuse the existing mouse-drag infrastructure and don't require new graph operations. Position persistence via the `layout` field means users can arrange complex graphs to their liking without losing the layout on reload.

**Implementation notes:**
- Commit: `fb67e92`

### Phase 12b: Graph Zoom & Pan — DONE

Add zoom and pan controls to the node graph view. Middle-click-drag (or scroll-drag) pans the view; scroll wheel zooms toward the cursor position.

- New state on `NodeGraphUI`: `pan_x_`, `pan_y_`, `zoom_`
- New GLFW callback: `glfwSetScrollCallback` routed to `NodeGraphUI::on_scroll()`
- CPU-side transform (recommended over shader approach): transform node positions before passing to `draw_text`/`draw_rect`. Inspector stays fixed, unaffected by pan/zoom
- All drawing applies: `screen_pos = (logical_pos - pan) * zoom`
- Hit testing applies inverse: `logical_pos = screen_pos / zoom + pan`
- Thumbnail viewports scaled by zoom and offset by pan (on top of existing `dpi_scale`)
- Clamp zoom to 0.25x–3x range
- Files: `node_graph.h/cpp`, `main.cpp` (scroll callback)

**Verify:** Scroll to zoom, middle-drag to pan, click on nodes still works, thumbnails track position correctly. Inspector panel is not affected by zoom/pan.

**Why it matters:** Without zoom/pan, graphs with more than ~8 nodes overflow the visible area. This is the minimum navigation needed to work with real patches. The CPU-side approach keeps the implementation simple — no shader changes, and the inspector/REPL stay fixed on screen.

**Implementation notes:**
- Commit: `c81343f`

### Phase 12c: Operator Chooser — DONE

Spacebar (or Tab) opens a floating popup listing available operator types. The user can filter by typing, navigate with arrow keys, and press Enter to add a new node to the graph — no REPL required.

- Registry already has `type_names()` returning sorted names; `find()` returns `OperatorLoader*` with full descriptor (name, domain, params, ports)
- Popup: text filter + domain-grouped list drawn via `TextRenderer`
- Keyboard navigation: arrow keys to select, Enter to confirm, Escape to dismiss
- On confirm: generate unique ID (e.g. `lfo2`), call `RuntimeAPI::add_node(type, id)`
- Requires routing key events to `NodeGraphUI` — add `on_key()` method, update `main.cpp` callbacks
- Files: `node_graph.h/cpp`, `main.cpp` (key routing), `operator_registry.h` (already has what's needed)

**Verify:** Press Space, type "osc", see filtered list, Enter to add — node appears in graph. Escape dismisses without adding.

**Why it matters:** This is the first way to add operators without the REPL or JSON editing. Combined with 12a (drag to position) and the existing inspector (click to tweak parameters), the UI becomes a self-contained creative tool for basic patching. The operator chooser is also the natural hook point for the LLM chat panel (Phase 17) — the same `add_node` call, triggered by conversation instead of keyboard.

**Implementation notes:**
- Tab opens chooser; typing filters by name via `rebuild_chooser_items()`
- Domain-grouped list with arrow key navigation
- Enter confirms and calls `RuntimeAPI::add_node()`; Escape dismisses

### Phase 12d: Interactive Wire Dragging — DONE

Click an output port dot to start a wire drag. A preview wire follows the cursor. Release on a compatible input port to create the connection; release elsewhere to cancel. Click an existing wire to disconnect it.

- Hit-test output port dots on mouse-down; start drag state tracking (`drag_from_node`, `drag_from_port`, cursor position)
- During drag: draw a preview wire from the source port to the cursor using the existing Z-route wire style
- Hit-test input port dots on mouse-up; if valid, call `RuntimeAPI::connect()` + flush pending topology change
- Type compatibility: only highlight compatible input ports during drag (same domain or valid cross-domain bridge)
- Disconnection: click an existing wire (hit-test against wire paths) to call `RuntimeAPI::disconnect()`
- Files: `node_graph.h/cpp`, `main.cpp` (pending flush, already added in 12c fix)

**Verify:** Drag from an output dot to an input dot — wire appears, data flows. Release on empty space — no connection made. Click an existing wire — it disconnects. Save and reload — connections persist.

**Why it matters:** Wire dragging completes the basic graph editing loop: add nodes (12c), position them (12a), connect them (12d), and tweak parameters (Phase 11). With this, the UI is a fully self-contained patching environment — no REPL required for basic workflows. This is the last piece needed before the graph view can stand on its own as a creative tool.

**Implementation notes:**
- `dragging_wire_` state with `wire_from_node_`/`wire_from_port_` tracking
- `draw_preview_wire()` renders Z-routed preview during drag
- `hit_test_port()` for both start and end of drag
- Click input port to disconnect existing wire

---

## Tier 4: Can It Do the Thing?

The tool works. Now make it do what no other tool does: audio-reactive visuals through Spreads, polyphonic audio, LLM-assisted creative exploration, and the North Star Demo.

### Phase 13: Spreads — DONE

Build the Spread type (contiguous array + length, broadcasting logic). FFT Analysis operator (512-bin, simple radix-2 in C++, no FFTW). Bars operator (GPU bar graph visualization sized by Spread input). Spread data crossing the Control→GPU bridge as a GPU storage buffer.

**Verify:** Audio input → FFT → 512 bins → bar heights. Real-time audio-reactive bars flowing through the graph, with Spreads carrying the data.

**Why it matters:** Spreads are the most impactful data model decision in the architecture. This phase proves they work end-to-end: an audio operator produces a Spread, it flows through the control domain, it crosses into the GPU domain, and it drives 512 independent visual elements. If broadcasting or the GPU storage buffer path has problems, you find out on a concrete, visually obvious test case. The Spread plumbing validated here is what Phase 15 will use for per-voice envelope → per-rectangle color.

**Implementation notes:**
- `VividSpreadPort` type in `operator_api/types.h` — contiguous float array + count
- `VIVID_PORT_CONTROL_SPREAD` port type for spread-carrying connections
- FFT Analysis operator: 512-bin radix-2 FFT in C++, takes waveform Spread input, outputs spectrum Spread
- Bars GPU operator: instanced bar graph visualization driven by Spread input via storage buffer
- Demo graph: `graphs/fft_bars_demo.json` (Oscillator → Gain → FFT → Bars)
- Commit: `e800755`

---

> **Interstitial work (between Phase 13 and Phase 16):**
> Significant development occurred outside the phased roadmap:
>
> - **audio_out / video_out built-in sink operators** — terminal nodes for audio and video output
> - **Composite blend-mode GPU operator** — two-texture blending with Normal/Add/Multiply/Screen/Overlay modes
> - **Per-node GPU texture system** — each GPU node owns its own offscreen texture, with texture port wiring between nodes
> - **Graph UI enhancements** — context menus, bezier wires, node delete, Cmd+S save, wire tooltips, dropdown/enum params, resolution editing, node visibility toggles
> - **Fullscreen blit fit modes** — Fit/Fill/Stretch for video_out display
> - **Code quality refactoring** — split `node_graph.cpp` into draw/input/core files, deduplicated shared utilities, replaced magic numbers with named constants, added utility headers

### Phase 14a: Cross-Domain Spreads + Polyphonic Synthesis — DONE

The core infrastructure and operators that make Vivid a musical instrument:

- **Cross-domain Spread bridge** — `SpreadSnapshot` double-buffered bridge carries Spread data between Control and Audio threads. Lock-free, fixed-size buffers avoid audio-thread allocations.
- **NotePattern** — Control operator emitting chord progressions as Spreads (notes/velocities/gates), driven by Clock's `beat_phase`.
- **WavetableSynth** — Audio operator with 16-voice polyphony, wavetable playback, ADSR envelopes, and voice stealing. Supersedes the originally-planned Polysynth with a more capable architecture. Receives note Spreads from Control domain via the cross-domain bridge and exposes per-voice envelope values back as a Spread.
- **ChordProgression + Arpeggiator** — bonus Control operators built alongside: ChordProgression emits chord Spreads, Arpeggiator sequences them into note patterns.

**Implementation notes:**
- `SpreadSnapshot` in `audio_engine.h` — double-buffered, lock-free spread bridge
- `operators/control/note_pattern/` — NotePattern operator
- `operators/audio/wavetable_synth/` — WavetableSynth (16 voices, wavetable playback, ADSR, voice stealing)
- `operators/control/chord_progression/` — ChordProgression operator
- `operators/control/arpeggiator/` — Arpeggiator operator

### Phase 14b: Envelope ADSR + Inspect + Tests — DONE

- **Envelope ADSR** — full ADSR with `sustain`, `release` params and `gate` input port. Backward-compatible: without a gate connection, falls back to phase-wrap trigger (existing AD behavior).
- **inspect() Spread display** — `inspect_graph` shows Spread data for CONTROL_SPREAD output ports.
- **Cross-domain Spread tests** — `test_cross_domain_spread.cpp` covers the Spread bridge path end-to-end.

**Implementation notes:**
- `operators/control/envelope/envelope.cpp` — full ADSR with gate input
- `inspect_graph` MCP tool displays spread port values
- `tests/test_cross_domain_spread.cpp` — integration tests for spread bridge

### Phase 14c: Per-Voice Modulation System — DONE

Spread-based audio-domain modulator operators, replacing the legacy `ModulatorHost` with visible graph wires:

- **SpreadADSR** — Audio operator: takes `gates` (CONTROL_SPREAD input), produces per-slot ADSR envelopes (CONTROL_SPREAD output). Each spread slot maintains independent envelope state, sample-accurate on the audio thread.
- **SpreadLFO** — Audio operator: produces a Spread of LFO values. Per-voice mode (independent phase per slot, retrigger on gate) and free-running mode.
- **WavetableSynth modulation inputs** — CONTROL_SPREAD input ports: `filter_env`, `pitch_mod`, `amp_mod`, `position_mod`. Per-sample voice loop reads per-voice modulation values.
- **AudioSpreadWire** — routes CONTROL_SPREAD data between audio-domain operators.

**Implementation notes:**
- `operators/audio/spread_adsr/` — SpreadADSR operator
- `operators/audio/spread_lfo/` — SpreadLFO operator
- WavetableSynth spread inputs in `operators/audio/wavetable_synth/`
- `AudioSpreadWire` in `audio_engine.h/cpp`

### Phase 14d: Stereo Output — DONE

Stereo throughout the audio pipeline:

- **WavetableSynth** — dual outputs (`output_left`/`output_right`), unison stereo spread via `unisonDetune`, per-voice panning
- **AudioEngine** — `config.playback.channels = 2`, stereo buffer layout in `VividAudioState`
- **audio_out** — stereo passthrough
- **Existing audio operators** — Oscillator, Gain, Drum updated for stereo output

**Implementation notes:**
- WavetableSynth stereo in `operators/audio/wavetable_synth/`
- `audio_out` stereo passthrough in `src/audio_engine.cpp`

*North Star progress: Clock → chord progression → WavetableSynth works. MIDI knobs control timbre. Envelope Spread is produced. Per-voice modulation shapes timbre. 6 of 8 pieces.*

### Phase 15: The North Star Demo — Instance Operator (Hardware Instancing) — DONE

General-purpose **Instance** GPU operator that uses hardware instancing (instanced draw calls) to stamp any upstream GPU texture N times, driven by Spread data. Shape renders a polygon to texture with premultiplied alpha; Instance stamps it across the screen. Replaces the old fragment-loop Rects operator with a scalable architecture.

**Architecture:** No vertex buffer — quad vertices generated from `@builtin(vertex_index)`, per-instance data (position, value) read from a storage buffer via `@builtin(instance_index)`. Alpha blending (premultiplied: src=One, dst=OneMinusSrcAlpha). Fragment shader only runs on pixels covered by each quad — scales to hundreds of instances.

**Params:** `layout` (grid/circle/random), `size` (instance size), `hue_spread` (per-instance hue variation). **Ports:** `input` (GPU_TEXTURE), `values` (CONTROL_SPREAD — determines instance count + brightness), `positions` (CONTROL_SPREAD — optional custom placement), `texture` (GPU_TEXTURE output).

**Verify:** The full North Star scenario works end-to-end:

1. Clock → ChordProgression → Arpeggiator → WavetableSynth → audio output (you hear chords)
2. SpreadADSR envelope Spread → Instance/values (shapes light up per-note in sync)
3. Shape/texture → Instance/input (any shape works — squares, stars, circles)

**Why it matters:** This is the proof. Every architectural decision (three domains, Spreads, cross-domain bridges, JSON graph, operator contract) is validated in a single session. The Instance operator is general-purpose — it works with any texture source, not just shapes. Later extensible with rotation, scale, and additional Spread inputs per instance.

**Implementation notes:**
- Shape alpha fix: `vec4f(color * alpha, alpha)` for premultiplied alpha output
- Instance operator: custom pipeline (not gpu_common helpers), 2 bind groups, storage buffer for per-instance data
- Demo graph: `graphs/shape_instance_demo.json` (Shape → Instance ← SpreadADSR ← Arpeggiator)

### Phase 16: MCP Server & Operator Scaffolding — DONE

Two halves, sharing the same `OperatorCreator` module:

**MCP Server** — Python FastMCP wrapper (`mcp/vivid_mcp.py`) bridging MCP stdio to the Vivid HTTP control server. 30 tools total: `inspect_graph`, `list_types`, `add_node`, `remove_node`, `connect`, `disconnect`, `set_param`, `get_param`, `set_string_param`, `set_resolution`, `set_node_layout`, `inspect_node`, `list_nodes`, `add_midi_mapping`, `remove_midi_mapping`, `update_midi_mapping`, `get_graph_errors`, `scaffold_operator`, `save_graph`, `load_graph`, `save_variation`, `recall_variation`, `remove_variation`, `rename_variation`, `update_variation`, `list_variations`, `queue_variation`, `set_quantize_clock`.

**Operator Scaffolding** — `scaffold_operator` MCP tool + in-app `+ New Operator...` UI (at the top of the Tab chooser). Both backed by `OperatorCreator`: validates the name, writes a domain-appropriate template (control/audio/GPU), patches CMakeLists.txt, and hands off to `HotReloader` for compilation. New operators load into the registry and appear in the chooser without restarting. See [phase-16-design.md](phase-16-design.md) for the full design.

**Verify:** Claude Code connects via MCP. `inspect_graph` returns the full graph JSON. `set_param lfo1/frequency 8.0` changes the live output. `scaffold_operator gpu MyShader` creates the directory with boilerplate and opens it in the editor. In-app: press Tab, select `+ New Operator...`, pick a domain and name — operator compiles, loads, and appears in the chooser.

**Why it matters:** MCP is the bridge between Vivid and external LLM tools. Once this works, Claude Code can inspect and modify a running Vivid instance — which means you can develop operators and iterate on patches from your terminal. It also validates the Runtime API design under real external use: if the tool interface is awkward for Claude Code, it needs revision before the built-in chat wraps the same API. The in-app scaffolding UI makes the same creation flow available without leaving the app — the user hits Tab, realizes the operator they want doesn't exist, and creates it on the spot.

**Implementation notes:**
- `mcp/vivid_mcp.py` — 22 MCP tools wrapping HTTP POST to ControlServer
- `src/runtime/control_server.cpp` — 12 new dispatch handlers (set_resolution, set_node_layout, inspect, list_nodes, 3× MIDI mapping, get_graph_errors, scaffold_operator)
- `src/runtime/operator_creator.h/cpp` — name validation (lowercase_with_underscores, collision check), three templates (control/audio/GPU with WgslFilterBase), CMakeLists.txt patching via domain-specific insertion markers, editor launch ($VISUAL → $EDITOR → open)
- GPU template generates both .cpp (WgslFilterBase subclass) and .wgsl (fragment shader)
- `ControlServer::set_src_dir()` / `set_hot_reloader()` — context needed for scaffold_operator
- `poll_hot_reload()` handles unknown targets: loads new dylib via `register_loaded_operator()`, registers file watch for the new source
- `FileWatcher::add_watch()` made public for new operator registration

### ~~Phase 17: Built-in Chat Panel~~ — SKIPPED

**Decision:** The MCP server (Phase 16) already provides full LLM integration. Users connect Claude Code, Cursor, or any MCP-compatible client and get the complete tool surface — inspect, add, connect, set_param — with streaming, multi-turn context, and tool use UIs that would take weeks to replicate. Building a built-in chat panel would mean significant complexity (HTTP client, TLS/curl subprocess, threading, tool execution loop, text wrapping, scroll) to produce a worse version of what external clients already provide. The MCP server *is* the LLM integration — Tier 4 is complete without this phase.

---

> **Interstitial work (after Phase 16):**
> Significant development occurred outside the phased roadmap:
>
> - **Connection scale support** — wires carry a multiplier; `set_connection_scale` MCP tool
> - **Smart port visibility** — ports auto-hide when not connected; param picker for wiring
> - **DrumSequencer enhancements** — per-step modulation with tabbed grid UI
> - **macOS frame timer refactor** — separated event polling from tick callback
> - **Unit test expansion** — topo sort, operator creator, settings, string utils

### Phase 16a: Inspector Overhaul — DONE

Major rewrite of the inspector panel with rich widget support and layout metadata.

- Knob widgets, collapsible parameter groups, multi-column layouts
- XY pad controls and color pickers with new display hints (`XY_PAD`, `COLOR`)
- Inspector layout metadata in operator API (`group`, `layout_columns`, `layout_column_index`, `display_hint`)
- All GPU operators annotated with rich layout metadata
- Param-as-wire-source — parameters can be wired as sources, not just output ports
- Deferred deselection for multi-selected nodes

**Implementation notes:**
- `VividParamDescriptor` extended with `group`, `layout_columns`, `layout_column_index`, `display_hint` fields
- Inspector rendering rewritten to support grouped/columnar layout, knob drawing, XY pad hit-testing, color picker
- All GPU operators updated with `VIVID_GROUP`, `VIVID_DISPLAY_HINT`, and column annotations in their `collect_params()`

### Phase 16b: Data-Driven WGSL Filter Framework — DONE

Replaced 19 C++ dylib GPU filters with self-describing `.wgsl` files. A single generic runtime loads any WGSL filter from its embedded JSON metadata — no per-filter C++ code needed.

- WGSL header parser extracts JSON metadata (name, params, inputs, time_dependent) from `/* { ... } */` comment blocks
- `DataDrivenFilterConfig` + `OperatorLoader::init_data_driven()` — generic runtime for any WGSL filter
- 19 filters migrated: HSV, Levels, Blur, GaussianBlur, Edge, Mirror, Pixelate, Posterize, Gradient, Noise, ChromaticAberration, Scanlines, CRTEffect, Transform, Displace, ChannelMixer, Vignette, Dither, Halftone
- Unified into single `WGSLFilter` operator type with per-instance loaders (`NodeState::owned_loader`)
- Backward compatibility: old graphs with `"type": "HSV"` still work transparently via alias resolution
- Clone & Edit workflow preserved — user filters remain individually registered
- JSON theme system — 8 built-in themes, user-selectable via preferences

**Implementation notes:**
- `src/runtime/wgsl_header_parser.h/cpp` — parses `/* { ... } */` JSON blocks from `.wgsl` files
- `src/runtime/operator_registry.cpp` — `init_data_driven()` scans preset directories, registers each `.wgsl` as a `WGSLFilter` with an `OperatorLoader` carrying the parsed config
- `NodeState::owned_loader` — per-instance loader pointer so each `WGSLFilter` instance knows which `.wgsl` it represents
- `src/runtime/scheduler.cpp` — alias map resolves legacy type names (e.g. `"HSV"` → `"WGSLFilter"`) and attaches the correct loader
- `src/ui/theme.h/cpp` — JSON theme system with 8 built-in themes (Dark, Light, Monokai, Solarized Dark/Light, Nord, Dracula, Gruvbox)

---

## Tier 5: Experimentation Interfaces

The core tool is complete. Now build the interfaces that make Vivid's exploration model unique.

### Phase 18: Patch Panel — DONE

Two-node connection viewer replacing the original N×M matrix concept. When two nodes are selected, the inspector switches to a patch panel showing both nodes' ports as interactive jacks — filled circles for outputs, hollow rings for inputs, half-filled for parameters. Drag between compatible jacks to create a connection. Right-click a wire to disconnect. Incompatible jacks dim during drag for immediate visual feedback. Bezier wire preview follows the cursor during drag.

**Verify:** Select two nodes. Patch panel appears showing all ports for both nodes. Drag from an output jack to an input jack — wire created. Right-click a wire — disconnects. Incompatible ports dim during drag.

**Why it matters:** The original plan was an N×M connection matrix, but a contextual two-node panel turned out to be a better fit. It integrates naturally with the existing selection model (select two nodes → see their connection points), provides immediate compatibility feedback via dimming, and avoids the overwhelming grid of an N×M matrix where most intersections are irrelevant. The patch panel is the "zoom in on two nodes" complement to the graph view's "zoom out on everything."

**Implementation notes:**
- `src/ui/node_graph_draw.cpp` — `draw_patch_panel()` renders jacks, wires, and labels
- `src/ui/node_graph_input.cpp` — `handle_patch_click()` for drag-to-connect, `handle_patch_right_click()` for disconnect
- `src/ui/node_graph.h` — `PatchJack` / `PatchWire` state structs
- `src/ui/node_graph_constants.h` — `kPatch*` layout constants

### Phase 19: Session / Variation Grid — DONE

Variation system with named parameter snapshots and a session grid UI strip at the bottom of the node graph. A `VariationDef` captures `node_id → { param → value }` for all nodes — a complete parameter configuration. The runtime API provides full CRUD: `save_variation`, `recall_variation`, `recall_variation_idx`, `remove_variation`, `rename_variation`, `update_variation`, `list_variations`, `queue_variation`, `set_quantize_clock`. Beat-quantized switching reads `beat_phase` from a designated Clock node and supports Instant, Beat, Bar, and 4Bar quantize modes. The session grid UI shows clickable cells (double-click to rename), a dirty indicator when parameters change while a variation is active, quantize mode buttons, and horizontal scroll. A `UICommandSink` / `RuntimeCommandSink` abstraction layer decouples the UI from the runtime for testability. 8 MCP tools and 8 HTTP endpoints provide full LLM access to the variation system.

**Verify:** Save a variation, modify parameters, save another. Click cells to switch — parameters restore instantly. Set quantize to Beat with a Clock node — switching aligns to beats. MCP `list_variations` returns all variations with their parameter data.

**Why it matters:** The session grid is what makes LLM-generated variation useful. Without it, the LLM generates one thing at a time and the user evaluates sequentially. With it, the user sees a field of options and navigates spatially — the core innovation described in the vision statement. Beat-quantized switching adds a live performance dimension: transitions snap to musical boundaries rather than happening at arbitrary moments.

**Implementation notes:**
- `src/runtime/graph.h/cpp` — `VariationDef` data model + CRUD methods
- `src/runtime/runtime_api.h/cpp` — 9 variation methods + `tick_quantized_switch()`
- `src/runtime/control_server.cpp` — 8 dispatch handlers for variation HTTP endpoints
- `src/ui/node_graph_draw.cpp` — `draw_session_grid()` renders the bottom strip
- `src/runtime/graph_snapshot.h` — `VariationInfo` for external consumption
- `src/ui/ui_command_sink.h` / `src/runtime/runtime_command_sink.h` — command abstraction layer
- `mcp/vivid_mcp.py` — 8 variation tools

### Phase 20: Pattern Algebra — DONE

Five composable pattern operators in the control domain — not a DSL, but standard Control operators that emit time-varying sequences through the existing graph wiring model. Pattern combinators: **Euclidean** (Bjorklund rhythm generator), **PatternSeq** (16-step beat-driven value sequencer), **Stack** (spread combiner with concat/interleave modes), **Alternate** (beat-driven input cycler), **PatTransform** (reverse/rotate/scale/offset/probability chain). All operators use Spread ports for pattern data, making them composable with each other and with any existing spread-consuming operator.

**Verify:** Clock → PatternSeq (steps=4, vals: 0.3, 0.8, 0.5, 1.0) → Shape/radius produces beat-driven size changes. Two Euclidean generators (E(3,8) + E(5,8)) → Stack → PatTransform(scale=2) → Instance/values creates a polyrhythm. `ctest -R test_pattern_algebra` passes all algorithm and integration tests. `pattern_algebra_demo.json` loads and runs.

**Why it matters:** Patterns are what make Vivid temporal — not just reactive to live input, but generative of its own rhythmic behavior. They're extremely LLM-friendly (short, composable, text-representable) and connect directly to the TidalCycles and Strudel communities' insight that pattern composition is a creative medium.

**Implementation notes:**
- `operators/control/euclidean/` — Bjorklund algorithm with rate multipliers, beat tracking, trigger/gate/step/pattern outputs
- `operators/control/pattern_seq/` — 16-step sequencer with rate, gate_length, probability, spread output
- `operators/control/stack/` — Concat/Interleave modes for combining up to 4 spread inputs
- `operators/control/alternate/` — Cycles between connected spread inputs at Beat/2 Beats/Bar/2 Bars/4 Bars rates
- `operators/control/pat_transform/` — Transform chain: reverse → rotate → scale → offset → probability (deterministic Knuth hash)
- `tests/test_pattern_algebra.cpp` — 60+ assertions covering algorithm correctness, edge cases, and integration
- `graphs/pattern_algebra_demo.json` — Clock driving Euclidean + PatternSeq → Stack → PatTransform → Instance

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
- **Multi-window / multi-monitor** — output undocking for projector/LED wall
- **Accessibility** — keyboard navigation, screen reader, high-contrast
- **Library version pinning** — lockfile vs vendoring
- **Project file format** — single JSON vs directory with assets
- **Bundled compiler** — optionally shipping a C++ compiler (e.g., Zig's `zig c++`) so users don't need Xcode CLI tools. Packaging problem, not architecture problem.
- **OSC input** — same pattern as MIDI Input (Phase 10), deferred because MIDI is more immediately useful for the North Star scenario

---

## The Guiding Principle

The LLM populates the exploration space; the user navigates it. Every architectural decision should be evaluated against this principle. If a decision makes it harder for the LLM to generate options, or harder for the user to evaluate and combine them in real time, it's the wrong decision.
