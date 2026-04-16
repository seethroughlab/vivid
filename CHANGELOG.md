# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Execution Model
- Operators have a fixed cadence: each is either frame-rate (`_fr`) or audio-rate (`_au`), declared at compile time — no runtime inference
- 20 operators (LFO, Envelope, Clock, sequencers, etc.) are dual-cadence, implementing both `process_frame` and `process_audio`
- Cross-cadence connections use explicit `AudioFrameBridge` edges with typed dispatch
- `RuntimeCore` owns the main loop; `FrameExecutor` and `AudioExecutor` run their respective cadences; `CompiledGraph` is the sole runtime authority

### Lanes
- Lane-aware signal propagation with structural provenance tracking across all three domains
- `LaneStateService` provides identity-keyed per-lane persistent state
- Structural reshape operators (Repeat, Tile, Select) with lane modes (linear, random, phase, golden)
- Frame-domain per-lane lifting and GPU compute-backed lane evaluation
- Wire badges show lane count; node badges show lane behavior
- Lane metadata exposed in MCP introspection and UI snapshots

### Operator ABI
- ABI v11: `node_id` in process contexts, `VividOperatorKind` enum, `VIVID_PORT_SIGNAL` / `SCALAR` / `AUDIO_BUFFER` port naming
- ABI hardened: `bool` → `uint8_t`, fixed-width enums throughout
- Entry point is `vivid_process_frame`; composition via `ChildOp<T>`

### New Operators
- GPU: Cellular Automata, Reaction Diffusion, Flocking, Fluid, Instanced Shapes, Trails, MeshWarp, LUT, Scopes, SVG Render, Rich Text, Particles
- Audio: Compressor, Limiter, Convolution Reverb, DualFilter (split crossover), DrumKit, plus 11 additional operators with factory presets
- Control: PathAnimate, Macro, MSEG, Step Sequencer
- Random and Random S&H consolidated into LFO (phase output, multiple waveform modes)
- Drums, sequencers, and sampler packages merged into core

### Audio
- Multi-mode filter DSP: 14 filter types (replaces single SVF)
- Highway SIMD foundation for DSP, exposed to package builds
- Configurable audio buffer size; denormal flushing
- Polyphonic lane-array gate support in Envelope
- Per-note expression and MPE support in MidiInput
- Clock `beat_trigger` output; metronome always active with per-step division timing

### Movie Playback
- Async video decode with `MovieFileIn` (GPU) and `MovieFileAudio` (audio) operators
- AV sync with drift reconciliation, generation-based decoder flush, Metal frame upload sync
- Playback diagnostics and validation

### Modules & Modulation
- Subgraph modules: composable operator groups with param/preset proxy
- Composite-local modulation assignments
- Module-aware control server queries and introspection

### UI
- Animated splash screen with plugin scan progress
- Smooth scrolling/panning with animated node hover and selection glow
- Domain tabs and scored search in operator chooser
- Diagnostics panel with per-node audio telemetry and lane state
- Streaming build console
- Sticky notes with markdown rendering
- Clone-to-edit: double-click a node to scaffold a local editable package copy
- Multi-column inspector with param tooltips, auto-select on click, reset-to-defaults
- Custom inspectors: arpeggiator velocity bars, chord grid, interactive waveform
- N-channel audio waveform visualization
- Wire reconnect drag; node copy-paste with offset positioning
- Graph metronome transport with BPM editing
- Save As; recent files; protected default graph
- Session exploration surface with variation branching
- Configurable pan gesture (middle/left/right drag)
- `video_out` shows input texture as thumbnail; GPU thumbnails fit source aspect ratio

### Localization
- i18n with OS locale auto-detection and 21 languages
- UTF-8 glyph baking and text wrapping in the 2D renderer

### Package System
- Clone-to-edit: double-click a node to auto-scaffold a local editable copy
- Streaming build console for package builds
- Machine-readable error codes; improved error display

### MCP & Tooling
- `sample_node_outputs` for live output sampling; `set_analysis` for GPU frame analysis
- `inspect_graph` summary mode for LLM-friendly output
- Per-node audio telemetry and system health diagnostics (schema v2)
- Operator registry scan diagnostics
- Operator development MCP server with source access, build diagnostics, and WGSL docs
- Source-driven operator documentation; visual reference image analysis

### Dependencies
- nlohmann/json (replaces yyjson), efsw file watcher (replaces kqueue), libcurl (replaces shell curl), TinyXML-2 (replaces regex XML), Midifile (replaces hand-rolled MIDI parser)
- Dragonbox for float32 serialization; NanoSVG; Snappy; Highway SIMD
- argv-based `ProcessRunner` replaces shell-based process spawning
- wgpu-native switched to upstream; Syphon metallib compiled at build time

