# Phase 0 — Case Study Findings

**Date:** 2026-04-19
**Scope:** Empirical evaluation of the 9 graphs in `graphs/intro/` against the three-axis rubric (sonic interest, visual interest, AV coupling) defined in the strategic roadmap.

## TL;DR

The roadmap's central diagnosis is empirically confirmed and one element corrected:

- **Confirmed**: 8 of 9 intro graphs produce "very_dark" or "dark" output by the analyzer's own categorical labels. Every AV graph reports near-zero `energy_brightness_correlation` (range −0.110 to +0.092). The "audio is animation" promise is not measurable in any current intro graph.
- **Confirmed**: the hero demo (`showcase_demo`, `featured_rank: 1`) has the *lowest* visual brightness of any AV graph (mean 0.0004) — drum-trigger shapes flash too briefly to register.
- **Corrected**: the roadmap claimed `analyze_output(mode="audio")` returns no audio data. It does — but only after a 3–4 second settling window after a graph load. Earlier 3-second probes returned RMS=0 for pure-audio graphs. This is a **settling-time legibility problem**, not a broken handler. Worth noting because it changes Phase 1.1a from "wire up missing handler" to "document/handle settling delay."
- **New finding**: the existing `av_reactivity` metric is **structurally too narrow**. It only correlates audio energy with frame brightness. `audio_reactive_demo` — which physically distorts a hexagon via Displace — scores −0.11 on AV correlation because Displace doesn't change brightness. Phase 1.1b's "per-band correlation" needs to expand to multi-axis (brightness, motion, contrast, dominant_hue) or it will misjudge legitimate audio-reactive work.

## Rating Table

Loaded each graph, allowed 3s settling, called `analyze_output(mode="av", window_seconds=3)`. Categorical labels (`audio_level`, `brightness`, `contrast`, `motion`) come from the analyzer's own `summary` block; raw metrics in parentheses.

| Graph | Rank | Domains | Audio | Brightness | Contrast | Motion | AV corr | Sonic | Visual | Coupling |
|-------|------|---------|-------|-----------|----------|--------|---------|-------|--------|----------|
| `showcase_demo` | 1 | A+G+C | quiet (0.07) | very_dark (0.0004) | low (0.010) | none (0.003) | −0.009 | 3 | 1 | 1 |
| `av_demo` | 4 | A+G+C | mod (0.11) | dark (0.054) | low (0.099) | none (0.0001) | −0.014 | 2 | 2 | 2 |
| `audio_demo` | 5 | A+C | mod (0.28)\* | n/a | n/a | n/a | n/a | 2 | – | – |
| `demo` (Getting Started) | 6 | G+C | silent | very_dark (0.022) | mod (0.127) | none (≈0) | – | – | 2 | – |
| `audio_reactive_demo` | 7 | A+G+C | loud (0.41) | very_dark (0.023) | mod (0.134) | mod (0.064) | −0.110 | 3 | 2 | 2 |
| `lanes_intro_demo` | 8 | G+C | silent | dark (0.158) | mod (0.165) | mod (0.198) | – | – | **4** | – |
| `lanes_stack_demo` | 9 | G+C | silent | dark (0.193) | mod (0.210) | mod (0.171) | – | – | **4** | – |
| `stereo_demo` | 10 | A+C | needs settle | n/a | n/a | n/a | n/a | 2 | – | – |
| `av_metronome_demo` | 11 | A+G+C | mod (0.11) | very_dark (0.028) | mod (0.164) | none (0.004) | +0.092 | 2 | 1 | 2 |

\* `audio_demo` initially read RMS=0 after 3s; reading after 4s returned 0.28. Settling-time issue, not a broken handler.

Subjective ratings (1–5) are based on the metrics + my read of the graph topology. **No graph scores 4+ across all three axes.** The two pure-visual lane demos are the only ones reaching 4 on any axis, and they have no audio to couple to.

## Empirical Patterns

### Pattern 1 — The hero demo is invisible by its own metrics
`showcase_demo` (rank 1, hero) wires `kick.peak → kick_shape.scale_x` (and same for snare/hat) with a remap `from_max=0.8, to_min=0.01, to_max=0.35`. When a drum is silent (most of the time), `scale_x = 0.01` — the shape is 1% of canvas. When the drum hits, scale jumps to 0.35 momentarily, then the peak collapses back to 0. The time-averaged brightness is 0.0004 (essentially black) over a 3s window. The shapes flash but don't sustain.

This is not a metric artifact: a viewer sees mostly-black output with brief sub-frame flashes too short to register as motion (motion=0.003). The demo is "drum-driven shapes" but reads as "mostly-black screen with imperceptible flicker."

