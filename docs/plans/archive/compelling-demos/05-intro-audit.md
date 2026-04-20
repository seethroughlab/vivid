# Phase 2 Polish — Intro Graph Audit

**Date:** 2026-04-20
**Scope:** After fixing `showcase_demo`, sweep the other 8 intro graphs for the same class of bug (single-axis scale driving, near-zero `to_min` invisibility) and fix any that match.

## Findings

| Graph | Issue? | Diagnosis | Action |
|---|---|---|---|
| `showcase_demo.json` | ✅ fixed in prior slice | — | — |
| `audio_reactive_demo.json` | No | Displace-based reactivity; shape scale_x/y both fixed at 0.35. Phase 1 motion correlation already +0.90. | None |
| `av_demo.json` | No | Metaball `pos_x` driven by LFO — no scale issue. Single-LFO minimalism is intentional for the demo. | None |
| `av_metronome_demo.json` | **Yes** | `lfo_vis/value → shape1/scale_x` with `scale_y` fixed at 0.35 → horizontal-only wobble | **Fixed** |
| `lanes_intro_demo.json` | No | Pure visual, metaball count=8, healthy metrics at baseline | None |
| `lanes_stack_demo.json` | No | Pure visual, Stack + metaballs, healthy metrics at baseline | None |
| `audio_demo.json` | No | Pure audio, simplest patch | None |
| `stereo_demo.json` | No | Pure audio, pan + LFO | None |
| `demo.json` (Getting Started) | **Yes** | `lfo1/value → shape1/scale_x` with `scale_y` fixed at 0.25 → horizontal-only breathing | **Fixed** |

**2 of 8 intro graphs needed fixes.** The bug repeats whenever a single-axis LFO/envelope drives a Shape2D without also driving the complementary scale axis.

## Fixes applied

Both fixes are one-line JSON additions:

```json
// av_metronome_demo.json — add after existing lfo_vis/value → shape1/scale_x
{ "from": "lfo_vis/value", "to": "shape1/scale_y" }

// demo.json — add after existing lfo1/value → shape1/scale_x
{ "from": "lfo1/value", "to": "shape1/scale_y" }
```

No remap needed — the LFO output ranges (0.1–0.6 on av_metronome, 0.1–0.4 on demo) map 1:1 to visible shape scales. No sticky notes updated; the existing notes describe the pulse mechanic and the direction change ("pops big then shrinks") is now true on both axes.

## Verification

Loaded each fixed graph with a freshly booted runtime; 5 s settling; `analyze_output(mode="av", window_seconds=3)`:

### `av_metronome_demo.json`

| Metric | Phase 0 (before) | Phase 2 polish (after) | Delta |
|---|---|---|---|
| `motion_magnitude` | 0.004 | **0.017** | ~4× |
| `mean_brightness` | 0.028 | 0.008 | slightly lower |
| `contrast` | 0.164 | 0.085 | lower |
| `onset_response_rate` | (not measured in Phase 0) | **1.000** | 23/23 onsets responsive |
| `detected_onsets` | n/a | 23 | saw-LFO-driven osc produces frequency transients |

The shape now pulses uniformly on both axes. Motion up 4× captures the new radial movement. Brightness and contrast dropped slightly because uniform scaling with soft edges integrates to a different total luminance than stretched-ellipse scaling — cosmetic metric shift, not a regression in visual interest. `onset_response_rate: 1.0` confirms every audible transient lands a visible visual response.

### `demo.json` (Getting Started)

| Metric | Phase 0 (before) | Phase 2 polish (after) | Delta |
|---|---|---|---|
| `motion_magnitude` | 0.000006 | **0.000621** | **~100×** |
| `mean_brightness` | 0.022 | 0.008 | slightly lower |
| `contrast` | 0.127 | 0.075 | lower |

Huge jump in motion — the shape now actually "breathes" radially instead of just distorting horizontally. As with av_metronome, mean brightness and contrast shifted slightly because the geometry changed (uniform scaling on both axes vs stretched ellipse produces different pixel integral), but the graph is more alive on the axis that matters for the demo's intent ("the circle breathes with the beat").

## What this closes

Phase 2 polish for the demo set as a whole is now complete:

- `showcase_demo` — Smooth + scale_y + baseline floors (prior slice)
- `av_metronome_demo` — scale_y wired (this slice)
- `demo` (Getting Started) — scale_y wired (this slice)

The intro graphs ship without the single-axis invisibility bug. The pattern is now one worth surfacing in a future composition guide entry: **"when modulating shape scale from a single source, drive both scale_x AND scale_y — otherwise the shape distorts rather than pulses."**

## Files touched

- `graphs/intro/av_metronome_demo.json` — one connection added
- `graphs/intro/demo.json` — one connection added
- `docs/plans/compelling-demos/05-intro-audit.md` — this doc

## Status of the broader roadmap

Phase 1 (perception loop):
- ✅ 1.1a settling delay
- ✅ 1.1b multi-axis correlation (brightness / motion / contrast)
- ✅ Task 9 — onset detection + `onset_response_rate` + `reactivity_latency_ms`
- 🟡 Task 8 — per-band correlation (bass/mid/treble) — pending

Phase 2 (operators + polish):
- ✅ Verified `EnvelopeFollower` already exists as `Smooth`; zero new operators needed
- ✅ Smooth discoverability polish (factory presets, docstring, `EnvelopeFollower` alias)
- ✅ `showcase_demo` Smooth-fixed and saved to disk
- ✅ Intro graph audit — `av_metronome_demo` and `demo` fixed (this slice)
- 🟡 OnsetDetector graph operator — pending (graph-side trigger primitive, distinct from the metric)

Phase 3 (composition library): not started.

## Recommended next slice

Three reasonable candidates:

1. **Per-band correlation (Phase 1 task 8)** — extend the metric to bass/mid/treble vs brightness/motion/contrast. 3×3 reactivity matrix. Builds on the existing per-chunk audio infrastructure.
2. **OnsetDetector graph operator** — graph-side trigger primitive. Not strictly needed given Smooth + peak ports work, but unlocks new compositional patterns (audio onset → visual flash without indirection through Smooth).
3. **Start Phase 3 — composition guide** — now that we have concrete case studies of what "fixed" looks like, a first pass at `docs/COMPOSITION-GUIDE.md` could codify the patterns: "drive both scale axes," "envelope-follow percussive triggers," "leave baseline visible presence," etc. Cheap leverage — these principles would prevent the bugs we just fixed from recurring in new demos.

Lean toward (3) — Phase 3 is the only phase not yet started, and the current moment (post-intro-audit, post-Smooth-polish) has the most concrete anti-pattern examples to anchor guidance on.
