# Wavetable morph recording

## What it is

A "keyframe" model for automating `position` (and optionally `warp_amount`) over time. The user records a sequence of (time, position, warp_amount) snapshots by scrubbing the preview canvas; the operator plays them back as a timed morph when triggered by gate / MIDI note / loop.

Concretely: a timeline region at the bottom of the editor where the user drops keyframes. Dragging a keyframe's Y axis sets its position; dragging its X axis sets its time offset. The operator interpolates between keyframes during playback. Bruh it's essentially an envelope for wavetable motion.

## Why deferred from v1

This is a *feature*, not a visual improvement. It adds meaningful operator behaviour:
- A new param array for the keyframe list (time + position + warp_amount per keyframe).
- A new compute-side pathway that selects the current keyframe segment and interpolates.
- A new concept of "morph playback" that interacts with the existing gate / beat_phase semantics.
- The editor gains a timeline widget (new idiom).

Each of these is ~2 hours on its own. The whole bundle deserves its own design pass; it's not an afternoon's work bundled into the WavetableOsc editor scope.

## Engine cost

**~4 hours**:
- New params: `morph_keyframe_count` (int 0..16), `morph_time_N` (0..1 normalized within a playback cycle), `morph_position_N` (0..1), `morph_warp_N` (0..1, optional — sentinel = "don't override"). That's 48 new params if we cap at 16 keyframes.
- New top-level param: `morph_enabled` (bool) and `morph_source` (Manual | Gate | BeatPhase | MIDI_Note).
- compute(): resolve current time within the morph cycle, find the two bracketing keyframes, interpolate, apply to `position` (and optionally `warp_amount`) override.
- Edge cases: fewer than 2 keyframes → no morph. Keyframes out of time order → auto-sort at compute time.

Param-index stability: all new params append after the existing 27. No churn.

## Editor cost

**~6 hours**:
- Timeline widget — new region, ~100px tall, horizontal axis = time 0..1. Click to add keyframe, drag to move, right-click to delete.
- Morph source selector + enable toggle in the side panel.
- Live playhead marker while the morph is playing (similar to step-playhead highlights elsewhere).
- Mode-specific timing readout (beats / ms depending on source).

## Interactions

- **[audition](audition.md)** — hitting audition while morph is enabled plays the full morph sequence. Completes the iterative-authoring loop.
- **[frame-stack-visualization](frame-stack-visualization.md)** — as the morph plays, the position cursor on the frame stack animates across. Beautiful visualization.

## Scope cuts

- **Per-keyframe curves** (linear vs. bezier interpolation): ship linear first. Curves can be added as a per-segment param later.
- **Multiple morph tracks** (e.g. one for position, one for warp): keep them in lockstep for v1 of this feature; decouple later.
- **Record-in-realtime** (scrub position while graph plays, record the motion): nice but needs recording-mode plumbing. Manual keyframe entry is enough to start.
- **Morph curve smoothing**: `position_smooth_ms` already exists and applies. Don't add a second smoothing layer.

## Test plan

- Pure-logic: keyframe interpolation math (linear between adjacent keyframes; edge cases at cycle boundaries; correct behaviour with 0/1/2/many keyframes).
- End-to-end: add keyframe via timeline click → captured set_param adding a keyframe at the expected time. Drag keyframe → set_param updates time + position.
- Compute: given a fixed keyframe list and a beat_phase ramp, assert the effective position follows the expected interpolation curve.

## Architectural note

Morph recording introduces "timeline" as a new idiom in the editor vocabulary. If this ships, consider whether it should live in `operators/shared/editor_ui/timeline.h` so future operators (Sequencer with automation, envelope editor beyond MSEG, etc.) can reuse it. Same pattern as `selection.h` — land in one adopter first, extract to shared when the second adopter arrives.
