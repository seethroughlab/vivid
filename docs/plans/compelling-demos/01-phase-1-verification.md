# Phase 1 — Perception Loop Verification

**Date:** 2026-04-19
**Scope:** Multi-axis AV reactivity correlation + audio settling guidance.

## What Shipped

### 1.1b — Multi-axis AV reactivity correlation

The single biggest Phase 0 finding was that `analyze_output(mode="av")` only correlated audio energy against frame brightness — so any reactivity that drives motion, displacement, position, contrast, or hue was structurally invisible to the metric. The Phase 0 case study `audio_reactive_demo` (a graph that physically distorts a hexagon via Displace) scored −0.110 on AV correlation, suggesting (incorrectly) that it was not audio-reactive.

**Implementation:**
- `src/runtime/debug/output_analyzer.{h,cpp}` — replaced the 2-frame `analyze_av_reactivity` signature with a multi-frame version that takes a `std::vector<VisualSample>` time series. Computes Pearson correlation per axis (brightness, motion, contrast) against per-chunk audio RMS. Uses linear interpolation to align the two time series.
- `src/runtime/debug/capture_coordinator.{h,cpp}` — `tick_analysis` now captures intermediate frames at ~6 fps during AV analysis windows. Each frame is reduced to a `VisualSample` (brightness, contrast, inter-sample motion). Sampling cadence: every 160 ms; a 1 s window yields ≥6 samples, a 3 s window yields ≥18.
- `tests/integration/test_output_analyzer.cpp` — added cases covering multi-axis correlation (brightness ramp, motion-only ramp), and degenerate empty input. All assertions pass.
- `serialize_analysis` now emits `energy_motion_correlation`, `energy_contrast_correlation`, and `visual_samples` alongside the existing `energy_brightness_correlation`.
- `compare_analyses` now produces directional deltas for all three correlation axes.

**API change** (intentional, no shim — per project convention "don't add backwards-compatibility shims when you can just change the code"):
```cpp
// Before
AVReactivityMetrics analyze_av_reactivity(
    const float* audio, uint64_t count, uint32_t rate, uint16_t channels,
    const uint8_t* frame_a, const uint8_t* frame_b, uint32_t w, uint32_t h,
    float window_seconds);

// After
AVReactivityMetrics analyze_av_reactivity(
    const float* audio, uint64_t count, uint32_t rate, uint16_t channels,
    const std::vector<VisualSample>& visual,
    float window_seconds);
```

### 1.1a — Audio settling-delay guidance

Phase 0 found that after `load_graph`, audio analysis returns RMS=0 for ~3-4 seconds because the audio engine takes that long to begin filling the recording tap. This is not a broken handler — just an undocumented latency.

**Implementation:**
- `mcp/vivid_mcp.py` — extended `analyze_output` docstring with explicit guidance: wait at least 4s after `load_graph`, or re-call once if RMS=0.
- `mcp/vivid_mcp.py` — added new MCP tool `wait_for_audio_settle(timeout_seconds, window_seconds, rms_threshold)` that polls until audio is non-silent or timeout. Lets Claude programmatically gate analysis on a settled audio engine.

## Verification Against Phase 0 Case Studies

Re-ran the two case studies with the new metrics. All values are after a 5-second settling delay following `load_graph`.

### Case A — `showcase_demo` (the hero, Phase 0 score 1/1/1)

| Metric | Phase 0 (before) | Phase 1 (after) | Interpretation |
|---|---|---|---|
| `energy_brightness_correlation` | −0.009 | +0.147 | Slight positive (drum hits brighten briefly) |
| `energy_motion_correlation` | (not measured) | −0.189 | Slight negative |
| `energy_contrast_correlation` | (not measured) | +0.167 | Slight positive |
| `visual_samples` | 2 (interpolated) | 19 | Real time series |
| `mean_brightness` | 0.0004 | 0.004 | Still near-black |

**Diagnosis preserved.** All three correlations are weak (|r| < 0.2). This is correct: `showcase_demo`'s drum-trigger pattern produces sub-frame flashes that don't sustain, so the time-series visual signal is mostly baseline (1% scale) with rare brief peaks. The graph is genuinely not reactive on any visible axis. **Phase 2's `EnvelopeFollower` is the correct fix**, as predicted by the Phase 0 findings doc.

### Case B — `audio_reactive_demo` (the diagnostic, Phase 0 score 3/2/2)

