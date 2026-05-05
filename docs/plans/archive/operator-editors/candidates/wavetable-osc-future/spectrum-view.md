# Spectrum view (frequency-domain preview)

## What it is

Toggle on the preview canvas between:
- **Time domain** (v1 default): x = phase, y = amplitude, polyline of the waveform.
- **Frequency domain** (new): x = frequency bin, y = magnitude (optionally log-scaled), polyline of the FFT of the current wavetable slice.

Serum, Vital, and Phase Plant all have this. It's the canonical way to tune harmonic content.

## Why deferred from v1

Adding FFT infrastructure is non-trivial and orthogonal to the "make the editor exist" work in v1. Also, most users will spend more time in the time-domain view; spectrum is a specialist mode.

## Engine cost

**~2 hours**:
- A simple FFT helper (we don't have one in the toolkit yet — probably lean on a small standalone implementation like KISS FFT, or pull in the existing FFT from wherever the runtime's analysis hooks live).
- Pure function: `compute_spectrum(samples, out_magnitudes, size)` — input N time-domain samples, output N/2 + 1 magnitudes.
- For v1.5 of this feature: N=512 samples → 257 bins. Enough resolution to see harmonics; cheap to compute per-frame.

## Editor cost

**~1 hour**:
- Toggle button in the top bar: "Time / Freq" / keyboard shortcut (maybe `F`).
- Alternative render in the preview canvas:
  - Horizontal axis: frequency bins (optionally log-scale for better harmonic visibility at low frequencies).
  - Vertical axis: magnitude in dB (log) or linear.
  - Grid lines at octaves (2x, 3x, 5x harmonics labelled).
- Persist the toggle on the core as `editor_spectrum_mode_` (survives editor close).

## Interactions

- **[frame-stack-visualization](frame-stack-visualization.md)** — spectrum-mode frame-stack is the "spectral morphing view" from Serum: y = position, x = frequency, intensity = magnitude. Same rendering structure as time-domain frame-stack but using magnitudes.
- **[warp-preview-overlays](warp-preview-overlays.md)** — warp changes spectral content dramatically. Seeing before/after spectrum is useful.

## Scope cuts

- **Adjustable FFT size**: keep it fixed at 512 for v1.5.
- **Windowing choice** (Hann, Hamming, Blackman-Harris): fix at Hann.
- **Phase display** alongside magnitude: niche; defer.
- **Harmonic analysis labels** ("C4 fundamental", "3rd harmonic"): nice polish but not essential.
- **Spectrum of the *output* audio** (post-warp, post-unison): probably belongs in [live-monitoring](live-monitoring.md) as an output-scope alternative.

## Test plan

- Pure-logic: `compute_spectrum(sine_512hz, out, 512)` → assert peak at bin corresponding to 512Hz (given a known sample rate). Test a few canonical waveforms — sine, square, triangle — against expected harmonic structure.
- End-to-end: toggle the spectrum mode → `editor_spectrum_mode_` flips; canvas renders the alt view without crash.

## Architectural note

If we add FFT to the editor, consider whether it should live in `operators/shared/editor_ui/fft.h` rather than wavetable_osc-specific. Any future spectral-editor operator (EQ analyzer, granular source-spectrum view, etc.) would benefit from sharing it. Don't prematurely extract — extract on the second adopter.