**Root cause:** `kick.peak` is an instantaneous peak port. Without an envelope follower with release tail, shapes collapse to near-invisible between hits. The `to_min: 0.01` choice compounds this — even with a sustained envelope, there's no baseline presence.

### Pattern 2 — The "audio-reactive" demo fails its own promise
`audio_reactive_demo` (rank 7, the demo explicitly named "Audio-Reactive Visuals") wires `gain1.rms → displace1.amount`. Audio is loud (RMS 0.41), the displacement is happening (motion 0.064), but `energy_brightness_correlation` is **−0.110** — slightly negative.

**Two failures stacked:**
1. **The graph fails to make audio drive a perceptually obvious change.** Hexagon is dark green (g=0.8 with default lighting it reads dim); displacement deforms it but doesn't brighten it. Even a human watching might miss the audio coupling.
2. **The metric structurally can't see displacement-based reactivity.** It only correlates audio energy with mean frame brightness. Any reactive parameter that doesn't change brightness (displacement, rotation, hue, contrast, position) is invisible to the metric.

This single graph is a **diagnostic for both the capability gap (no envelope-following / no spectral features) and the legibility gap (perception layer too narrow).**

### Pattern 3 — The "AV sync" demos aren't audio-reactive at all
`av_demo` and `av_metronome_demo` are categorized as cross-domain starters but neither has an audio→visual signal flow. Both fork a single LFO (or metronome) into both the audio path *and* the visual path in parallel. Visuals respond to the same source as audio, but never to the audio output itself.

This means **2 of the 4 cross-domain demos are parametric-parallel, not audio-reactive.** Only `showcase_demo` and `audio_reactive_demo` have actual audio→visual signal flow — and both score below 0.1 on AV correlation.

### Pattern 4 — Pure-visual graphs are healthy
The two lane demos (`lanes_intro_demo`, `lanes_stack_demo`) are the strongest output by every visual metric: mean_brightness ~0.16–0.19, contrast ~0.17–0.21, motion ~0.17–0.20. They're a useful baseline showing the visual pipeline works fine when not gated on transient audio events.

### Pattern 5 — Settling delay corrupts blind comparison
After loading a graph, audio metrics need ~4s to settle; visual metrics need ~1s. A naive evaluation harness ("load, wait 3s, measure") will sometimes report RMS=0 on a perfectly working audio graph, giving false negatives. Any Phase 1 feedback loop must encode this.

## Two Case Studies

### Case A — `showcase_demo` (almost works, highest leverage)

**Why it almost works:**
- Structural pattern is exactly right: discrete drum hits map to discrete visual elements, one-to-one
- Sticky note literally says "The music IS the animation" — the intent is clear and ambitious
- Feedback + Bloom topology suggests sustain was intended

**Why it fails in practice:**
- `peak` port is instantaneous; without an envelope follower, "drum hit" lasts <50ms in the visual domain
- `to_min: 0.01` removes baseline shape presence
- Feedback `decay: 0.92` and Bloom `threshold: 0.08` are tuned for sustained light, not sub-frame flashes
- Net result: near-black output with imperceptible flicker

**Fixes mapped to roadmap phases:**
- **Phase 2 — `EnvelopeFollower`** (attack 5ms, release 200ms) on each drum's `peak` would give sustained shapes. This is the single intervention that would change this graph from "broken" to "working."
- **Phase 3 — composition guide** would teach future authors that `peak` ports need envelope-following before driving visual scale, that `to_min` should leave a baseline visible.
- **Phase 1 — perception** would tell Claude that the current graph reads as black, so Claude wouldn't ship it.

**Improvement attempt (live):** Connected `master/rms → bloom/intensity` with remap `[0.02–0.4] → [0.5–2.0]`, intent: pulse the bloom amount with overall energy to add sustained glow. **Result: no measurable change** (mean_brightness still 0.0004, motion still 0.003). Stuck point: bloom can't amplify a black scene. The fix needed to be upstream (give shapes baseline presence), not downstream (pulse the post-effect).

### Case B — `audio_reactive_demo` (most diagnostic)

**Why it's the most diagnostic graph:**
- It's explicitly labeled "Audio-Reactive Visuals"
- It has the loudest audio (RMS 0.41), highest contrast (0.134), and only moderate motion of any AV graph
- It scores −0.110 on the analyzer's flagship AV metric
- Three different layers of the failure stack are visible at once:
  1. **Capability**: only one reactive axis (RMS → displacement amount). No spectral features, no onset detection, no envelope shaping
  2. **Legibility**: `analyze_output` can't see the reactivity that *is* present (displacement)
  3. **Judgment**: even if Claude sees the metric, it would infer "this is broken" — but a human watching would see mild distortion and call it "subtle but present"