### Codebase
- `src/runtime/`, `src/ui/`, and `tests/` restructured into focused subdirectories
- Large files split; major classes (`GraphCompiler`, `RuntimeAPI`, `PackageManager`, `NodeGraphUI`) decomposed into helper files
- All 91 example graphs updated to schema v3

### Notes
- 616 commits since alpha2
- Major themes: fixed-cadence execution model, lanes completion, runtime architecture cleanup, movie playback rewrite, audio DSP expansion, localization, and dependency modernization

## [0.1.0-alpha2] - 2026-03-14

### Port Type Registry
- Transport-based custom port system replacing `VIVID_PORT_HANDLE`
- Stable type IDs using explicit namespaced strings (replaces `__PRETTY_FUNCTION__` hash)
- MIDI port type support
- `audio_safe` flag with validation; audio engine rejects non-audio-safe custom types at wire time
- Registry hardening: graceful failure on conflicts, stable_type_id validation
- Introspection: expose `stable_type_id` in JSON responses

### Operator ABI
- ABI v8: typed per-domain operator base classes and contexts
- Multi-channel audio ABI: `channels` field on `VividPortDescriptor`
- Float CV port inputs for audio operators
- Port type enum collapsed from 15 to 7 channel kinds with unified handle routing
- ABI version reset to v1 with backward-compatible aliases removed
- First-class GPU port types (`GPU_BUFFER`, `GPU_MESH`, `GPU_COMPUTE`)
- 48 built-in operators migrated to typed base classes

### Hot-Reload & Error Recovery
- Atomic swap in `OperatorLoader` with compile error propagation
- Compile errors surfaced in node tooltip, border, and inspector
- Error messages propagated to `ns.error_message` for MCP introspection

### Solo Mode
- Solo mode core: scheduler, audio engine, runtime API, snapshot
- Solo mode UI: context menu, 'S' shortcut, visual indicators, control server integration

### Audio
- New operators: Filter (SVF), Mixer (4-channel), Noise (5 colors)
- Clock operator: `beats_per_bar` param and `bar_phase` output
- MIDI buffer support in audio engine `push_params`
- Fix audio param propagation for operators without analysis ports
- Analyze `audio_out` sink node from its input buffers
- Fix audio fill-thread race on source switch in `MovieAudioOut`

### Movie & Media
- Media-stream movie session architecture with strict data-port flow
- Purge legacy `MovieFile` operators; finalize `MovieLoaded`/`MovieAudioOut` paths
- Movie operators migrated from `session_ptr` to `shared_handles` resolution
- Fix AVFoundation `dispatch_sync` deadlock on macOS
- Fix video seek-thrashing, preroll free-run, AV drift, and yellow flash on codec switch

### Operators
- New: Mouse, Keyboard, Basename, `texture_loader`
- New WGSL filters: hex grid, radial rainbow, raster grid, spirograph, voronoi, division raster
- Remove `physics_sim` and `bars`/`instance` operators
- Operator semantic metadata tags wired into CI validation
- Operator scaffold: fix empty template, add `--outputs` flag, scaffold custom port types

### Semantic Coercion
- Semantic-tag workflow with destination policy and runtime/UI hardening
- Explicit semantic coercion contract with exposed inferred remaps
- Validation matrix and tolerance regressions

### UI
- Tab-to-connect: filtered chooser opens during wire drag
- Create operator modal with configurable ports, params, and variants
- Migrate hardcoded inspectors to operator-side `draw_inspector`
- File > New (Cmd+N) resets to default empty graph
- User-expandable output port affordance for operators with >3 outputs
- Separate ports and params: hide param wires by default, split patch bay
- Wire string values directly into file/text params
- `MISSING` indicator for nodes with unknown operators
- GPU texture size inheritance tracking shown in inspector
- WGSL error scopes, transient error state, error tooltip
- Scrollbar in example browser; Cmd+Shift+P for package browser
- Centralized tab button, checkbox, and box-select border draw helpers

### Package System
- `catalog/repos.json` registry of package repositories
- Scan `factory_presets` directory when loading installed packages
- Auto-reload graph after package install/link if missing operators exist
- Async package install/uninstall/link/unlink (non-blocking UI)
- Package browser "Link Local..." button
- Move category/tags from catalog to local package manifests
- Improved error reporting and GitHub URL normalization

### Tooling
- Operator development MCP server with documentation resources
- Tool discovery extracted into shared module with env var overrides
- `graph_base_dir` added to `VividProcessContext`

### Stability
- Graph parsing hardening: validate inputs, deduplicate, clamp
- Scheduler & audio engine robustness hardening
- Operator loading & hot-reload hardening
- Package manager, catalog, settings & file watcher fixes
- Control server: API field renames, path validation, safety guards
- Skip wires referencing unknown nodes/ports instead of failing build
- Runtime/package WebGPU loading and thumbnail safety
- Plugin ABI checks hardened

