# WavetableOsc Editor (vivid-wavetable)

## Context

`WavetableOsc` lives in the sibling `vivid-wavetable` repo (`/Users/jeff/Developer/vivid-wavetable/src/wavetable_osc.cpp`). It's the single largest inspector offender across the Vivid ecosystem — ~27 params across six or seven conceptual clusters (wavetable selection, position + modulation smoothing, warp mode + amount, phase behaviour, unison voicing, detune/drift, interaction modes). The inspector scrolls them as a flat list. Selecting a wavetable family/member with no preview is particularly painful.

The editor is a non-trivial engineering job because it needs live visualization (current waveform), live playback (one-voice audition), and browsing (tree of families/members). It's also the one where the editor-window concept delivers the biggest UX uplift.

## High-level approach

Four-region window:
1. **Wavetable browser** (left) — scrollable list of families; each family expands to its member waveforms. Member rows show a tiny waveform glyph. Click → load.
2. **Waveform preview** (top right, ~60%) — the currently-selected wavetable rendered as a 2D surface: x = position within wavetable, y = sample amplitude. A vertical cursor shows the live `position` param value. Warp/phase states are reflected.
3. **Unison scatter** (bottom right, ~40%) — a small 2D visualization of the unison voices: x = detune (cents), y = pan. Drag to edit `unison_detune` / `unison_spread`; center slider for `unison_voices`.
4. **Side panel** (far right, ~220px) — numeric readouts and dropdowns for warp mode, phase mode, portamento, interaction mode — the discrete params that don't belong on a surface.

## Editor layout

```
┌────────────────────────────────────────────────────────────────────────┐
│ top bar: audition note · gate · A/B · warp mode · phase mode           │
├──────────────┬──────────────────────────────────────────┬──────────────┤
│              │                                          │              │
│              │          waveform preview (2D)           │              │
│  wavetable   │          (position + warp applied)       │  side panel  │
│  browser     │                                          │  (discrete   │
│  (families / │                                          │   params)    │
│   members)   │                                          │              │
│              ├──────────────────────────────────────────┤              │
│              │  unison scatter (detune × pan)           │              │
│              │                                          │              │
└──────────────┴──────────────────────────────────────────┴──────────────┘
```

Default window ~1200×700; min ~900×520.

## Interactions

### Wavetable browser
- Click family row → expand/collapse.
- Click member row → set `wavetable_source` / `family` / `member` params.
- Arrow keys with browser focus → move through list; Enter selects.

### Waveform preview
- Hover → ghost cursor showing `position` at mouse x.
- Drag → set `position` param (clamped 0..1).
- Shift+drag → scrub through `warp_amount`.
- Ctrl+click → reset position to 0.

### Unison scatter
- Drag any voice dot → adjust `unison_detune` (x → cents) and `unison_spread` (y → pan). Single drag affects all voices symmetrically (the visualization is a preview, not per-voice authoring).
- Scroll → `unison_voices` (1..8 or whatever the max is).
- Double-click → reset unison to mono.

### Audition
- Top-bar "audition" button plays a short note at a reference pitch using the host's audio-settle mechanism; read the host API before committing.
- If live audition isn't trivially supported via the editor context, defer and ship v1 without. The visual payoff alone is worth the editor even without audition.

### Keyboard
- `Space`: audition on/off (if supported).
- Arrow keys in the browser context: navigate.
- `W`: cycle warp mode.
- `P`: cycle phase mode.

## Data model recap

From `vivid-wavetable/src/wavetable_osc_internal.h` (confirm at implementation time; names approximate):
- Wavetable source / family / member selectors
- `position`, `position_smoothing`
- `warp_mode`, `warp_amount`, `warp_smoothing`
- `amplitude`
- `phase_mode`, `phase_reset`, `phase_drift`
- `unison_voices`, `unison_detune`, `unison_spread`, `unison_stereo`, `unison_phase_spread` (~5 params)
- `portamento`, `interaction_mode` (~3 params)

## Implementation

### Files (in vivid-wavetable repo)
- `src/wavetable_osc.cpp` — add `VIVID_EDITOR(WavetableOsc)`.
- `src/wavetable_osc_internal.h` — add `editor_metadata()` / `draw_editor()` declarations, editor state, browser expansion state.
- `src/wavetable_osc_editor.cpp` — new; four-region painting and input.
- `src/wavetable_osc_editor_shared.{h,cpp}` — new; pure-logic helpers: wavetable browser tree model, waveform sampling to pixels, unison layout math.
- `CMakeLists.txt` — add new sources to the `wavetable_osc` target.

### State on the core
- `editor_browser_expanded_[N]` — per-family expansion bits.
- `editor_browser_cursor_` — selected row for keyboard nav.
- `editor_position_drag_` — drag state for the waveform preview.
- `editor_unison_drag_` — drag state for the unison scatter.

### Shared-helpers reuse
- None from the core seed operators — the idiom (browser + preview + scatter) is new.
- If vivid-wavetable grows a second operator with a browser (e.g., a sample library operator), promote the browser helpers locally inside the package first.

### Tests
- `tests/test_wavetable_osc_editor_helpers.cpp` in the package — browser tree construction, waveform-to-pixel sampling, unison layout math.
- Live audition is hard to test in the package harness; skip for editor-shared tests, rely on existing operator tests for audio correctness.

## Inspector retirement

Mirror DrumSequencer's phase-4 move: dedicated editor is the only interactive authoring surface; inspector becomes passive preview + "Open Editor" button.

- If `vivid-wavetable` has a custom inspector paint file today, delete it and drop it from the package's `CMakeLists.txt`. The inspector's 27-knob list is exactly the UX the editor replaces.
- Mark the warp / phase / unison / drift / portamento params as `VIVID_DISPLAY_HIDDEN` so the default param list shows only the top-level selectors: wavetable source, family, member, position, amplitude. Anything that belongs to the clustered editor surface shouldn't surface as a floating knob.
- The existing `wavetable_osc_thumbnail.cpp` already renders a waveform preview — that's the passive inspector preview. Keep it and make sure it reads the same sample data the editor's preview pane will use (share a helper in `wavetable_osc_editor_shared.cpp`).

## Deferred / out of scope

- Per-voice unison authoring (today's params are symmetric; full per-voice would require more params).
- Wavetable import (drag a WAV into the editor). Belongs to a sampler workflow.
- MIDI learn / MPE controls.
- Morph-transition recording.
- Warp-mode-specific visualizations (each mode showing its own stylized curve overlay). v1 just shows the raw warped output.

## Open questions

- Does the vivid-wavetable operator expose wavetable sample data through the editor context, or does the editor re-synthesize a preview by calling a shared helper? If the latter, lift the preview synthesis into `wavetable_osc_editor_shared` so the preview can't drift from playback.
- Audition: can the editor trigger a note through a sidecar voice without disturbing the main polyphony, or is audition best handled by flipping `gate` / `note` params on the live operator? The latter is simpler; it interferes with a running graph but is acceptable for v1.
- Browser population: is there a manifest of wavetables somewhere, or does the operator enumerate them at load time? Read the existing code and mirror whatever mechanism it uses; don't invent a parallel discovery path.
