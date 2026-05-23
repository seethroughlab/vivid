# Plan: Audio Clip Waveform Editor

## Context

`AudioClip` should feel like a clip player, not a collection of numeric loop
params. The right primary surface is a dedicated editor window with an
Ableton-style waveform view: input/output boundaries, loop brace, live playhead,
tempo grid, and room for future warp markers.

The flat params remain important as the automation/API layer, but the editor is
the authoring surface. As with other `VIVID_EDITOR` adopters, the inspector
should stay passive: default params, thumbnail/status, and an Open Editor
button.

---

## Goals

- Add a dedicated `VIVID_EDITOR(AudioClip)` window using the existing editor
  stack described in `docs/runtime/editor_windows.md` and
  `docs/operators/editor-ui.md`.
- Make waveform boundaries and loop editing direct, visual, and undo-aware.
- Fix the current playback correctness issues before leaning on the editor:
  external beat-phase playback, Signalsmith latency/end flushing, source sample
  rates, and scalar output buffers.
- Reserve a graph-serializable warp-marker model for v2 without making v1 parse
  marker data on the audio thread.

## Non-Goals For V1

- Warp-marker editing or nonlinear time mapping.
- Reverse playback.
- Loop crossfades.
- A second rich inspector UI. The dedicated editor is the rich UI.

---

## Phase 1: Playback Contract Hardening

Before adding the editor, make the underlying operator truthful and testable.

### External Mode

Choose one v1 behavior:

- Preferred: render actual audio from `beat_phase` per sample, using interpolation
  over the clip region.
- Acceptable fallback: remove `rate_mode=external` from v1 and keep only
  `free` and `metronome` until audio-rate phase playback is correct.

Do not keep the current block-rate sample-and-hold behavior.

### Signalsmith Lifecycle

Define and implement a clear stretcher lifecycle:

- Reset/seek must not skip the first audible transient.
- Non-looping playback must flush or account for stretcher latency before
  emitting `done_out` and stopping.
- Looping playback must not produce early done pulses or underrun silence at
  the loop boundary.
- Pitch/time changes should either update the stretcher live without reset or
  deliberately reset with documented audible behavior.

### Source Sample Rate

Decide one policy and test it:

- Preferred: resample decoded PCM to the runtime audio rate on load, so stretch
  and simple playback share one source-rate domain.
- Alternative: prove Signalsmith input-rate behavior supports feeding native-rate
  PCM into a runtime-rate stretcher, then encode that assumption in tests.

### Scalar Outputs

`position_out` and `done_out` are scalar audio-domain outputs. Fill their whole
output buffers consistently so both direct audio consumers and frame-side bridge
readback see coherent values. `done_out` should be a deliberate pulse, not stale
buffer contents.

---

## Phase 2: Parameter Model

Separate clip boundaries from loop boundaries.

### Params

Add:

| Param | Type | Default | Description |
|---|---:|---:|---|
| `clip_start` | float | `0.0` | Normalized source input boundary. |
| `clip_end` | float | `1.0` | Normalized source output boundary. |
| `loop_enabled` | bool/int | existing loop value | Enables loop brace playback. |
| `loop_start` | float | `0.0` | Normalized loop start, constrained inside clip range. |
| `loop_end` | float | `1.0` | Normalized loop end, constrained inside clip range. |
| `warp_points` | TextValue | `""` | Reserved v2 serialized marker data. Hidden/editor-only in v1. |

Keep existing graphs compatible:

- If a graph has only old `loop_start` / `loop_end`, treat them as the loop
  region and use `clip_start=0`, `clip_end=1`.
- If a graph has new clip params, use them directly.

Use param descriptions and visibility hints so the flat inspector is readable,
but do not duplicate the waveform editor there.

---

## Phase 3: Editor Data Preparation

Prepare waveform overview data off the audio thread.

### Waveform Overview

When the file changes on the main thread:

- Decode or reuse decoded PCM.
- Build immutable min/max bins for each channel.
- Store duration, frame count, source sample rate, and optional display metadata.
- Swap the overview data alongside `ClipState`, or keep a parallel atomically
  swapped editor cache.

Use enough bins for deep zoom without rebuilding every frame. A practical v1 is
4096 to 16384 bins per channel, with min/max pairs.

### Thread Rules

