---
name: reactive-visuals
description: >
  How to author music-reactive visuals in Vivid that read as OBVIOUSLY caused by the sound (not
  screensaver wiggle). Use whenever building, tuning, or reviewing a reactive visual graph — mapping
  audio to 3D scene params, choosing what should react, or diagnosing a flat/dead-looking output.
  Covers the legibility principle, the band→role convention, mechanical templates, anti-patterns, the
  measure-every-change loop via analyze_output, and a dead-graph decision tree.
---

# Authoring obviously-reactive visuals

Vivid's plumbing is rich (a full 3D scene graph, a good mapping engine, note/beat/energy signal
sources). The hard part is **legibility** — making a viewer *see the music causing the form*. Most
weak clips fail here, not for lack of operators. This skill is the design method; it does not replace
taste, and it is not a list of "good" looks to imitate.

## The one principle that matters most

**Reactivity reads as caused-by-sound only when it is PUNCTUAL or MONOTONIC-AND-LARGE.**

- **Punctual** — a discrete audio event triggers a discrete, visible visual event. A note-on spawns a
  burst; a kick fires a flash/scale-pop. The eye locks audio↔visual within ~80ms.
- **Monotonic-and-large** — a continuous audio feature drives one obvious, large excursion. Bass
  inflates the whole form; energy opens the camera. One big axis, not ten small ones.
- **Anti-goal: generic wiggle.** Subtle continuous modulation spread across many params reads as an
  animation that happens to be *near* music, not *caused by* it. If you can't point at the exact
  audio event that made a visible change, the viewer can't either.

## Band → role convention (start here, then deviate with intent)

| Audio | Visual role | Main-graph example |
|---|---|---|
| **bass / `master.low`** | **scale / impact** — big monotonic inflation | `Shape3D` scale, `Deformer` amount, `SDF3D` radius |
| **mids / `master.mid`** | **motion / color** | camera drift, hue/palette phase, `InstanceNoise` amount |
| **highs / `master.high`** | **detail / sparkle** | instance count, emissive/highlight, particle rate |
| **note-ons** (`track_<id>.note`, `Notes`→`InstancesFromSignal`) | **spawn / seed** — the punctual event | new instance/burst per note; chords bloom, arps trail |
| **quiet** | **empty** — let it go dark/still so hits read | low baseline, but keep a faint presence (below) |
| **beat/bar** (`transport.beat_pulse`, `transport.downbeat`, `transport.bar_phase`) | **choreography** — hit ON the beat, cut scenes on the bar | pulse scale on the beat; `Clock`→`Switch3D` cuts on the bar |

> **Most demos ignore the transport pulses.** `transport.beat_pulse` / `transport.downbeat` /
> `transport.bar_phase` let a visual hit *on the beat* rather than only trailing loudness — the single
> cheapest legibility win. Reach for them.

## Five core rules

1. **Audio is animation, not background.** Wire `audio event → envelope → visual param`. Do NOT run a
   free LFO "in parallel" with the music and hope it lines up — that's two performances, not one.
2. **Layer 3+ reactivity axes** (scale, color, and a discrete spawn) — a single strong axis still
   reads as monotone.
3. **Drive both geometric axes together.** Scaling only `scale_x` stretches a shape into an ellipse;
   drive `scale_x` AND `scale_y` (and `scale_z` in 3D) to pulse it.
4. **Leave baseline presence.** Don't let a mapped param collapse to ~0 between hits or the form
   flickers into existence. Keep the mapping's low end (`lo` / `out_lo`) at ≈0.03–0.05 of the range so
   it's always faintly there, growing on hits.
5. **Measure, don't guess.** After every reactivity change, run `analyze_output(mode="av")` and read
   all three lenses (below). Metrics don't replace your eye — they catch failure modes (sub-frame
   flicker, coupling on the wrong axis) before you ship.

## Mechanical templates

The mapping wire in Vivid is `{src, dst:"node:<id>.<param>", amt, attack, release, curve, lo, hi,
inv}` (via `connect_mapping` / `map_audio_to_visual_param`). `amt` is excursion gain over `[lo,hi]`;
`attack`/`release` are one-pole envelope times (fast attack snaps up on a hit, slow release glides
back).

