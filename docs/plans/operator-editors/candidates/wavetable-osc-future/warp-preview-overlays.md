# Warp-mode preview overlays

## What it is

The operator exposes a `warp_mode` param with 8 modes (Sync, BendPlus, BendMinus, Mirror, Asym, Quantize, FM, Flip) plus a `warp_amount` 0..1 scalar. At `warp_amount > 0` the warp reshapes the playing waveform in mode-specific ways.

Today's editor preview shows only the **post-warp** waveform — whatever is actually being heard. That's correct, but it makes the warp effect invisible: you can't see *what the warp is doing* to the underlying shape.

The future feature adds **ghost overlays** on the preview that make the warp visually legible:

- **Pre-warp ghost polyline** drawn dimmed behind the post-warp shape. Makes the difference between raw wavetable and warped output obvious.
- **Mode-specific overlays**:
  - Sync → vertical line at the sync point (where the phase resets mid-cycle)
  - BendPlus / BendMinus → curve markers showing the bend function
  - Mirror → a mirror-axis indicator at the fold point
  - Asym → ratio indicator showing the asymmetry split
  - Quantize → tick marks at the quantization levels
  - FM → a small inset showing the modulator waveform
  - Flip → polarity indicators at the flip phase

## Why deferred from v1

Each warp mode is a mini visualization of its own. Doing one well takes 30 minutes; doing all 8 takes a focused afternoon. v1 prioritized the structural editor (three-region layout + live polyline preview) over mode-specific polish. Once users are actually editing with the v1 preview, the most-missed mode overlays will reveal themselves — we can ship the high-value ones first.

## Engine cost

**Zero** for the pre-warp ghost — we already have the pre-warp sampling path via `sample_level(phase, position, 0)`. Just sample twice: once for the post-warp (current preview logic) and once without the warp applied.

Actually — the operator's `sample_level` produces pre-warp output. The warp application lives in the audio processing path (`wavetable_osc_process.cpp`). So the current preview is showing **pre-warp** output, and the ghost-polyline addition would need to *apply* the warp to produce the post-warp shape.

That changes the framing: instead of "add a pre-warp ghost," it's "add a *post-warp* overlay" — the thing the user actually hears. More valuable, slightly more engine work. We'd need to extract the warp-application math into a shared helper callable from the editor.

## Editor cost

**~3 hours**:
- Extract warp application into `wavetable_osc_editor_shared::apply_warp(samples, warp_mode, warp_amount)` so the editor and compute() share one implementation.
- Add second polyline render on the preview canvas, alpha-blended, with slightly darker colour.
- Add mode-specific overlay dispatch: `draw_warp_overlay(ctx, mode, amount, preview_rect)` that selects from a small array of mode drawers.

Each mode drawer is ~20 lines — vertical line for Sync, curve for BendPlus, etc.

## Interactions

- **[frame-stack-visualization](frame-stack-visualization.md)** — post-warp frame-stack visualization is expensive (warp is per-sample). Either apply warp during the cache refresh (invalidate on warp param change) or skip warp on the stack view and show it only on the current-frame polyline. Probably the latter for performance.
- **[spectrum-view](spectrum-view.md)** — warp changes the spectral content. The FFT view naturally shows the effect, so maybe no separate overlay needed there.

## Scope cuts

- **Automated mode-signature diagrams** (a little cheatsheet showing what each warp mode does): nice but adds UI surface. Defer to docs.
- **Interactive warp parameters** (drag on the overlay to modify warp amount): the `warp_amount` slider in the side panel is enough for v1.5. Add direct-on-overlay interaction if users ask.

## Test plan

- Pure-logic: `apply_warp(samples, mode, amount)` against a golden reference for each mode. Catches regressions on the warp math.
- Editor: warp_amount slider change → post-warp overlay updates. Verify via captured `set_param("warp_amount", …)` + shape-helper assertion on the generated overlay.
