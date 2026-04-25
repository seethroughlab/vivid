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

**Standard operator** — single source file matching the dir name. Most operators land here:
```
operators/control/drum_sequencer/
    drum_sequencer.cpp                      registers "DrumSequencer"
    drum_sequencer_core.h/cpp               shared core + compute() + thumbnail
    drum_sequencer_editor.cpp               custom VIVID_EDITOR window
    drum_sequencer_editor_shared.{h,cpp}    pure-logic helpers shared with tests
```
The cmake target name matches the operator dir name (e.g. `drum_sequencer`); the registered `kName` is whatever the `VIVID_REGISTER` block names (e.g. `"DrumSequencer"`). All control operators run at audio rate; frame-rate consumers (GPU shaders, etc.) read the operator's outputs through the cross-cadence bridge transparently.

**ChildOp-embeddable operator** — a few control operators (currently `lfo`, `envelope`, `smooth`) are embedded into other operators via `ChildOp<T>`. They register two surfaces:
```
operators/control/envelope/
    envelope.h                  embeddable Envelope class + math (consumed via #include)
    envelope.cpp                registered audio Envelope (kName "Envelope") + VIVID_REGISTER
    envelope_embeddable.cpp     out-of-line virtuals + thumbnail; linked into both
                                envelope.dylib and vivid_embeddable_op_support.a
    factory_presets.json
```
Consumers like `modulated_gain.cpp` `#include "envelope.h"` and host an `Envelope` instance via `ChildOp<Envelope>`. They link `vivid_embeddable_op_support` to pick up the out-of-line definitions. The dylib also links that same static lib so its own thumbnail comes from the shared file. The `_embeddable.cpp` filename signals "this is what ChildOp consumers see"; nothing else uses that suffix.

(Historical note: `_au` and `_fr` cadence suffixes were retired during the operator naming consolidation — every operator is now single-cadence.)

**GPU operators with shaders** — WGSL shader code is embedded as C++ string literals inside the `.cpp` file, not in separate `.wgsl` files. The `filters/` directory at the repo root contains standalone self-describing WGSL presets — those are a separate mechanism from the compiled GPU operators here.

**Platform-specific operators** — some operators include `.mm` (Objective-C++) files for macOS APIs:
```
operators/gpu/webcam_in/
    webcam_in.cpp
    avf_capture.h/mm        macOS AVFoundation camera capture
    capture_source.h         platform abstraction
```

## Choosing a UI Surface

Every operator exposes itself through one of three UI surfaces. The tiers are mutually exclusive — pick exactly one.

### Tier 1 — Default params (no macro)

Parameters render as auto-generated sliders, toggles, and enums in the inspector sidebar. Covers the majority of operators. Nothing extra to write.

### Tier 2 — Custom inspector (`VIVID_INSPECTOR`)

Override the inspector section with your own paint, Unity-editor-style. The host gives you a width and scroll position; you lay out whatever mix of param controls, custom widgets, and read-only status reads best for this operator. Embedded in the sidebar, always visible when the node is selected, sharing the main window's input stream.

Use when:
- You need a small custom visualization or control that reads alongside normal params — a waveform scrubber, grain cloud, ADSR curve, chord grid, voice/CPU meter.
- The UI is additive to the param list, not a replacement authoring mode.
- Keyboard interaction, if any, is incidental — you do not need to capture focus away from the node graph.

Current examples: `granular_synth`, `sampler`, `arpeggiator`, `note_pattern`, `chord_progression`.

### Tier 3 — Editor window (`VIVID_EDITOR`)

Dedicated native OS window, one per node instance, opt-in via the inspector's **Open Editor** button or **Cmd+E** / **Ctrl+E**. Your operator owns the whole surface, receives its own pixel-space input, and has access to clipboard, cursor control, pointer capture, and focus via `VividEditorHostAPI`. Geometry persists per operator type.

Use when:
- The operator *is* the thing being authored — grids, multi-selection, drag handles, keyboard entry, clipboard.
- The UI needs dedicated keyboard focus, pointer capture, or more vertical space than the sidebar comfortably gives.
- The authoring task has its own mental mode separate from parameter tweaking.

Current examples: `drum_sequencer`, `mseg`, `sequencer`.

When an operator adopts Tier 3, its inspector reverts to Tier 1 shape (default params + thumbnail + an Open Editor button). Do not ship both `VIVID_INSPECTOR` and `VIVID_EDITOR` on the same operator — the dedicated editor becomes the sole interactive authoring surface, and maintaining two divergent UIs is a constant source of drift. Recent DrumSequencer adoption (`drum_sequencer_inspector.cpp` deletion) is the reference migration.

### Deciding between Tier 2 and Tier 3

The test is not "how big is the UI." It is:

> Does this UI want dedicated keyboard focus, clipboard, pointer capture, or more vertical space than the sidebar comfortably gives?

If yes → Tier 3. If no → Tier 2 is enough, even for moderately rich custom paint.

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

Each operator compiles to a separate `.dylib` for hot-reload during development. The cmake target name matches the operator dir name and the dylib filename (e.g. `operators/control/lfo/` → `lfo` target → `lfo.dylib`).

## See Also

- `AGENTS.md` §Operators — file conventions, scaffolding patterns, ChildOp usage
- `docs/ARCHITECTURE.md` §5.7 — operator API contract
- `docs/COMPOSITION-GUIDE.md` — patterns/anti-patterns for AV graphs; useful when designing example graphs for a new operator
- `src/operator_api/CLAUDE.md` — the public headers operators include
- `cmake/operators.cmake` — build registration for all seed operators
