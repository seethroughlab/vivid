# Vivid Master Development Plan

Combines the Dev Tools Philosophy and LLM Iteration Plan into a single document. Items are checked off based on current implementation status.

---

## Philosophy

Vivid's built-in tooling serves **the performer and the live debugger**, not the developer. Development happens outside Vivid (Claude Code, Cursor, VS Code). Performance, monitoring, and real-time interaction happen inside it.

Two audiences share the same underlying architecture:
- **The Performer** uses Vivid's built-in UI — chain visualizer, parameter surface, presets, keyboard/MIDI controls, recording
- **The Developer** (human or LLM) uses external tools for code editing and Vivid's MCP/CLI for validation and preview

---

## Part 1: Dev Tools Refocus

### Remove (Developer-Oriented Tools)

- [x] **Code editor** — External tools do this better; agents don't need it
- [x] **Integrated terminal** — General-purpose tool that doesn't belong inside Vivid
- [x] **File browser / management UI** — Belongs to external dev environment
- [x] **Operator library panel** — Removed alongside editor
- [x] **File buffer system** — No longer needed without editor
- [x] **Complex dock/split system** — dock_manager, split_container, panel_group, panel_leaf, layout_node all removed; replaced with flat PanelManager + z-order

### Keep (Performer/Debugger Tools)

- [x] **Chain Visualizer** — Custom node graph with live thumbnails, zoom/pan, solo mode, hierarchical auto-layout, pending changes indicator
- [x] **Performance Panel** — Real-time graphs for FPS, frame time, memory, DSP load (Cmd+1)
- [x] **Inspector Panel** — Interactive parameter sliders, color picker, auto-show on node select, change callbacks for Claude workflow
- [x] **Console/Log Panel** — Read-only scrolling log with color-coded severity, 256-msg ring buffer, hot-reload status (Cmd+2)
- [x] **Status Bar** — FPS, frame time, resolution, memory, record/snapshot buttons, codec selector, grid opacity, MCP warning
- [x] **Keyboard Shortcuts** — ShortcutManager with 10+ shortcuts, platform-native Cmd/Ctrl
- [x] **Output Recording** — VideoExporter with ProRes/H.264/H.265, hardware-accelerated, audio sync via recording tap
- [x] **Preferences** — Theme presets, UI style, visibility, corner radius, persisted to ~/.vivid/preferences.json

### Enhance (Planned Improvements)

- [ ] **Parameter Control Surface** — Evolve Inspector into a performer-oriented control surface:
  - [ ] "Performable" parameter curation — only author-marked params appear
  - [ ] Group by purpose (Color, Motion, Audio) not by operator
  - [ ] MIDI mapping — physical controllers bind to on-screen parameters
  - [ ] Large, high-contrast UI for dark rooms / distance viewing
- [ ] **Chain Health Monitoring** — Visual indicators on nodes:
  - [x] Infrastructure: `NodeState::healthBorderColor`, `overlayCallback`, per-node RMS/activity tracking
  - [ ] Thumbnail border colors (green=signal, yellow=dark/silent, red=NaN/static)
  - [ ] Inline VU meters on audio nodes
  - [ ] Frame delta sparklines (activity indicator)
- [ ] **Preset / Snapshot System** — Live performance cue management:
  - [ ] Save/recall named parameter snapshots
  - [ ] Hard cut or crossfade interpolation between snapshots
  - [ ] Setlist / cue sequence organization
  - [ ] Keyboard and MIDI triggering

---

## Part 2: LLM Self-Evaluation (Introspection & Validation)

*Fast, cheap, structured. The agent edits chain.cpp, runs introspection, checks assertions, and iterates without human involvement.*

### 2.1 Base Introspection API

- [x] **InspectData struct** — `modules/vivid-core/include/vivid/inspect_data.h`
  - Key-value metrics (float) and metadata (string) maps
  - `set()` methods, `toJSON()` serialization
- [x] **Operator::inspect() virtual method** — `modules/vivid-core/include/vivid/operator.h`
  - Base implementation auto-populates enabled state + all declared params
  - Subclasses override for custom metrics

### 2.2 Operator inspect() Overrides

- [x] **Feedback** — reports `has_buffer`, `first_frame`
- [x] **Levels** — reports `rms`, `peak`, `rms_left`, `rms_right`
- [x] **BandSplit** — reports `sub_bass`, `bass`, `low_mid`, `mid`, `high_mid`, `high`
- [x] **AudioOutput** — custom inspect override
- [ ] Remaining operators — incremental, add as needed

### 2.3 Output Texture Analysis

- [x] **FrameAnalysis struct** — `modules/vivid-core/include/vivid/frame_analysis.h`
  - meanBrightness, contrast, dominantColor, dominantHue, saturationAvg
  - 8-bucket luminance histogram, 3x3 spatial grid brightness
  - `toJSON()` serialization
- [x] **GPU readback + CPU analysis** — `modules/vivid-core/src/frame_analysis.cpp`
  - Handles RGBA8, BGRA8, RGBA16Float, RGBA32Float formats

### 2.4 Chain-Level Inspection

- [x] **Chain::inspectAll()** — `modules/vivid-core/src/chain.cpp`
  - Iterates operators in add order, calls inspect() on each
  - Adds operator type and output kind metadata
  - Analyzes output texture via `analyzeTexture()`

### 2.5 MCP Integration (Replaced CLI `vivid inspect`)

