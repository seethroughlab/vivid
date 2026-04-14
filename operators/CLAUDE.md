# Seed Operators

## Purpose

This directory contains Vivid's built-in operators — the core building blocks that ship with the application. They're organized by domain and serve as both functional components and reference implementations for package and project-local operators.

## Domain Organization

| Directory | Domain | Description |
|-----------|--------|-------------|
| `gpu/` | GPU | Texture-based visual operators: noise, shape, composite, bloom, feedback, particles, text, webcam input, Syphon, etc. |
| `audio/` | Audio | Sample-based audio operators: oscillators, filters, effects (reverb, delay, distortion), drum synthesis, samplers, vocoder, etc. |
| `control/` | Control | Scalar and lane-based operators: LFO, clock, envelope, sequencers, trackers, MIDI, math, analysis, etc. |
| `shared/` | — | Reusable C++ modules shared across operators (not operators themselves) |

## File Structure Patterns

**Simple operator** — one source file, optionally with factory presets:
```
operators/gpu/noise/
    noise.cpp
    factory_presets.json    (optional)
```

**Dual-cadence operator** — shared core logic with frame-rate and audio-rate wrappers. Only operators that genuinely serve both cadences (LFO, Clock, Envelope, StepCounter, Smooth) have both variants. The audio-rate wrapper uses the unsuffixed name; the frame-rate wrapper uses an `Fr` suffix:
```
operators/control/lfo/
    lfo.h                   shared core logic
    lfo.cpp                 shared implementation
    lfo_fr.cpp              frame-rate wrapper (registers "LfoFr")
    lfo_au.cpp              audio-rate wrapper (registers "Lfo")
    factory_presets.json
```

**Audio-only control operator** — most sequencer/timing operators only need audio-rate and have a single `_au.cpp` entry point with no Fr variant:
```
operators/control/drum_sequencer/
    drum_sequencer_au.cpp           audio-rate entry point (registers "DrumSequencer")
    drum_sequencer.cpp              main implementation
    drum_sequencer_core.h/cpp       extracted shared state
    drum_sequencer_inspector.cpp    custom inspector UI
```

**GPU operators with shaders** — WGSL shader code is embedded as C++ string literals inside the `.cpp` file, not in separate `.wgsl` files. The `filters/` directory at the repo root contains standalone self-describing WGSL presets — those are a separate mechanism from the compiled GPU operators here.

**Platform-specific operators** — some operators include `.mm` (Objective-C++) files for macOS APIs:
```
operators/gpu/webcam_in/
    webcam_in.cpp
    avf_capture.h/mm        macOS AVFoundation camera capture
    capture_source.h         platform abstraction
```

## Shared Modules

`operators/shared/` contains reusable header libraries that multiple operators depend on:

| Module | Provides |
|--------|----------|
| `filter_dsp/` | Multi-mode filter DSP algorithms (header-only) |
| `drum_dsp/` | Drum synthesis utilities: pink noise, decay envelope, SVF |
| `sequencer/` | Shared sequencer logic: arpeggiator patterns, MIDI helpers, tracker data structures |
| `movie_decode/` | Video/audio decoding infrastructure |
| `movie_audio/` | Audio extraction from video files |
| `sampler_common/` | Shared sampler utilities |

The sequencer module is particularly widely used — all sequencer, tracker, arpeggiator, and drum pattern operators include headers from `shared/sequencer/`.

## Build Registration

Operators are registered in `cmake/operators.cmake` using the `add_vivid_operator()` macro:

```cmake
add_vivid_operator(noise operators/gpu/noise/noise.cpp
                   FACTORY_PRESETS operators/gpu/noise/factory_presets.json
                   EXTRA_LIBS webgpu)
```

Each operator compiles to a separate `.dylib` for hot-reload during development. Dual-cadence operators produce two separate libraries (e.g., `lfo_fr.dylib` + `lfo_au.dylib`).

## See Also

- `AGENTS.md` §Operators — file conventions, scaffolding patterns, ChildOp usage
- `docs/ARCHITECTURE.md` §5.7 — operator API contract
- `src/operator_api/CLAUDE.md` — the public headers operators include
- `cmake/operators.cmake` — build registration for all seed operators
