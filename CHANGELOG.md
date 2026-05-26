# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0-alpha.2] - 2026-05-26

Second alpha release. Approximately five weeks after alpha.1, this release
delivers the complete session system (Tracks, Clips, Scenes), the native note
protocol replacing MIDI buffers, full CLAP plugin hosting, the operator codegen
framework, a unified 2D GPU drawable pipeline, and dedicated editor windows for
all major sequencer and synthesis operators.

### Session

- **Track / Clip / Scene architecture.** Replaces the legacy Variations model.
  Clips hold parameter snapshots; scenes group cross-track assignments; launch
  is quantized to a configurable clock (Phases 1–5 complete).
- **Scene×Track grid UI.** Cell context menus, ownership badges, and a
  node-assignment workflow replace the legacy clip launcher panel.
- **MCP tools.** `inspect_session`, `inspect_clip`, `inspect_scene`, and
  `GraphSnapshot` for LLM-driven session introspection.

### Operators — New & Changed

- **AudioClip.** New MIDI/audio clip operator with waveform editor, per-editor
  undo/redo, piano-roll thumbnail, and sync modes. Replaces MidiFilePlayer.
- **SP404.** Sampler-based drum machine with 16×4 pad grid, per-pad file
  parameters, and a dedicated grid editor.
- **Tape.** Analog tape emulation: saturation, wow, flutter, and hiss.
- **ColorBands / SpreadNoise / EnvelopeFr.** New GPU source and control operators.
- **PhrasePulse.** Phrase-aligned sequencer reset with `bar_sync` support.
- **DrumSequencer.** 4-pattern store + auto-advance song mode; per-drum MIDI
  outputs routable without a hub node.
- **Math.** Added subtract, divide, and modulo with safe zero handling.
- **Composite.** Fixed black-output bug when blending onto an empty accumulator.
- **audio_out.** Real device dropdown, hot-plug support, native sample-rate
  negotiation.

### Editor Windows

Dedicated editor windows replace inspector panels for all major sequencer and
synthesis operators: MSEG, Sequencer, DrumSequencer (probability, roll, pattern
A/B), Tracker, Arpeggiator, Euclidean, PatternSeq, ParametricEq, AudioClip
waveform editor, and SP404 pad grid editor. All editors share a per-editor
undo/redo stack with paste cursor; toolbars use shared layout helpers; dense
timelines auto-coarsen their grid.

### Plugin Hosting

- **CLAP.** Full CLAP instrument and effect support (`CLAPInstrument`,
  `CLAPEffect` operators), plugin browser editor window, MCP tools, and
  transport sync.
- **VST3.** Reintegrated with SDK v3.8 (MIT license); `IMidiMapping2` support;
  plugin state persisted across topology changes.
- **AU.** Parity improvements matching VST3/CLAP enhancements.
- **Shared improvements.** `DirectParamQueue` for low-latency param delivery;
  param metadata API; curated inspector UI for macro slots; inline numeric text
  entry; library ref-counting for safe rate-change reload.
- **Stutter fix.** `getState()` gated on `state_dirty_` with a 1 Hz max to
  prevent audio-thread stalls under concurrent calls.

### Native Note Protocol

`VividMidiBuffer` wire type replaced by `VividNoteBuffer`, carrying per-voice
expression (pitch bend, pressure, timbre) from emitters through `VoiceAllocator`
to all polyphonic synths. `note_breakout` provides shared-control fanout and
per-voice lane outputs. Legacy `LANE_ARRAY` note outputs removed.

### Operator Framework

- **operator_codegen.** AST-based (tree-sitter) code generation tool;
  `VIVID_DEFINE_OP` v2 pattern with descriptor validation at build time. All
  production operators migrated; packages use `VIVID_REGISTER` for codegen-free
  registration.
- **V3 metadata.** `display_name`, `keywords`, and `summary` fields on all
  operators; surfaced in `operator_docs` and the chooser.
- **`VIVID_DISPLAY_EDITOR` hint.** Marks params that appear only in dedicated
  editor windows.
- **Operator alias resolver.** Table-driven legacy-id → new-id mapping for
  future renames; `EnvelopeFollower` aliases to `SmoothFr`.

### GPU / 2D Visual Pipeline

- **Drawable pipeline (Phase E complete).** Unified 2D rendering path;
  `ShapeField` promoted to drawable; `Text2D` subsumes RichText. Legacy
  texture-chain duplicates retired.
- Operator `kName` normalized to CamelCase throughout.
- `Shape2D` draw_thumbnail previews the actual polygon; GPU thumbnail mipmap
  chain for filtered zoom.