- **A — Punctual pulse (percussive).** `master.transient` (or a track's note/gate) → scale on all
  axes, with a snappy envelope: `attack≈0.005, release≈0.25`, `lo≈0.05`. One form per drum voice reads
  as an immediate pairing. Expect `onset_response_rate > 0.85`.
- **B — Monotonic drive (sustained).** `master.low` or `master.level` → one big param (Shape3D scale,
  Deformer amount, camera dolly) directly, gentle smoothing. Expect `energy_motion_correlation > 0.5`
  (or brightness, depending which axis you drove).
- **C — Beat choreography.** `transport.beat_pulse`/`downbeat` → a scale/flash hit; `Clock`→`Switch3D`
  to cut scenes on the bar. Deterministic phase-lock rather than emergent coupling.
- **D — Spectral color.** `master.high`→warm hue / `master.low`→cool, or wire `AudioSpectrum` bands to
  a palette. Reads as "the timbre is coloring the form."
- **E — Note-driven spawn (the signature look).** `Notes`→`InstancesFromSignal`/`Emitter`: each
  note-on spawns an instance/burst that fades on note-off. Chords bloom, arps trail. The most legible
  per-note reactivity Vivid has — prefer it over modulating a static mesh.

## Reading analyze_output(mode="av")

Three complementary lenses — use ALL of them:

- **Per-axis correlation** (`energy_motion_correlation`, `energy_brightness_correlation`,
  `energy_contrast_correlation`): does the visual track energy *linearly*? Good for continuous
  coupling; **breaks down** under smoothing/feedback (can even go negative from lag).
- **Onset response** (`detected_onsets`, `onset_response_rate`, `reactivity_latency_ms`): when audio
  events fire, does something visible happen within ~400ms? Robust to smoothing/feedback; blind to
  purely continuous coupling.
- **Per-band correlations** (`band_*_correlations.{bass,mid,treble}`): surfaces selective coupling
  ("bass→motion works, treble doesn't") when the overall number is near zero.

**If one lens is high and another is low, that's information, not a bug** — it tells you which *kind*
of reactivity you built. Overall correlation ≈ 0 with high onset_response_rate = event-driven and
alive; trust the onset rate.

### Trustworthy thresholds (mechanically-working, NOT aesthetic pass/fail)

A graph can hit every number below and still be boring; a deliberately dim/ambient piece can miss some
and be exactly right. Read them as "consistent with a working reactive graph."

| Metric | Working range |
|---|---|
| `onset_response_rate` | > 0.7 for anything percussive (0.85+ is great) |
| `energy_motion_correlation` | > 0.5 for continuous-drive graphs |
| `reactivity_latency_ms` | < 300 (higher → your release/feedback is too slow) |
| `motion_magnitude` | > 0 and rising with energy — this is a small absolute frame-diff number; judge motion via `energy_motion_correlation`, not this value. ~0 = static |
| `mean_brightness` | 0.05–0.4 (< 0.02 usually means nothing's rendering) |

## Anti-patterns

| Symptom | Likely cause | Fix |
|---|---|---|
| Form distorts into an ellipse instead of pulsing | single-axis scale | drive `scale_x`+`scale_y`(+`scale_z`) together |
| Form flickers in/out between hits | mapping `lo` at ~0 | raise `lo` to 0.03–0.05 |
| "Reactive" but `energy_brightness_correlation` ≈ 0 | coupling drives motion/position, not luminance | check `energy_motion_correlation` |
| Every kick fires but correlation ≈ 0 | brief events undersampled by Pearson | read `onset_response_rate` instead |
| Rich trails/feedback but correlations are NEGATIVE | feedback shifts the visual peak later than the audio | expected — trust `onset_response_rate`, and shorten release |
| Reads as "near the music" not "caused by it" | free LFO in parallel with audio | replace with `audio → envelope → param` |

## Diagnosing a dead / weak graph

Run `analyze_output(mode="av", window_seconds=3)` (app must be PLAYING and settled ~0.5–4s after
load), then:

```
status == "insufficient_samples"
└── app isn't playing / just loaded → play, wait ~1s, retry

mean_brightness < 0.02
├── nothing feeds Output, or the form is off-camera → capture_frame; check the chain + camera params
└── a mapping collapsed scale to 0 → raise mapping lo

motion_magnitude < 0.01 (and you expected motion)
├── no mapping is actually driving a visible param → inspect_bindings / suggest_mappings
├── single-axis scale → drive both/all axes
└── release too long → shorten it

onset_response_rate < 0.3 on a percussive graph
├── driving scale from raw energy without a snappy envelope → attack≈0.005, release≈0.25
├── mapping lo at ~0 → raise to 0.05
└── feedback/decay swallowing hits → shorten release / reduce feedback

all correlations ≈ 0 but onset_response_rate high
└── the graph IS reactive — it's event-driven. Trust onset_response_rate; correlations are the wrong lens.
```

## Start from a recipe; the intent tools now apply legible defaults

- **`list_reactive_recipes`** returns proven composition patterns (punchy-drums, swelling-pads,
  note-bloom, beat-cut, spectral-color, camera-orbit) with their couplings, when to use them, the
  expected `analyze_output(av)` signature, and how to build them. Read it before wiring.
- **`connect_mapping_by_intent` and `map_audio_to_visual_param` now bake in legible defaults** — leave
  `amount`/`attack`/`release` unset and you get a *visible* excursion + a role-appropriate envelope
  (bass→scale gets a big monotonic swing; a kick gets a snappy pop; a hue gets a full sweep), not the
  old invisible `amount=1.0`-with-no-smoothing. Only set them to override. The response echoes what was
  applied. This is what the templates below assume — you rarely need to hand-tune amounts anymore.

## The authoring loop (do this, in order)

1. **Decide what should react to what** — check `list_reactive_recipes`, then use the band→role table
   to pick a *punctual* axis and a *monotonic-large* axis before wiring.
2. Build the scene graph (prefer note-driven spawns via `InstancesFromSignal`/`Emitter` for the
   punctual layer; a `Shape3D`/`Deformer`/`SDF3D` for the monotonic layer).
3. Wire mappings with `connect_mapping` — snappy envelope for punctual, gentle for monotonic, `lo`
   for baseline presence, both/all geometric axes.
4. `play`, wait ~1s, then `analyze_output(mode="av")`. Read all three lenses against the thresholds.
5. Adjust one thing, measure again. Stop when it evokes the intent AND the metrics say the coupling is
   real — not when it merely looks busy.
