# Phase 4: First Adopter — DrumSequencer (and MSEG as follow-up)

## Goal

Migrate DrumSequencer to use `VIVID_EDITOR` as its real authoring surface, proving the whole feature end-to-end. The inspector version continues to exist as a compact preview / quick-tweak surface; the editor window is where serious pattern authoring happens. Once DrumSequencer is proven, apply the same pattern to MSEG.

## Context

DrumSequencer today draws a 16 × 6 grid of trigger cells with tab-switched Mod A / Mod B views into the inspector panel (~350 px wide). See `operators/control/drum_sequencer/drum_sequencer_inspector.cpp` (~186 lines). It works, but cells are cramped, modulation lanes hide behind tabs, and there is no room for velocity/probability or larger pattern lengths.

MSEG is an even stronger candidate (53 hidden params, draggable envelope points), but we pick DrumSequencer first because its grid UI is simpler to lift into a larger canvas and less likely to surface edge-case input/interaction bugs.

## Scope

- Add `VIVID_EDITOR(DrumSequencer)` and implement `draw_editor`.
- Keep `draw_inspector` working — it becomes a compact overview / preview.
- Share drawing code between the two via a helper that takes a canvas rect and grid dimensions; inspector calls it with small cells, editor calls it with large cells.
- Add editor-only affordances that were not possible in the inspector: keyboard shortcuts, larger mod-value handles, visible Mod A + Mod B simultaneously, copy/paste stepping (optional v1).
- Sanity-check hot reload, param sync, and undo all still work from inside the editor.

After DrumSequencer ships and is stable, a follow-up PR migrates **MSEG** (`operators/control/mseg/mseg.cpp` lines 212–400) to the same pattern.

## Design

### DrumSequencer editor metadata

```cpp
static VividEditorMetadata editor_metadata() {
    return {
        .default_width  = 900,
        .default_height = 520,
        .min_width      = 640,
        .min_height     = 360,
        .title_suffix   = "Drum Pattern Editor",
    };
}
```

### Shared grid renderer

Extract the current inspector grid draw into a helper that both contexts call:

```cpp
struct DrumGridLayout {
    Rect   cells_area;      // where the 16 × 6 grid lives
    float  cell_w, cell_h;
    float  beat_gutter;
    // ...
};

void draw_drum_grid(const DrumSequencerState& state,
                    const VividDrawAPI& draw,
                    const VividInspectorTheme& theme,
                    const DrumGridLayout& layout,
                    DrumGridInputMode mode,   // trigger-only, mod-a, mod-b, or full (editor)
                    DrumGridInteraction& io); // in/out: mouse, clicks, etc.
```

- Inspector: `mode = trigger_only` with a tab strip for Mod A / Mod B (existing behavior preserved).
- Editor: `mode = full`, all three lanes visible simultaneously (trigger row + Mod A strip below it + Mod B strip below that), cells 2–3× larger, headers for each drum, visible beat labels.

### Editor-only affordances

- **Space** to clear/reset the step under the playhead (or the last-clicked cell).
- **Shift-click** to toggle a whole row / column.
- **Arrow keys** to move a cursor cell and **Enter** to toggle.
- **Cmd+C / Cmd+V** on selected step (v1 nice-to-have — skip if it pushes scope).
- Current-step highlight extended to full column with a subtle pulse.
- Pattern-length handle draggable at the right edge (replaces the current `num_steps` slider inside the editor; the inspector still shows the slider).

These all use the new `VividEditorContext::events` queue plus `ctx.commands.set_param` — no engine changes needed.

### Wiring in the operator

In `operators/control/drum_sequencer/drum_sequencer.cpp`:

```cpp
// already exists
VIVID_INSPECTOR(DrumSequencer)
// new
VIVID_EDITOR(DrumSequencer)
```

Class gains:

```cpp
static VividEditorMetadata editor_metadata();
void draw_editor(VividEditorContext* ctx);
```

`draw_editor` reuses `draw_drum_grid` with a full-size layout derived from `ctx->surface_width/height`. Input handling consumes events from `ctx->events`.

## Files

| Change | Path |
|---|---|
| Extract shared grid renderer | `operators/control/drum_sequencer/drum_sequencer_inspector.cpp` → split into `drum_sequencer_grid.{h,cpp}` |
| Implement `draw_editor`, add macro, metadata | `operators/control/drum_sequencer/drum_sequencer.cpp`, `drum_sequencer.h` |
| Build list update | `operators/control/drum_sequencer/CMakeLists.txt` |
| Follow-up (separate PR) | `operators/control/mseg/mseg.cpp` |

## Acceptance Criteria

1. Loading a DrumSequencer node shows the existing inspector grid **and** an "Open Editor" button.
2. Opening the editor shows a clearly larger grid with all three lanes (trigger + Mod A + Mod B) visible simultaneously.
3. Editing in the editor updates the inspector instantly; editing in the inspector updates the editor instantly.
4. Pattern-length drag and cell toggles in the editor are persisted to the graph and survive save/load.
5. Keyboard shortcuts (Space to clear, arrow keys to move cursor, Enter to toggle) work inside the editor window and do not leak to the main graph editor.
6. Closing the editor leaves the inspector intact and the node state correct.
7. Rebuilding the DrumSequencer package while the editor is open closes the window cleanly; reopening after rebuild reflects the rebuilt code.
8. Undo/redo works across edits made in the editor (relies on the existing `set_param` path, which already integrates with undo).
9. A scripted capture test (similar to existing `test_demo_graphs` patterns) exercises opening the editor, making an edit via programmatic input injection (or a smoke variant), and verifies the param value changed.
10. `ctest` passes in background.

## Dependencies

- **Phases 1, 2, 3** must all be merged. This phase consumes the full stack.

## Out of Scope for This Phase

- MSEG migration — schedule as a follow-up PR with its own short plan doc once DrumSequencer is stable.
- Tracker, Sampler, Arpeggiator migrations — evaluate one at a time after MSEG; each is a small, isolated change now that the ABI exists.
- Preset browser inside the editor window, pattern-bank management, live-output thumbnail inside the editor — additive features for later.
