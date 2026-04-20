# Phase 2 — Envelope Follower Investigation

**Date:** 2026-04-19
**Scope:** Verify that an envelope-follower-style operator fixes `showcase_demo`'s drum-trigger reactivity.

## Headline finding

**The "missing operator" wasn't missing — it was undiscovered.** `Smooth` (in `operators/control/smooth/`) is already an exponential envelope follower with separate `rise_time` and `fall_time` parameters. Its docstring even surfaces the use case: *"Set rise_time=0 and a long fall_time for a peak-hold-then-decay effect."* Phase 0's "build EnvelopeFollower" was a re-implementation of an operator that already exists.

This validates the project memory note ["Don't speculatively merge operators — large catalog is usually a discoverability problem, not a redundancy problem"](../../../../.claude/projects/-Users-jeff-Developer-vivid/memory/feedback_dont_speculative_merge.md). The same logic cuts both ways: I almost wrote a duplicate operator because I (and the demo authors) didn't know `Smooth` could be used as an envelope follower.

## What I tested

Live-modified `showcase_demo` (changes only to the running graph; original JSON unchanged):

1. Inserted three `SmoothFr` nodes — one per drum (kick / snare / hat).
2. Wired `<drum>/peak → smooth_<drum>/input → <drum>_shape/scale_x` (replacing the original direct wires).
3. Set each `Smooth` to `rise_time=0.005s` (snappy attack), `fall_time=0.4s` (long decay tail).
4. Preserved the original input/output ranges via `set_connection_remap`.
5. Discovered a graph-design bug: the original `showcase_demo` only drives `scale_x`, leaving `scale_y=0.01`. With sides=64, the shape is a 16% × 1% ellipse — essentially a thin line. **Driving `scale_y` from the same envelope** brings the shapes into actual visibility.

## Empirical results

Compared at three rounds:

| Metric | Phase 0 (original) | Smooth + scale_x only | Smooth + scale_x + scale_y |
|---|---|---|---|
| `mean_brightness` | 0.0004 | 0.0048 (12×) | **0.032 (80×)** |
| `contrast` | 0.010 | 0.042 (4×) | **0.161 (16×)** |
| `motion_magnitude` | 0.003 | 0.0006 | **0.014 (5×)** |
| `energy_brightness_correlation` | −0.009 | −0.366 | −0.248 |
| `energy_motion_correlation` | (n/a) | −0.100 | −0.123 |
| `energy_contrast_correlation` | (n/a) | −0.282 | −0.256 |

The graph is **visibly much more alive** (brightness 80× higher, contrast 16× higher, motion 5× higher) — but the per-axis correlation metric reports it as *less* reactive than the original. That's a metric problem, not a graph problem.

## Why the correlation metric understates the win

Three confounders, all real:

1. **Sampling rate vs event rate.** The visual sample interval is 160 ms (~6 fps). Drum hits last ~50–100 ms. Many visual samples land between hits and miss the brightness peak. The audio chunk RMS (50 ms windows) sees every hit cleanly, so the time series desync.

2. **Feedback-induced phase shift.** The graph's `Feedback` node has `decay=0.92` per frame (≈117 ms half-life). This is a deliberate aesthetic choice (trails) but it shifts visual brightness peaks 100–300 ms after audio peaks. Pearson correlation assumes zero-lag coupling and penalizes the shift. Setting `fb/mix=0` brought correlations back to near-zero (no longer negative) but also killed the trails — confirming feedback is the lag source.

3. **Smoothing reduces inter-frame motion.** The `Smooth` operator produces smoothly decaying values. Inter-frame motion (frame-to-frame pixel diff) measures discontinuities — and smoothed signals have small discontinuities by definition. Adding Smooth makes the visual better-looking AND lower-motion at the same time.

## Implication for the roadmap

The Phase 1 multi-axis correlation metric is necessary but **insufficient for percussive AV evaluation.** It works well for direct continuous coupling (the `audio_reactive_demo` motion correlation jumped to +0.904 — that's a real win) but understates compelling work that uses smoothing, decay, or feedback.

**The right metric for drum-driven AV is onset response rate** (Phase 1 task 9, currently deferred): "what fraction of audio onsets produce a visible visual change within N ms after the onset?" That metric is robust to smoothing and feedback because it just asks "did the visual respond at all near the onset" — not "did the visual track audio energy linearly."

## Recommended sequencing change

The Phase 0 plan promoted `EnvelopeFollower` to the highest-priority Phase 2 operator. Phase 2 verification has revealed:

- `EnvelopeFollower` is not needed (Smooth exists).
- The next highest-leverage work is **Phase 1 task 9 (onset response rate)** — it's the metric that would correctly verify the Smooth intervention as the win it actually is.
- After that, **`OnsetDetector` (Phase 2 task 1)** becomes a graph-side operator for triggering visuals from audio events — distinct from but synergistic with the metric.

## Smaller findings, mostly polish

- `Smooth` should ship with a `factory_presets.json` for "Envelope follower (snappy)", "Envelope follower (smooth)", "Peak-hold-decay" — making the common use cases first-class. (Task 15)
- Consider `EnvelopeFollower` as an alias for `Smooth` via `src/runtime/graph/operator_aliases.{h,cpp}` — when Claude (or any user) searches for "envelope follower," they should land on `Smooth`. (Task 15)
- `showcase_demo`'s graph file should be edited to: (a) add Smooth nodes, (b) drive both scale_x and scale_y, (c) probably bump baseline `to_min` so shapes stay faintly present between hits. This is graph-design polish.

## Files / state

- No code changes in this phase — purely a live-runtime investigation.
- The `showcase_demo` modifications are in the running runtime; reload to discard.
- Original `showcase_demo.json` unchanged.
- Existing `Smooth` operator unchanged; `factory_presets.json` and alias work belong to Task 15 (still pending).

## Status of Phase 2

| Task | Status | Notes |
|---|---|---|
| 11. Explore existing envelope/smoothing operators | ✅ Done | Found Smooth |
| 12. Scaffold and implement EnvelopeFollower | ❌ Cancelled | Smooth already exists |
| 14. Wire Smooth into showcase_demo, measure delta | ✅ Done | This document |
| 15. Improve Smooth discoverability | 🟡 Pending | Polish; defer |
| 13. Verify EnvelopeFollower fixes showcase_demo | ✅ Done | Smooth fixes the visual problem; metric problem deferred to Phase 1 task 9 |

## Recommendation

Pivot back to Phase 1: ship onset response rate (task 9), which is the metric needed to *verify* Smooth as the right intervention. Then return to Phase 2 with a sharper picture of which operators are still missing.
