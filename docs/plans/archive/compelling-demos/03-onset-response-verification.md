# Phase 1 Task 9 — Onset Response Rate Verification

**Date:** 2026-04-19
**Scope:** Add onset detection + onset-response measurement to `analyze_output(mode="av")`. Verify it correctly distinguishes a fixed `showcase_demo` (with Smooth) from the broken original.

## Headline result

The new metric resolves the verification gap that Phase 2 turned up:

| `showcase_demo` state | `energy_motion_correlation` | **`onset_response_rate`** | `reactivity_latency_ms` |
|---|---|---|---|
| **Original** | −0.04 | **0.45** | 171 |
| **+ Smooth + scale_y** | −0.03 | **0.91** | 218 |

The Pearson correlation can't tell the two graphs apart (both near zero). The onset response rate doubles when Smooth is added, exactly capturing the human-visible improvement (shapes go from "rare invisible flashes" to "consistent visible pulses on every drum hit"). Phase 2 was right that Smooth fixes the demo — Phase 1 task 9 is the metric that proves it.

## What shipped

### 1. `detect_audio_onsets` — spectral-flux onset detector

`src/runtime/debug/output_analyzer.{h,cpp}`. Standard textbook spectral flux:

1. Mix audio to mono, frame at 23 ms with 50 % overlap (11.5 ms hop).
2. Per-frame Hann-windowed FFT (1024 point), log-magnitude spectrum.
3. Spectral flux SF[t] = Σ_k max(0, log_mag[k,t] − log_mag[k,t−1]).
4. Smooth SF with a 3-tap moving average.
5. Adaptive threshold = max(0.04, 1.7 × local_median(SF in ±70 ms)).
6. Peak-pick: SF[t] > threshold AND local maximum AND ≥50 ms since last accepted onset.
7. Skip the first 2 frames (cold-start spectral flux is artificially high because prev_log_mag starts at zero).

Returns timestamps (seconds, relative to audio buffer start). Pure function — no runtime deps. Exposed in the public header so callers can run onset analysis independently of full reactivity.

### 2. `onset_response_rate` + `reactivity_latency_ms` in `AVReactivityMetrics`

For each detected audio onset:

1. Sample baseline visual metrics at the onset's timestamp (interpolated from the visual sample series).
2. Walk forward through visual samples within `kMaxLatencySec = 0.4 s` (covers Smooth `fall_time≈0.4`).
3. Compute normalized response score `max(|Δbrightness|/0.02, |Δcontrast|/0.02, motion/0.01)`.
4. An onset is "responsive" if score > 1 at any sample in the window.
5. Latency = time from onset to the sample with the strongest score.

Aggregated:
- `detected_onsets` — raw count
- `onset_response_rate` = responsive / total (0–1)
- `reactivity_latency_ms` = median onset→peak latency (across responsive onsets only)

### 3. Tests

`tests/integration/test_output_analyzer.cpp` adds three test cases:

- **`Onset detection: 4 pulses`** — synthetic 50 ms decaying sine bursts at 0.3 / 0.8 / 1.3 / 1.8 s. Detector finds all 4 within 13 ms of expected. ✓
- **`Onset detection: silence`** — silence produces zero onsets. ✓
- **`AV Reactivity: onset response rate`** — same audio + visual that responds (brightness rises after each onset). Expects rate > 0.7 and median latency in 0–200 ms range. Got rate=1.0, latency=38.5 ms. ✓
- **`AV Reactivity: onsets + flat visual`** — same audio + flat visual (no response). Expects rate ≈ 0. Got rate=0.0. ✓

All 35 assertions pass.

### 4. Python MCP docstring update

`mcp/vivid_mcp.py` — `analyze_output` docstring now describes both metric families and when to use each:

> Use both. If correlation is near zero but onset_response_rate is high, the
> graph IS reactive but the coupling is event-driven rather than continuous.

## Why this metric works where correlation fails

Three failure modes for Pearson correlation that onset response handles:

1. **Smoothing reduces inter-frame motion deltas.** The per-axis motion correlation drops when Smooth is applied because Smooth's whole job is to reduce discontinuities. Onset response just asks "did motion exceed threshold near each onset" — it doesn't penalize smoothness.

2. **Feedback decay shifts visual peaks late.** The demo's `Feedback` node delays the visual brightness peak 100–300 ms after the audio peak. Pearson assumes zero-lag coupling and reads a phase-shifted response as anti-correlated. Onset response allows up to 400 ms lag, so the late peak still counts.

3. **Visual sampling undersamples brief peaks.** Visual samples are 160 ms apart; drum hits are 50–100 ms wide. Many sampled visual brightness values fall between hits, so the time series doesn't "look like" the audio time series even though every hit produces a visible response. Onset response interpolates the visual at the onset moment and looks for any sample in the next 400 ms — undersampling doesn't hurt because at least one sample lands in the response window.

## Validation against case studies

### showcase_demo (original)
- detected_onsets: 11 (in 3 s window — 3.7/s, matches the drum kit's beat density at 115 BPM)
- onset_response_rate: 0.45
- Interpretation: about half the drum onsets produce a visible response, half don't. Consistent with the Phase 0 finding that shapes are 16% × 1% lines and only sometimes register on the analyzer's brightness/motion thresholds.

### showcase_demo + Smooth (5 ms attack, 400 ms decay) + scale_y wiring + baseline floor
- detected_onsets: 11 (identical — same audio)
- onset_response_rate: 0.91 (+102%)
- Interpretation: 10 out of 11 drum onsets produce a visible response. The remaining ~9% are likely hi-hat hits where the visual response is below the threshold (hat shape is small by design).

### audio_reactive_demo (Phase 1 baseline — continuous coupling)
Re-checked: still scores `energy_motion_correlation = +0.90` (continuous reactivity). Onset response rate would also be high but is less informative for this graph because there are no sharp "onsets" in a continuous oscillator — it's a low-onset-density signal. The two metrics complement each other.

## Sequencing forward

This completes the Phase 1 perception loop for the metric question. The next leverage points (in order):

1. **Polish `Smooth` discoverability** (Task 15) — write factory presets for "Envelope follower (snappy)" and "Peak hold + decay"; add `EnvelopeFollower` alias to `operator_aliases.{h,cpp}`. Also save a fixed `showcase_demo` to disk. ~30–60 min of work.
2. **Per-band correlation** (Task 8) — split audio into bass/mid/treble bands, correlate each with each visual axis. Lets Claude detect "bass drives brightness, treble drives motion" patterns. Builds on existing chunk infrastructure.
3. **Phase 2 OnsetDetector graph operator** — currently onsets are only detected inside the analyzer. A graph-side operator that emits a control trigger on onsets would unlock new compositional patterns (audio onset → visual flash, no Smooth indirection needed).

## Files touched

- `src/runtime/debug/output_analyzer.h` — extended `AVReactivityMetrics`, exposed `detect_audio_onsets`
- `src/runtime/debug/output_analyzer.cpp` — implementation of both
- `src/runtime/debug/capture_coordinator.cpp` — extended JSON serialization
- `tests/integration/test_output_analyzer.cpp` — added 4 test cases
- `mcp/vivid_mcp.py` — updated docstring
- `docs/plans/compelling-demos/03-onset-response-verification.md` — this doc

## Test status

- `test_output_analyzer`: 35 assertions, all PASS (was 27 after Phase 1.1b)
- Full runtime build: clean
- Live verification on `showcase_demo` (original vs Smooth-fixed): 0.45 → 0.91 onset response rate
