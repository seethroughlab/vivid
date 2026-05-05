# Euclidean Editor

## Context

`Euclidean` (`operators/control/euclidean/`) generates Euclidean rhythms — hits evenly distributed across N steps, with optional rotation offset. The operator's thumbnail already renders the pattern as a step grid (or a ring), but the inspector only has `hits`, `steps`, `rotation` scalar knobs. Users can't click a step on or off directly; they tweak `hits` until the pattern happens to include the step they want.

Smallest Tier-2 job. Interactive polygon / ring editor is a canonical Euclidean visualization that makes the math immediate.

## High-level approach

A ring (polygon with `steps` vertices) centered in the window. Active steps (where the Euclidean algorithm places hits) are filled; inactive are outlined. `rotation` rotates which step is at the 12 o'clock position. Drag a step to move `rotation`; click a step to increment/decrement `hits` (or, simpler, drag a `hits` slider on the side).

Alternatively (v1): render the pattern as a linear 32-step strip with the same algorithmic fill logic. The ring is prettier; the strip is more consistent with DrumSequencer and easier to implement with shared helpers.

**Recommendation:** ship the strip variant first (reuses grid helpers), add the ring as a toggle later if users ask for it.

## Editor layout (strip variant, v1)

```
┌──────────────────────────────────────────────────┐
│ top bar: hits · steps · rotation · rate · gate   │
├──────────────────────────────────────────────────┤
│                                                  │
│  ● ○ ○ ● ○ ○ ● ○ ○ ● ○ ○ ● ○ ○ ● ○ ○ ● ○ ○ ...  │
│   1   2   3   4   5   6   7   8   9 ...          │
│                                                  │
├──────────────────────────────────────────────────┤
│ side panel: live rhythm preview (scroll bar)     │
│             fill/rotate quick buttons            │
└──────────────────────────────────────────────────┘
```

Default window ~820×300; min ~600×220.

## Interactions

### Mouse
- Drag anywhere on the strip left/right → scrub `rotation`.
- Scroll (or pinch) → nudge `hits` up/down.
- Alt+scroll → nudge `steps`.

### Keyboard
- Arrow left/right: nudge `rotation`.
- Arrow up/down: nudge `hits`.
- Shift+up/down: nudge `steps`.
- `R`: reset rotation to 0.
- `D`: density cycle (common hit:step ratios — 3:8, 5:8, 3:16, 5:16, 7:16).

### Live feedback
- Current step highlighted while running.
- Pattern redraws instantly on any param change (Euclidean is deterministic from hits/steps/rotation).

## Data model recap

From `operators/control/euclidean/euclidean_core.h`:
- `hits`, `steps`, `rotation`
- Rate / clock config
- Gate/velocity outputs

## Implementation

### Files
- `operators/control/euclidean/euclidean.cpp` — `VIVID_EDITOR(Euclidean)`.
- `operators/control/euclidean/euclidean_core.h` — editor metadata + state.
- `operators/control/euclidean/euclidean_editor.cpp` — new.
- `operators/control/euclidean/euclidean_editor_shared.{h,cpp}` — new; Euclidean bit-array computation (shared with `compute()` and the thumbnail; find the existing implementation and promote it).
- `cmake/operators.cmake` — add new sources.

### State on the core
- `editor_hover_step_` — for showing a "clicking here would do X" hint.
- No selection model needed — the editor is continuous in `rotation`, discrete in `hits`/`steps`; no cells to select individually.

### Shared-helpers reuse
- The Euclidean algorithm — find the existing implementation (likely inline in `compute()` or in `operators/shared/sequencer/`) and make sure the editor shares it. Critical: thumbnail and editor must not drift.

### Tests
- `tests/operators/test_euclidean_editor_helpers.cpp` — algorithm round-trip matches known (hits, steps) → pattern reference values.

## Inspector retirement

Mirror DrumSequencer's phase-4 move: dedicated editor is the only interactive authoring surface; inspector becomes passive preview + "Open Editor" button.

- No `euclidean_inspector.cpp` exists today; no deletions needed. Retirement here is preventative — don't reach for a mini step-strip in the inspector once the editor exists.
- Euclidean has no dense per-cell params (just `hits`, `steps`, `rotation` and clock config) so no `VIVID_DISPLAY_HIDDEN` changes are needed. Everything stays visible in the default param list; the editor is just a graphical alternative.
- Keep the existing thumbnail (pattern ring/grid) as the passive preview. It already shares the Euclidean algorithm with `compute()`, which is exactly the shared-helper pattern this plan formalises.

## Deferred / out of scope

- Ring rendering (possible v2 toggle).
- Polyrhythmic stacking (multiple Euclidean patterns side by side).
- Density morphing animations.
- Per-step velocity (Euclidean is binary).

## Open questions

- Ring vs. strip: strip is easier and more consistent. Unless the team has a specific demo / pedagogical case for the ring, don't build it first.
- Should the editor show multiple rotations (e.g., how `rotation+1` vs `rotation` differ) as ghosted preview? Out of scope v1.