- `Shape2D` position_x / position_y params.
- `create_shader_checked()` with validation error scope for cleaner WGSL
  diagnostics.

### MCP & Developer Tooling

- Music evaluation backend switched to Gemini; `configure_music_eval_backend`
  MCP tool.
- 16 audio-analysis diagnostic tools for LLM-driven audio reasoning
  (librosa-based).
- Reference-driven AV composition tools and AV-coupling visual evaluation tools.
- `validate_operators` HTTP command + MCP tool; `rescan_operators` for
  late-bound dylibs.
- `scaffold_operator` defaults to the project-local package (not core).
- Operator coverage policy, audit script, and tests.
- Shader-filter description and per-param docs surfaced in `operator_docs`.

### UI

- Session panel drawn in the overlay pass; track node-assignment workflow with
  ownership badges.
- Operator chooser polish; `Make many` shortcut.
- Port description tooltip on hover.
- Inspector docs-link button.
- Chooser Map tab axis-label polish and sizing.
- Diagnostics panel: long-window memory trend with slope indicator; AV
  reactivity analysis expanded to three lenses.
- Crash-recovery dialog wraps to fit long prompts.

### Audio Analysis

- `AudioAnalysis` operator writes smoothed RMS, peak, spectral centroid, and
  related values to named scalar output ports for direct wire connections into
  the graph.

### Demo Graphs

- Proswell-style IDM/chiptune demo with multi-track drums and 4-section song mode.
- GROOVIN drum-loops + SP404 sync-mode demo.
- Session example graphs (tracks, clips, scenes).
- Intro demos updated: both shape axes driven, envelope-followed drums.

### Build & Localization

- wgpu-native upgraded to v29.0.0.0.
- ARM64 macOS architecture pinning; oscpack ARM64 guards patched at configure
  time.
- `.json` graphs open via drag-drop onto the app icon or "Open With".
- 22 new locale keys translated across all 21 locales.
- Default output texture resolution raised from 800×600 to **1280×720**.

### Bug Fixes & Performance

- `drain_inject` hot path: atomic pending flag + pre-allocated scratch buffer
  eliminates per-frame allocation.
- Lane state `sweep_retired`: atomic flag makes the sweep zero-cost when idle.
- Dock-icon drop no longer shows a spurious "cannot open Vivid Graph" dialog.
- Drop-settle race conditions fixed (guard on `completed_` flag, async retry on
  silent failures).
- TEXT/FILE param sync skipped on the audio thread to prevent callback races.
- Graph compiler bridge-annotation enforcement relaxed to warnings.

## [0.1.0-alpha.1] - 2026-04-18

First official release. This entry collapses all prior development history
into a single baseline — earlier `[0.1.0-alpha1]` and `[0.1.0-alpha2]` sections
(dated 2026-03-06 and 2026-03-14) represented planning text, not real
releases; 17 informal GitHub Release objects tagged `v0.1.0-alpha.1`–`alpha.28`
in January 2026 predate this canonical baseline.

### Runtime

- **Execution model.** Operators declare a fixed cadence at compile time —
  frame-rate (`_fr`) or audio-rate (`_au`) — with no runtime inference.
  20 operators (LFO, Envelope, Clock, sequencers, etc.) are dual-cadence and
  implement both `process_frame` and `process_audio`. Cross-cadence
  connections use explicit `AudioFrameBridge` edges with typed dispatch.
  `RuntimeCore` owns the main loop; `FrameExecutor` and `AudioExecutor` run
  their respective cadences; `CompiledGraph` is the sole runtime authority.
- **Lanes.** Lane-aware signal propagation with structural provenance tracking
  across all three domains. `LaneStateService` provides identity-keyed
  per-lane persistent state. Structural reshape operators (Repeat, Tile,
  Select) support lane modes (linear, random, phase, golden). Frame-domain
  per-lane lifting and GPU compute-backed lane evaluation. Wire badges show
  lane count; node badges show lane behavior. Lane metadata exposed in MCP
  introspection and UI snapshots.
- **Solo mode.** Core + UI + control-server integration with context menu,
  `S` shortcut, and visual indicators.
- **Hot-reload & error recovery.** Atomic swap in `OperatorLoader` with
  compile-error propagation to node tooltips, borders, inspector, and
  `ns.error_message` for MCP introspection.
- **Crash recovery.** Detects previous-session crashes; shows a recovery
  dialog with "Open Normally" / "Open Safe Mode" / "Reveal Crash Report".
