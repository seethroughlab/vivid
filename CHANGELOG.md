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
