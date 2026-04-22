# Frame-stack visualization

## What it is

The v1 preview shows a single waveform — the slice at the current `position` value. A frame-stack visualization shows the **entire wavetable** by rendering every frame (up to 256) as a horizontal band in a 2D map, with the current position drawn as a highlighted horizontal line.

Imagine:
- Horizontal axis: phase within one cycle (0..1)
- Vertical axis: position through the wavetable (0..1, bottom = first frame, top = last frame)
- Pixel intensity: sample amplitude at that (phase, position) pair

This is the canonical "wavetable morphing" visualization — every modern wavetable synth has it (Serum, Vital, Massive X, Phase Plant). Seeing it makes the position param's behavior immediate: a user can trace exactly how the waveform morphs from frame 0 to frame N.

## Why deferred from v1

v1 prioritized the single-frame polyline because it's faster to render, easier to read when the wavetable is simple, and pairs naturally with the drag-to-scrub-position gesture. The frame stack is a *second* visualization, not a replacement — ideally v1.5 ships with a toggle between the two (or both, side by side).

## Engine cost

**Zero**. All the data is already in `Wavetable::data` (or `resolve_table()->level_data(0)` for the highest-fidelity frames). Sampling is just:
```cpp
for (uint32_t frame = 0; frame < table.frame_count; ++frame) {
    for (int x = 0; x < preview_width; ++x) {
        float phase = static_cast<float>(x) / preview_width;
        float amp = table.sample_level(phase, frame_to_position(frame), 0);
        plot(x, frame_to_y(frame), amp);
    }
}
```

For a 256-frame table × 256-pixel-wide preview, that's ~65K samples per frame — fine at 60fps if we cache per-(family, member) and only re-render on change.

## Editor cost

**~3 hours**. Mostly rendering:
- New helper: `sample_wavetable_grid(table, width, height, out_buffer)` that returns amplitude samples on a grid. Pure function, test-friendly.
- Render: either (a) draw the grid as N×M small rects coloured by amplitude (simple, works with the current draw API), or (b) blit a texture we computed on the CPU. Start with (a) — the draw_rect API handles it, and the compute is cheap enough.
- Overlay: a horizontal line at the current position value, plus the current-position polyline superimposed on top of the stack for extra clarity.
- Interaction: drag vertically on the frame stack → scrub position. Horizontal axis is purely informational.

## Layout

Two options for v1.5:

**Option A — replace the single-frame preview.** Simpler; fits in the current preview region. Polyline overlay shows the current frame on top of the stack map.

**Option B — dual view.** Preview region is split: top half is frame stack (2D map), bottom half is the current-position polyline. More information, takes more screen real estate.

I'd ship A first with a planned "show polyline only" toggle (Tab key) that hides the stack. If users ask for B, it's additive.

## Interactions with other deferred items

- **[audition](audition.md)** — playing a note while watching the frame stack highlight the current position in real time is the dream UX. Depends on the audition button being wired first, but the frame stack is independently useful.
- **[spectrum-view](spectrum-view.md)** — a frequency-domain frame stack (vertical axis = position, horizontal = frequency bins, intensity = magnitude) is the Serum-style "spectral view" mode. Same rendering infrastructure; second visualization.
- **[warp-preview-overlays](warp-preview-overlays.md)** — when warp is active, the frame stack could show post-warp amplitudes. More expensive to compute (warp applies per-sample) but same shape.

## Scope cuts

- **GPU rendering**: the thumbnail path uses WGSL to render the wavetable as a GPU texture. Tempting to reuse that infrastructure for the editor, but it couples the editor to a render surface we don't yet have in `VividEditorContext`. CPU rasterization is fine for v1.5.
- **Smooth colour gradients**: draw_rect primitives give blocky output at coarse grid sizes. Accept the blockiness for v1.5; fix with polyline interpolation or a texture-blit API later.

## Test plan

- Pure-logic test: `sample_wavetable_grid(table, 64, 64, buf)` against a known wavetable, assert specific (phase, position) → amplitude values.
- Visual regression: render the stack for the 6 builtin families at a canonical position and golden-image diff. Overkill for v1.5; defer.
- End-to-end: drag vertically on the frame stack → captured `set_param("position", …)` with the expected value.