- **System-requirements preflight.** `vivid doctor` CLI subcommand and
  in-app **Help → Check System Requirements...** dialog that detect
  missing cmake / git / C++ compiler with platform-specific install hints.
  Compiler presence gated in `compile_package()` so missing toolchain
  surfaces a friendly message instead of raw cmake stderr.
  `explain_build_failure` extracts install hints into a structured
  `remediation` field.

### Operators

- **GPU.** Noise, Blur, Bloom, Composite, Gradient, HSV, Mirror, Edge,
  Scanlines, Ramp, Dither, Transform, CRT Effect, Chromatic Aberration,
  Pixelate, Tile, Halftone, Levels, Displace, Posterize, Feedback, Shape,
  Bars, Cellular Automata, Reaction Diffusion, Flocking, Fluid, Instanced
  Shapes, Trails, MeshWarp, LUT, Scopes, SVG Render, Rich Text, Particles,
  Quad Warp, Solid Color, Edge Blend, Radial Rainbow, Division Raster, and
  a data-driven WGSL filter framework with self-describing `.wgsl` presets.
- **Audio.** WavetableSynth, PlexusSynth, Compressor, Limiter, Convolution
  Reverb, DualFilter (split crossover), DrumKit, Reverb, Delay, Bitcrush,
  Distortion, glitch operators (BeatRepeat, Stutter, Scratch, TapeStop,
  Stretch, FrequencyShift), plus factory presets across 17 operators and
  multi-mode filter DSP (14 filter types). Highway SIMD foundation, exposed
  to package builds. Configurable audio buffer size; denormal flushing.
  Polyphonic lane-array gate support in Envelope. Per-note expression and
  MPE in MidiInput.
- **Control.** Gate, Logic, Random, Sequencer, Smooth, Switch, LFO, Clock,
  PathAnimate, Macro, MSEG, Step Sequencer, DrumSequencer,
  ChordProgression. Pattern algebra (Euclidean, PatternSeq, Stack,
  Alternate, PatTransform). Random and Random S&H consolidated into LFO.
  Drums, sequencers, and sampler packages merged into core.
- **Input.** Webcam, MovieLoaded, MovieFileIn (GPU), MovieFileAudio, MidiInput,
  Mouse, Keyboard, Interactive input system.
- **Analysis.** TextureAnalysis (GPU→Control readback), FFT.

### Movie Playback

- Async video decode with `MovieFileIn` (GPU) and `MovieFileAudio` (audio)
  operators.
- AV sync with drift reconciliation, generation-based decoder flush, Metal
  frame upload sync.
- Playback diagnostics and validation.
- Fix AVFoundation `dispatch_sync` deadlock on macOS.
- Fix video seek-thrashing, preroll free-run, AV drift, yellow flash on
  codec switch.

### Modules & Modulation

- Subgraph modules: composable operator groups with param/preset proxy.
- Composite-local modulation assignments.
- Module-aware control-server queries and introspection.

### UI

- Animated splash screen with plugin-scan progress.
- Smooth scrolling/panning with animated node hover and selection glow.
- Domain tabs and scored search in operator chooser.
- Diagnostics panel with per-node audio telemetry and lane state.
- Streaming build console.
- Sticky notes with markdown rendering.
- Clone-to-edit: double-click a node to scaffold a local editable package copy.
- Multi-column inspector with param tooltips, auto-select on click,
  reset-to-defaults.
- Custom inspectors: arpeggiator velocity bars, chord grid, interactive
  waveform.
- N-channel audio waveform visualization.
- Wire reconnect drag; node copy-paste with offset positioning.
- Graph metronome transport with BPM editing.
- Save As; recent files; protected default graph.
- Session exploration surface with variation branching.
- Configurable pan gesture (middle/left/right drag).
- `video_out` shows input texture as thumbnail; GPU thumbnails fit source
  aspect ratio.
- Tab-to-connect: filtered chooser opens during wire drag.
- Create-operator modal with configurable ports, params, and variants.
- Per-operator `draw_inspector` migrations.
- File > New (Cmd+N) resets to default empty graph.
- User-expandable output-port affordance for operators with >3 outputs.
- Separate ports and params: hide param wires by default, split patch bay.
- Wire string values directly into file/text params.
- `MISSING` indicator for nodes with unknown operators.
- GPU texture-size inheritance tracking shown in inspector.
- WGSL error scopes, transient error state, error tooltip.
- Scrollbar in example browser; Cmd+Shift+P for package browser.

### Localization

- i18n with OS-locale auto-detection and 21 languages.
- UTF-8 glyph baking and text wrapping in the 2D renderer.

### State & Presets