| Metric | Phase 0 (before) | Phase 1 (after) | Interpretation |
|---|---|---|---|
| `energy_brightness_correlation` | −0.110 | −0.252 | Brightness is anti-correlated (audio peaks during dark distortion) |
| `energy_motion_correlation` | (not measured) | **+0.904** | **Strong positive** — displacement IS reactive |
| `energy_contrast_correlation` | (not measured) | −0.248 | Slight anti-correlation |
| `visual_samples` | 2 (interpolated) | 20 | Real time series |
| `motion_magnitude` | 0.064 | 0.041 | Stable |

**Phase 0 prediction confirmed.** The graph is highly audio-reactive — but only via motion (displacement-driven), not brightness. The Phase 0 brightness-only metric reported −0.110, suggesting the graph was broken; the Phase 1 multi-axis metric correctly reports motion correlation +0.904, surfacing the actual reactivity that was always there.

**This is the headline win of Phase 1.** Without this fix, Claude would conclude "audio_reactive_demo is broken" from the metric and try to fix it. With this fix, Claude sees "motion is highly reactive, brightness isn't — if I want a brightness response, I need to add a hue/luminance modulation alongside the displacement." That diagnosis enables a real intervention rather than a wild goose chase.

### Pure-visual sanity check — `lanes_intro_demo`

A graph with no audio domain. Expected: silent audio, all correlations 0, sensible degenerate metrics.

| Field | Value | Interpretation |
|---|---|---|
| `audio.rms` | 0.0 | Correct (no audio) |
| `mean_brightness` | 0.182 | Healthy visual |
| `motion_magnitude` | 0.148 | Healthy motion |
| `energy_*_correlation` (all three) | 0.0 | Correct (no audio to correlate against) |
| `visual_samples` | 0 | Cosmetic quirk (see below) |

**Cosmetic quirk.** For graphs with no `audio_out` node, the runtime's audio-tap-started flag stays false, which gates the call site for `analyze_av_reactivity`. The metric block reports all zeros (correct outcome) but `visual_samples` and `window_seconds` also read 0 instead of reflecting the captured-and-discarded samples. Functionally fine — the right call is `mode="frame"` for pure-visual graphs anyway — but worth tightening in a follow-up so the field is informational regardless of audio presence.

## Roadmap Adjustments Confirmed

The Phase 0 findings doc proposed three roadmap adjustments. Phase 1 results validate them:

1. ✅ **Phase 1.1a is smaller than thought** — settling delay was a documentation + helper-tool problem, not a broken handler. ~30 lines of Python, no C++ change needed.
2. ✅ **Multi-axis correlation BEFORE per-band correlation** — the brightness-only metric was actively misjudging good work. Fixing this first makes per-band correlation an additive enhancement rather than a bug fix.
3. ✅ **`EnvelopeFollower` is the highest-leverage Phase 2 operator** — the showcase_demo verification confirms the graph itself needs an envelope follower; the metric correctly reports near-zero reactivity, so we trust the metric to verify Phase 2's effect.

## Unblocked / Remaining

### Unblocked by Phase 1
- **Phase 2 verification harness.** The new metrics let us measurably verify whether a candidate `EnvelopeFollower` improves `showcase_demo`'s reactivity. Target: motion or brightness correlation > 0.5 after Phase 2.
- **Per-band correlation (Task 8).** Now that the time-series infrastructure exists, splitting audio into bass/mid/treble bands (per-chunk FFT) and correlating each band against each visual axis is purely additive — no architectural change needed.
- **Onset response rate (Task 9).** Same — adds an onset-detection pass over the audio time series, then measures visual delta in the N ms after each onset. Builds on existing per-chunk audio RMS series.

### Deferred (still Phase 1, not yet done)
- Per-band correlation (`energy_bass_*`, `energy_mid_*`, `energy_treble_*`) — task 8
- Onset response rate + reactivity latency — task 9
- LLM-INTEGRATION.md docs update — promote §9.2/§9.4 from "aspirational" to "documented as of Phase 1"

These are coherent next slices but bigger than the multi-axis change. Worth user check-in on whether to push through immediately or pause to ship Phase 2's `EnvelopeFollower` first.

## Files Touched

- `src/runtime/debug/output_analyzer.h`
- `src/runtime/debug/output_analyzer.cpp`
- `src/runtime/debug/capture_coordinator.h`
- `src/runtime/debug/capture_coordinator.cpp`
- `tests/integration/test_output_analyzer.cpp`
- `mcp/vivid_mcp.py`

## Test Status

- `test_output_analyzer`: 27 assertions, all PASS (was 23 before Phase 1; added 4 for multi-axis)
- Full runtime build: clean (only pre-existing warnings, no new warnings)
- Live runtime verification: `audio_reactive_demo` motion correlation goes from "structurally invisible" to +0.904, confirming the fix end-to-end