### Build & CI
- Split validate vs publish release workflows; rolling-alpha policy
- Bundle and relink `libwgpu_native` for test artifacts and app launch portability
- Bundle Syphon runtime dylib; fix app/plugin rpaths
- Force arm64 architecture in release and smoke workflows
- Apple notarization fixes
- cmake `reset` target to wipe user data directories

### Docs & Website
- Website: landing page, trimmed package catalog, rename `catalog/` to `site/`
- Docs audit: fix stale operator lists, counts, URLs
- Docs reorganized into internal/testing folders

### Notes
- 143 commits over 8 days since alpha1
- Major themes: port type registry, ABI overhaul, hot-reload error recovery, solo mode, movie pipeline rewrite, semantic coercion, and broad stability hardening

## [0.1.0-alpha1] - 2026-03-06

### Core Runtime
- WebGPU graph engine with node-based visual programming
- Hot-reload operator system via dlopen
- Interactive node graph UI with thumbnails, multi-select, box select
- GPU text rendering and HiDPI support
- JSON theme system (8 built-in themes)
- macOS app bundle support

### Operators
- GPU operators: Noise, Blur, Bloom, Composite, Gradient, HSV, Mirror, Edge, Scanlines, Ramp, Dither, Transform, CRT Effect, Chromatic Aberration, Pixelate, Tile, Halftone, Levels, Displace, Posterize, Feedback, Shape, Bars
- Data-driven WGSL filter framework (19 filters as self-describing .wgsl presets)
- Control operators: Gate, Logic, Random, Sequencer, Smooth, Switch, LFO, Clock, DrumSequencer, ChordProgression
- Pattern algebra: Euclidean, PatternSeq, Stack, Alternate, PatTransform
- Composition: ChildOp<T> pattern, Instance (GPU instancing)
- Input: Webcam, MovieLoaded, MovieAudioOut/MovieVideoOut, MidiInput, Interactive input system
- Analysis: TextureAnalysis (GPU→Control readback), FFT

### Audio
- Audio engine with stereo output and null device
- WavetableSynth with filters, noise, sub, unison, wavetables
- Audio FX: Reverb, Delay, Bitcrush, Distortion
- Glitch operators: BeatRepeat, Stutter, Scratch, TapeStop, Stretch, FrequencyShift
- PlexusSynth for particle-driven ambient synthesis
- Factory presets across 17 operators
- ADSR, per-voice modulation, velocity support

### State & Presets
- State machine system with per-operator presets
- Variation system with save/recall and beat-quantized switching
- Crossfade transitions between states/presets
- Per-parameter lock flags

### Package System
- Operator package infrastructure with dependency resolution
- Package catalog with remote fetch, caching, install/uninstall
- Package browser UI with search and category tabs
- Package test runner and cmake builds

### Export & Recording
- Standalone export pipeline
- Audio export (--audio-only)
- Capture/recording infrastructure
- Screenshot CLI flag

### Tooling
- MCP server for Claude Code integration
- Control server with HTTP endpoints
- CLI subcommands: build, export, params, graph, docs, inspect, check
- MIDI learn UI and mapping persistence

### Inspector & UI
- Inspector with knobs, XY pad, color picker, collapsible groups
- Param-as-wire-source support
- Patch panel UI (replaced connection matrix)
- Performance bar with FPS/frame time/memory sparklines
- Session grid UI

### Added
- Core app update system:
  - appcast-backed `AppUpdateManager` with non-blocking startup checks
  - CLI: `vivid check-core-updates`
  - Control server endpoint: `check_core_updates`
  - MCP tool: `check_core_updates(force_refresh=false)`
- macOS update UX:
  - menu actions for `Check for Updates...` and `Automatically Check for Updates`
  - in-app non-intrusive update notice with `Install`, `Skip`, and `Later`
- Settings persistence for core update behavior:
  - `core_update_auto_check`
  - `core_update_last_checked_at`
  - `core_update_skipped_version`
- Release infrastructure:
  - GitHub Actions release pipeline for macOS (`release-macos.yml`)
  - Version drift guard workflow (`version-guard.yml`)
  - appcast generator script and published `catalog/appcast.xml`
- Release operations documentation and checklist:
  - `docs/release/RELEASE-OPS.md`
  - `docs/release/RELEASE-CHECKLIST.md`

### Changed
- Updated macOS app icon assets (`Vivid.icns`, iconset, SVG source)
- Updated onboarding/docs references to canonical `seethroughlab/vivid` release location

### Notes
- This is the first alpha pre-release tag for Vivid 0.1.0.