- State-machine system with per-operator presets.
- Variation system with save/recall and beat-quantized switching.
- Crossfade transitions between states/presets.
- Per-parameter lock flags.

### Package System

- Operator package infrastructure with dependency resolution.
- Package catalog with remote fetch, caching, install/uninstall.
- Package browser UI with search and category tabs.
- Package test runner and cmake builds.
- Streaming build console for package builds.
- Machine-readable error codes; improved error display.
- Auto-reload graph after package install/link if missing operators exist.
- Async package install/uninstall/link/unlink (non-blocking UI).
- "Link Local..." button.
- `catalog/repos.json` registry of package repositories.
- Scan `factory_presets` directory when loading installed packages.
- GitHub URL normalization.

### Port Type Registry

- Transport-based custom port system; stable type IDs using explicit
  namespaced strings.
- MIDI port type support.
- `audio_safe` flag with validation.
- Registry hardening: graceful failure on conflicts, stable_type_id validation.
- First-class GPU port types (`GPU_BUFFER`, `GPU_MESH`, `GPU_COMPUTE`).
- Introspection: expose `stable_type_id` in JSON responses.

### Semantic Coercion

- Semantic-tag workflow with destination policy and runtime/UI hardening.
- Explicit semantic coercion contract with exposed inferred remaps.
- Validation matrix and tolerance regressions.

### Export & Recording

- Standalone export pipeline.
- Audio export (`--audio-only`).
- Capture/recording infrastructure.
- Screenshot CLI flag.

### MCP & Tooling

- MCP server for Claude Code integration.
- `sample_node_outputs` for live output sampling; `set_analysis` for GPU
  frame analysis.
- `inspect_graph` summary mode for LLM-friendly output.
- Per-node audio telemetry and system-health diagnostics.
- Operator registry scan diagnostics.
- Operator development MCP server with source access, build diagnostics,
  WGSL docs, and documentation resources.
- Source-driven operator documentation; visual reference image analysis.
- MIDI learn UI and mapping persistence.
- CLI subcommands: build, export, params, graph, docs, inspect, check,
  doctor, lock, verify-lock.

### Core App Update System

- appcast-backed `AppUpdateManager` with non-blocking startup checks.
- `vivid check-core-updates` CLI.
- Control-server endpoint and MCP tool.
- macOS menu: "Check for Updates...", "Automatically Check for Updates".
- In-app non-intrusive update notice.
- Settings persistence for update behavior.

### Dependencies

- nlohmann/json (replacing yyjson), efsw file watcher, libcurl, TinyXML-2,
  Midifile, Dragonbox for float32 serialization, NanoSVG, Snappy, Highway
  SIMD, argv-based `ProcessRunner`.
- wgpu-native switched to upstream; Syphon metallib compiled at build time.

### Codebase

- `src/runtime/`, `src/ui/`, and `tests/` restructured into focused
  subdirectories.
- Large files split; major classes (`GraphCompiler`, `RuntimeAPI`,
  `PackageManager`, `NodeGraphUI`) decomposed into helper files.
- All bundled example graphs landed at the alpha.1 graph-schema baseline.

### Release Infrastructure

- GitHub Actions release pipeline for macOS (`release-macos.yml`).
- Release validation workflow (`release-macos-validate.yml`).
- Version drift guard workflow (`version-guard.yml`).
- appcast generator script and published `site/appcast.xml`.
- Release operations documentation and checklist under `docs/release/`.
- Apple notarization + stapling; ad-hoc and Developer ID signing paths.

### Baseline Reset for This Release

- **Operator ABI reset to v1.** The ABI version counter, which had grown to
  15 during pre-release engineering, is reset to 1 for this first official
  release. ABI version is a staleness detector for plugin dylibs, not a
  cross-version compatibility promise; the reset signals the alpha.1
  baseline.
- **Graph schema reset to v1.** All bundled example and fixture graphs are
  stamped at schema v1 (the prior v3-to-v4 port/param migrations for
  `Composite`, `Mixer`, `Stack`, and `Alternate` are applied on disk in a
  one-shot migration under `scripts/reset_graph_schema_to_v1.py`). Older
  dead migration code removed from the loader.
- External operator-package repos (`vivid-3d`, `vivid-cef`, `vivid-glitch`,
  `vivid-ml`, `vivid-physics2d`, `vivid-plexus`, `vivid-wavetable`) need to
  be rebuilt against the new ABI in their respective repos. Graph files in
  those repos receive the same v3→v4 migration + v1 stamp; the rebuild of
  their dylibs is picked up automatically the next time they're compiled
  against the new Vivid headers.