**Stuck points logged during attempted improvement:**
- Want to add bass→hue, treble→position, onset→flash. **Blocked**: no `OnsetDetector`, no `SpectralFeatures` reduction. Could wire 512 FFT bins but ungainly.
- Want to verify each intervention measurably. **Blocked**: AV correlation only sees brightness. A correct intervention might still score 0.
- Want to know if the result is "more compelling." **Blocked**: no aesthetic rubric, no reference corpus, no diagnose-composition tool.

All three roadmap phases are needed to fix this single graph. That's the diagnostic value.

## Categorized Stuck Points (mapped to roadmap)

### Capability gaps (Phase 2)
- **No envelope follower with attack/release** — every drum-trigger demo collapses to invisibility between hits because `peak` is instantaneous. Single highest-impact missing operator.
- **No onset detector** — even if envelope-following existed, the intent on `showcase_demo` is "the kick *triggers* a flash," not "the kick energy modulates a sustained shape." Threshold + trigger logic is universal in AV; we can't author it.
- **No spectral feature reduction** — `audio_reactive_demo` is single-axis (RMS); to add hue-from-bass or scale-from-treble I would need to scaffold a custom operator first.
- **No "AudioVisualBridge" preset bundle** — every AV graph rebuilds the same wire shape (`gain.rms → param` with bespoke remap). Should be one-step.

### Legibility gaps (Phase 1)
- **`analyze_output(mode="audio")` settling delay is undocumented.** 3s probe returns RMS=0 on a working audio graph if loaded fresh. False-negative risk for any iterative refinement loop.
- **`av_reactivity` is brightness-only.** Misjudges any reactivity that drives shape, position, hue, contrast, or motion. Phase 1.1b expansion to multi-axis correlation should be the first thing Phase 1 ships.
- **No way to A/B compare interventions cheaply.** I made a change to `showcase_demo` in the live runtime, then re-ran `analyze_output` — but the window straddles the change moment, the audio settling delay reapplies, and the comparison is muddied. Need an explicit "snapshot, intervene, snapshot, diff" tool or convention.
- **Auto-inferred remaps don't always materialize.** Connecting `master/rms → bloom/intensity` got `bridge: rms` semantic but `inferred_remap_applied: false` — the wire used raw RMS values which collapsed bloom to near-zero. Either the inference rule is missing this case, or its absence should produce a warning Claude can see.

### Judgment gaps (Phase 3)
- **No principle encoded that says "shapes need baseline presence."** The author of `showcase_demo` chose `to_min: 0.01` (1% of canvas) for the resting state — invisible. A composition guide entry would be: "When mapping audio events to visual scale, leave at least 5–10% baseline so shapes are present between hits."
- **No principle encoded that says "AV reactivity needs more than one channel."** `audio_reactive_demo` ties everything to one axis; even fixed, it would feel monotone.
- **No diagnose-composition tool.** I had to read the JSON, guess the failure mode, and verify with metrics. A tool returning "graph X likely fails because Y; consider Z" would compress this enormously.
- **No reference corpus to anchor judgment.** I have no labeled "this is what compelling looks like at metrics level X, Y, Z" set.

## Recommended Sequencing Adjustments to the Roadmap

Based on these findings, three small adjustments to the strategic roadmap:

1. **Phase 1.1a is smaller than thought.** `analyze_output(mode="audio")` works; the fix is documenting/handling settling delay and exposing a `wait_for_audio_settle()` or auto-retry. Save effort here for Phase 1.1b.
2. **Phase 1.1b should expand `av_reactivity` to multi-axis FIRST**, before per-band correlation. The current single-axis (brightness) metric actively misjudges good work. Add `motion_correlation`, `contrast_correlation`, `hue_correlation` alongside the existing brightness one.
3. **Phase 2's `EnvelopeFollower` is the single highest-leverage operator** — it's the difference between `showcase_demo` working and not working. Promote it to first ship in Phase 2, before `OnsetDetector`. (OnsetDetector is more impressive in isolation but `EnvelopeFollower` fixes the existing hero demo.)

## Pointer to Next Phases

When Phase 1 work begins, **`showcase_demo` is the verification harness.** If a perception-loop change can let Claude detect "this graph is producing black output despite an active drum sequencer," Phase 1.1 is working. If, after Phase 2 ships `EnvelopeFollower`, re-authoring `showcase_demo` produces visible sustained shapes that score >0.3 on AV correlation, Phase 2 is working.

## Artifacts

- This document: `docs/plans/compelling-demos/00-case-study-findings.md`
- Capture directory created: `docs/plans/compelling-demos/captures/` (PNG capture deferred — `mode="output"` returned inline base64 too large to handle in-context; revisit with explicit file-write path on the runtime side)
- Live runtime modification to `showcase_demo` (added `master/rms → bloom/intensity` wire) was unsaved and discarded on next `load_graph`. Original graph file unchanged.