- `process_audio()` reads only immutable prepared playback data.
- `draw_editor()` reads immutable prepared display data.
- No parsing, allocation-heavy waveform generation, or file decoding happens on
  the audio thread.
- Editor writes params only through `ctx.commands.set_param` /
  `ctx.commands.set_string_param`, preserving undo behavior.

---

## Phase 4: Editor V1

Implement the editor as `AudioClip::draw_editor(...)` plus shared helper code
for testable math.

### Suggested Files

- `operators/audio/audio_clip/audio_clip.cpp`
- `operators/audio/audio_clip/audio_clip_editor.cpp`
- `operators/audio/audio_clip/audio_clip_editor_shared.h`
- `operators/audio/audio_clip/audio_clip_editor_shared.cpp`

Register extra editor sources in `cmake/operators.cmake` with the `audio_clip`
target.

### Layout

Use existing editor helpers from `operator_api/editor_ui.h`:

- Top row: file name, duration, source sample rate, `file_bpm`, stretch state,
  and rate mode.
- Main lane: stereo waveform min/max display, clip start/end handles, loop brace
  handles, selected/hover states, and a live playhead.
- Bottom ruler: seconds ticks, plus beat grid when `file_bpm > 0`.
- Side panel: exact controls for clip start/end, loop start/end, loop toggle,
  stretch, rate mode, speed, pitch, and file BPM.

### Interactions

- Drag `clip_start` and `clip_end` handles with clamping and a minimum region
  length.
- Drag `loop_start` and `loop_end` handles constrained inside the clip region.
- Drag the loop brace body to move the loop region without changing length.
- Zoom and pan horizontally using `Viewport1D`.
- Set cursor and status text via `ctx.host` while hovering or dragging handles.
- Do not implement double-click audition unless a safe seek/audition command is
  added to the audio engine.

### Drawing

- Draw waveform bins as min/max vertical bars or filled strips in the audio
  domain accent color.
- Draw clip boundaries more strongly than loop boundaries.
- Draw inactive regions dimmed outside `clip_start` / `clip_end`.
- Draw the playhead from `position_out` or the operator's current playback
  position snapshot.

---

## Phase 5: Warp Markers V2

Reserve the data model now, but do not implement nonlinear playback in v1.

### Serialized Shape

Store warp markers in `warp_points` as compact JSON text:

```json
[
  {"source_sample": 0, "beat": 0.0},
  {"source_sample": 48000, "beat": 1.0}
]
```

Validation rules:

- sorted by `source_sample`;
- unique source positions;
- nondecreasing beat positions;
- markers must fall within the decoded source frame range.

### Runtime Shape

When warp playback is implemented, parse `warp_points` outside the audio thread
and compile it into an immutable warp map. The audio thread consumes only that
compiled map.

### Editor V2

The editor should draw warp markers as beat anchors over the waveform and allow
dragging source or beat positions. The beat grid should deform according to the
compiled map so users can see the effect before playback.

---

## Test Plan

### Build

```bash
cmake --build build --target audio_clip
```

### Unit Tests

Add focused helper tests for:

- waveform min/max bin generation;
- normalized time/sample conversion;
- handle hit-testing;
- clip and loop drag clamping;
- loop brace movement preserving length;
- warp-point parse/sort/validation helpers.

### Audio Behavior Tests

Cover:

- stretch-on playback preserves the first transient;
- non-looping clips finish after the stretcher tail is accounted for;
- 44.1 kHz and 48 kHz WAVs play at correct pitch and duration;
- `position_out` advances monotonically through the active region;
- `done_out` emits exactly one pulse for non-looping playback;
- loop playback never emits `done_out`.

### UI Smoke

Add a narrow editor smoke fixture:

- create a graph with one `AudioClip` node and a small test WAV;
- open the editor;
- capture the editor surface;
- drag clip and loop handles with scripted input;
- verify params changed via `inspect`;
- verify the inspector exposes Open Editor and does not duplicate the waveform
  editing UI.

Use the existing editor-window test path described in
`docs/runtime/editor_windows.md`.

---

## Acceptance Criteria

- Existing `AudioClip` graphs continue to load.
- Dropping a WAV can create/open an `AudioClip` node and its editor.
- The editor displays a recognizable waveform without blocking audio.
- Dragging handles updates graph params through the normal command sink.
- Playback honors the same boundaries visible in the editor.
- v1 ships with no audio-thread parsing/allocation for editor or warp metadata.
