# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
- Input: Webcam, MovieFileIn, MidiInput, Interactive input system
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

## [0.1.0-alpha1] - 2026-03-06

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
  - `docs/RELEASE-OPS.md`
  - `docs/RELEASE-CHECKLIST.md`

### Changed
- Updated macOS app icon assets (`Vivid.icns`, iconset, SVG source)
- Updated onboarding/docs references to canonical `seethroughlab/vivid` release location

### Notes
- This is the first alpha pre-release tag for Vivid 0.1.0.