- [x] **inspect_chain MCP tool** — `src/cli/mcp_server.cpp`
  - Returns per-operator metrics + output texture analysis as JSON
  - WebSocket handler in `src/cli/runtime_api.cpp`
- [x] **capture_frame** — Capture current frame to PNG
- [x] **capture_at_frame** — Advance to frame N and capture
- [x] **set_param** — Set parameter on running operator immediately
- [x] **advance_frames** — Progress animation forward
- [x] **orbit_camera** — Position camera around target point
- [x] **get_pending_changes / clear_pending_changes** — Claude-first parameter workflow

### 2.6 Snapshot Mode (CLI)

- [x] **--snapshot flag** — `src/cli/app.cpp`
  - Single frame: `--snapshot output.png`
  - Multi-frame: `--snapshot-frame 0-11` (range), `0-20:2` (step), `0,5,10` (list)
  - Auto-numbered output files for multi-frame

### 2.7 Assertion System

- [ ] **vivid-assertions.yaml format** — Declarative pass/fail conditions
  - Operator metric checks (e.g., feedback energy > 0.3)
  - Output analysis checks (e.g., contrast > 0.15)
  - Conditional checks (after_frame, range checks)
- [ ] **vivid check CLI command** — Run assertions, exit code 0/1, JSON report
- [ ] **YAML parser** for assertion definitions

### 2.8 CLI `vivid inspect` Command

- [ ] **Standalone CLI command** — Multi-frame sampling with structured output
  - `vivid inspect path/to/project --duration 2.0 --samples 5 --out report/`
  - Produces inspection.json + frame thumbnails + optional waveform
  - *Note: MCP `inspect_chain` provides this interactively; CLI version is for CI/headless*

---

## Part 3: Puppeteered Playback & A/V Export

*The agent produces actual video/audio exports with simulated interactivity so a human can experience the result.*

### 3.1 Video/Audio Infrastructure

- [x] **VideoExporter** — `modules/vivid-core/include/vivid/video_exporter.h`
  - ProRes 4444, H.264, H.265/HEVC codecs
  - Hardware-accelerated via VideoToolbox (macOS)
  - `startWithAudio()` for combined A/V export
- [x] **Audio Recording Tap** — `chain.h`
  - `startAudioRecordingTap()` / `stopAudioRecordingTap()`
  - `popAudioRecordedSamples()` for float sample retrieval
  - Integrated with VideoExporter via `pushAudioSamples()`
- [x] **AudioEvent system** — `audio_event.h`
  - Thread-safe lock-free queue (SPSC)
  - Event types: NoteOn, NoteOff, Trigger, ParamChange, Reset

### 3.2 Event Injection & Scripted Playback

- [ ] **EventInjector class** — Read timeline scripts, feed synthetic events into Context
  - Support: param_set, key_press, trigger (start here)
  - Later: param_ramp, midi_note, midi_cc, mouse_move
- [ ] **Playback script format** (YAML) — Duration, resolution, FPS, timed events
- [ ] **YAML parser** for playback scripts
- [ ] **Script validation** — Warn on events targeting nonexistent operators/params

### 3.3 `vivid export` CLI Command

- [ ] **CLI subcommand** for headless A/V export
  - `vivid export path/to/project --script script.yaml --duration 30 --resolution 1920x1080 --fps 60 --output preview.mp4`
  - Quick preview mode (lower res, shorter)
  - Audio-only export option

### 3.4 Project Manifest

- [ ] **vivid-project.json** — Machine-readable project description
  - Chain file path, preview settings
  - Exposed parameters with ranges and descriptions
  - Assertion file reference

---

## Part 4: Future

- [ ] **Live Streaming** — WebRTC/RTMP stream of headless output for real-time human feedback
- [ ] **LLM Script Generation** — LLM generates playback YAML from natural language requests

---

## Agent Iteration Loop (How It All Composes)

```
INNER LOOP (Part 2) — seconds, no human needed
  LLM edits chain.cpp
       ↓
  MCP inspect_chain → structured JSON + thumbnails  ← IMPLEMENTED
  (or: vivid inspect → JSON report)                 ← NOT YET
       ↓
  vivid check → assertions pass?                    ← NOT YET
       ↓ no: iterate        ↓ yes: proceed
  ← back to edit       move to outer loop

OUTER LOOP (Part 3) — minutes, human reviews
  LLM generates playback script                     ← NOT YET
       ↓
  vivid export → video/audio file                   ← NOT YET
       ↓
  Agent sends to human
       ↓
  Human feedback → back to inner loop
```

---

## Implementation Priority

**Already done:**
1. ~~Remove dev-oriented panels (editor, terminal, file browser)~~
2. ~~InspectData + Operator::inspect() + key operator overrides~~
3. ~~FrameAnalysis with GPU readback~~
4. ~~Chain::inspectAll() aggregation~~
5. ~~MCP inspect_chain + capture + set_param tools~~
6. ~~Snapshot mode (--snapshot with multi-frame)~~
7. ~~VideoExporter with codec selection~~
8. ~~Audio recording tap~~
9. ~~Panel system simplification~~

**Next up:**
1. Chain health monitoring (wire up existing infrastructure to render visual indicators)
2. Assertion system (vivid-assertions.yaml + vivid check)
3. MIDI parameter mapping
4. Preset/snapshot save/recall system
5. EventInjector + playback scripts
6. `vivid export` CLI command
7. `vivid inspect` standalone CLI (if needed beyond MCP)
8. Project manifest (vivid-project.json)
