# Live monitoring (mod-input preview + output scope + MPE)

## What it is

Three closely-related features that all depend on the editor seeing **live state** beyond the operator's own output:

1. **Mod-input preview** — show the effective `position` and `warp_amount` (base + modulation) as the live graph feeds `position_mod` / `warp_mod` into the operator. A secondary cursor on the preview canvas.
2. **Output scope** — a small waveform scope in the side panel showing the actual audio coming out of the oscillator. Confirms that the preview polyline matches what's being heard.
3. **MPE / per-voice visualization** — overlay each active voice's pitch offset and gate state on the preview canvas, so MPE-driven patches are legible.

All three share a gap: today's `VividEditorContext` exposes `param_values` (what the operator was told to do), `output_values` (what the operator emitted as scalar/step outputs), but **not** live input lane-array state and **not** recent audio output. Without those, these three features don't work.

## Why deferred from v1

Platform work, same flavour as [audition](audition.md) and [file-drop-import](file-drop-import.md). One extension unlocks a set of live-monitoring features across every synth-class editor, not just WavetableOsc.

## Platform extension

Add to `VividEditorContext`:

```cpp
typedef struct VividEditorContext {
    // … existing fields …

    // Live lane-array input state. Read-only; pointer valid for the frame.
    // Useful for visualizing modulation and per-voice state without having
    // to infer from output_values.
    const VividLaneView* input_lanes;
    uint32_t             input_lane_count;

    // Ring buffer of recent audio output, if the operator has an audio
    // output port. Size is host-controlled (typically ~2048 samples,
    // ~42ms at 48kHz). null when the operator doesn't have an audio
    // output or when host chooses not to capture.
    const float*         audio_tail;
    uint32_t             audio_tail_sample_count;
    uint32_t             audio_tail_channel_count;
    float                audio_tail_sample_rate;
} VividEditorContext;
```

Host work:
- Input lanes: copy the CompiledGraph's snapshot of the node's input state into editor context each frame. Low-risk; already cached.
- Audio tail: a per-editor-window audio ring buffer. Fill from the audio thread; read from the UI thread. Needs a small lock-free SPSC pattern — doable but nontrivial.

## Editor-side cost (post-platform)

**Mod-input preview (~30 min):**
- Read `input_lanes[position_mod_index]` for the "effective position" after modulation.
- Draw a secondary cursor on the preview canvas, slightly lighter colour, showing base-position + mod-position.

**Output scope (~2 hours):**
- New side-panel region (60px tall) showing the last N samples of audio_tail as a polyline.
- Separate channel-split mode for stereo outputs.

**MPE visualization (~1 hour):**
- Read `input_lanes[frequencies_index]` for active voice pitches.
- Draw voice-count indicator on the preview canvas; colour voices by gate state.

## Interactions

- **[audition](audition.md)** — audition + output scope is the full iterative-authoring loop. Enable both together.
- **[frame-stack-visualization](frame-stack-visualization.md)** — effective-position cursor on the frame stack is the MPE sweet spot.
- **[morph-recording](morph-recording.md)** — while a morph is playing, the effective-position cursor traces the morph curve. Great visual feedback.

## Scope cuts

- **Full oscilloscope trigger modes** (free-run, rising edge, etc.): output scope should be simple. One mode (free-run latest-N-samples) is enough.
- **Spectrum in side panel**: that's [spectrum-view](spectrum-view.md); keep the time-domain scope separate.
- **Input spectrum analysis of mod_input**: niche; defer.

## Test plan

- Pure-logic: lane-array reading → cursor position mapping. Given synthesized `input_lanes` data, assert the secondary cursor draws at the right X.
- End-to-end: null `input_lanes` → editor doesn't crash, no secondary cursor drawn.
- Output scope: null `audio_tail` → panel region is blank but no crash.
- Platform-level audio-tail correctness is covered at the host level once the ring buffer lands.
