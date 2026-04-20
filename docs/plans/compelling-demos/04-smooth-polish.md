# Phase 2 Polish — Lock in the Smooth/EnvelopeFollower win

**Date:** 2026-04-19
**Scope:** Save the fixed `showcase_demo` to disk; ship factory presets, an `EnvelopeFollower` alias, and a docstring rewrite so future authors find `Smooth` when they look for an envelope follower.

## What shipped

### 1. `graphs/intro/showcase_demo.json` rewritten
The hero demo now ships with three `SmoothFr` envelope followers between the drum `peak` ports and the shape `scale_x`/`scale_y` ports. Same audio, same compositional intent — but every drum hit now produces a sustained visible pulse instead of a sub-frame flash.

Three structural changes from the original:
- Each drum's `peak` feeds a `SmoothFr` (rise=0.005s, fall=0.4s) before driving the shape
- Both `scale_x` AND `scale_y` are driven from the smoother (the original drove only `scale_x`, leaving the shape as a 16% × 1% line — invisible)
- Wire `to_min` bumped from 0.01 to 0.03–0.05 so shapes have baseline presence between hits
- Sticky notes updated to explain the envelope-follower pattern; new "Why Smooth?" sticky added

Round-trip verified: `analyze_output(mode="av")` after `load_graph` reports **onset_response_rate ≈ 0.92** (12 detected onsets, 11 responsive, median latency 235 ms). The Phase 0 baseline was 0.45 with the same audio; this graph is now ~2× more reactive.

### 2. `operators/control/smooth/factory_presets.json` created

Five presets surface the common use cases as one-click starting points:

| Preset | rise_time | fall_time | When to use |
|---|---|---|---|
| Envelope follower (snappy) | 0.005 s | 0.2 s | Punchy drum-driven visuals |
| Envelope follower (long tail) | 0.005 s | 0.6 s | Sustained, ambient response |
| Peak hold + slow decay | 0.0 s | 1.0 s | Classic peak-meter behavior |
| Slew limiter | 0.05 s | 0.05 s | Smoothing stepped/quantized signals |
| Pitch glide | 0.15 s | 0.15 s | Portamento on discrete pitch changes |

Both `smooth_fr` and `smooth_au` cmake targets register the same presets file via `FACTORY_PRESETS` — verified that both `factory_presets/smooth_fr.json` and `factory_presets/smooth_au.json` end up in the app bundle on build.

### 3. `Smooth` docstring rewritten
`operators/control/smooth/smooth.h` now leads with the four common use cases (envelope follower, slew limiter, pitch glide, peak hold) instead of the abstract "first-order low-pass filter" framing. MCP `operator_docs("Smooth")` will now surface the envelope-follower pattern as the primary use case — the Phase 0 discoverability gap is closed.

### 4. `EnvelopeFollower` → `SmoothFr` alias

Added to `src/runtime/graph/operator_aliases.cpp`. Loading a graph with `"type": "EnvelopeFollower"` now resolves to `SmoothFr` with all params preserved. Verified both:
- Unit test (`test_operator_aliases`): 6 assertions pass — including the rename + param preservation contract.
- Live runtime: a one-node test JSON using `"type": "EnvelopeFollower"` loaded as a `SmoothFr` node with `rise_time=0.005, fall_time=0.4` intact.

The alias maps to the **frame-cadence** variant since envelope-following for visual modulation is the dominant use case. Users who want audio-rate smoothing can search for `Smooth` directly.

## Files touched

- `graphs/intro/showcase_demo.json` — full rewrite with Smooth nodes + scale_y wiring + updated sticky notes
- `operators/control/smooth/factory_presets.json` — new
- `operators/control/smooth/smooth.h` — docstring rewrite
- `cmake/operators.cmake` — added `FACTORY_PRESETS` to both smooth_fr and smooth_au registrations
- `src/runtime/graph/operator_aliases.cpp` — added `EnvelopeFollower` entry
- `tests/graph/test_operator_aliases.cpp` — new (covers no-alias + EnvelopeFollower paths)
- `cmake/tests/10-runtime-control-graph.cmake` — registered the new test executable

## Test status

- `test_operator_aliases`: 6 assertions, all PASS (new)
- `test_output_analyzer`: 35 assertions, all PASS (unchanged)
- Full runtime build: clean
- Live verification: `showcase_demo.json` round-trips with `onset_response_rate ≈ 0.92`, `detected_onsets = 12`
- EnvelopeFollower alias: end-to-end verified via JSON load_graph

## What this closes

The Phase 2 verification doc identified two follow-ups that this slice resolves:
- ✅ **"Save the fixed `showcase_demo` to disk"** — done; the hero demo now ships in the alive state by default
- ✅ **"Ship `Smooth` factory presets"** — done; five presets covering the common use cases
- ✅ **"Add EnvelopeFollower alias via operator_aliases"** — done; resolves to SmoothFr through the JSON parse path

## Status of the broader roadmap

Phase 1 (perception loop):
- ✅ 1.1a settling delay — documented + `wait_for_audio_settle` MCP tool
- ✅ 1.1b multi-axis correlation — brightness + motion + contrast
- ✅ Task 9 — onset detection + `onset_response_rate` + `reactivity_latency_ms`
- 🟡 Task 8 — per-band correlation (bass/mid/treble) — pending

Phase 2 (operators):
- ✅ Verified `EnvelopeFollower` already exists as `Smooth`; Phase 2 ships zero new operators
- ✅ Phase 2 polish: presets, alias, fixed hero demo, docstring (this slice)
- 🟡 OnsetDetector graph operator (graph-side trigger primitive) — pending; useful but not blocking

Phase 3 (composition library, reference corpus, pattern library) — not started.

## Recommended next slice

Two candidates:
1. **Phase 1 task 8 — per-band correlation.** Adds bass/mid/treble vs brightness/motion/contrast — turns the existing single-axis correlation into a 3×3 reactivity matrix. Lets Claude detect "bass drives brightness, treble drives motion" patterns. Builds on the existing per-chunk audio infrastructure; ~similar scope to onset detection.
2. **Audit other intro graphs.** Phase 0 found that several intro graphs share the "drives scale_x but not scale_y" bug, plus `to_min: 0.01` keeps shapes invisible. Worth a sweep to apply the same fix pattern to other graphs while it's fresh. Lower-effort, immediate compounding value for the demo set as a whole.
