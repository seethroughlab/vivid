# ADR-0048: Clip Editors Follow a Dedicated Ableton-Style Detail View

Status: accepted (implemented — #246–#249, #256–#260)

Date: 2026-08-02

## Context

The current `ClipEditor` has grown real power, but the surface does not communicate it.

The MIDI editor can draw/select notes, drag notes, resize note ends, edit velocity and MPE lanes, fold,
ghost, step-input, scale-highlight, loop braces, fit/follow, copy/paste, quantize, invert, retrograde,
humanize, strum, and glide. The audio editor can trim loop handles, show transients/slices/warp markers,
toggle warp modes, auto-warp, pitch, zoom, amp-zoom, and slice to MIDI. But much of that is exposed as bare
header text with invisible hit rectangles (`ghost`, `fold`, `step`, `Draw`, `auto`, `pit`, `float`, `X`) or
as a long footer instruction string. The controls do not look like controls, and draggable regions are often
only discoverable by accident.

Ableton Live's Clip View is a useful benchmark. Live separates clip panels/properties from the editor, uses
a dedicated Sample Editor for audio clips and MIDI Note Editor for MIDI clips, exposes warping and looping
as named clip controls, and makes direct manipulation objects (notes, velocities, warp markers, transients,
loop braces) visually distinct in the editor. The important lesson is not to copy Ableton's pixels; it is
to make the editing surface read as an intentional instrument, not a debug overlay.

## Decision

Rebuild Vivid's clip editing surface around a dedicated bottom detail view with three stable zones:

1. **Title and transport strip** - clip name, track/scene identity, active editor mode, follow/fit, close,
   dock/float, and compact transport/readout controls.
2. **Inspector strip** - explicit controls for clip properties and modes. For MIDI this includes grid,
   tool, fold, scale, ghost, step input, lane selector, quantize, and musical transforms. For audio this
   includes warp on/off, warp mode, auto-warp, pitch, loop/trim, slice mode, and slice-to-MIDI.
3. **Editor canvas** - the piano roll or waveform, with direct manipulation handles and overlays.

Text-only controls are no longer acceptable for the clip editor. Header actions must be drawn as actual UI
controls with bounded geometry shared by draw and hit-test: segmented controls, toggles, icon buttons,
menus, or sliders/steppers. A text label may name a control, but the click target must be visibly bounded
and have a hover/pressed/selected state.

The footer instruction crawl is removed. Persistent instructions at the bottom of the editor should not be
needed for normal operation. Short contextual status text may appear only for the thing currently hovered or
dragged, and keyboard shortcuts belong in the command palette / shortcut overlay, not as always-visible
editor chrome.

## MIDI Editor Requirements

- Notes must advertise their affordances: body drag for move, right-edge handle for resize, selected-state
  outline, hover highlight, and cursor/status feedback.
- The piano keyboard sidebar must be large enough to read and audition; pitch names should remain visible
  at usable zoom levels.
- The velocity / expression lane must behave like an editor lane, not a mystery footer band: visible lane
  selector, lane title, selected-note relationship, value readout while dragging, and a clear way to switch
  velocity/bend/pressure/timbre.
- Loop braces, grid, scale highlighting, ghost notes, fold, step input, and draw/select mode must be
  explicit mode controls with active states.
- Editing operations that are currently keyboard-only or footer-only (`quantize`, `invert`, `retrograde`,
  `humanize`, `strum`, `glide`) should be accessible through a compact tool/menu surface.

## Audio Editor Requirements

- The waveform editor must make loop/trim handles, warp markers, transient markers, and slice boundaries
  visually distinct.
- Hover states must say what will happen before the user clicks: drag trim, drag warp marker, create warp
  marker, delete marker, pan, zoom, adjust amplitude, etc.
- Warp mode, auto-warp, pitch, slice mode, and slice-to-MIDI must be explicit controls. `to MIDI`, `auto`,
  and `pit +N` as bare text are not enough.
- The editor should follow Ableton's principle that waveform content and clip properties are adjacent but
  not mixed together: property controls sit in the inspector strip; source/warp/loop geometry sits in the
  waveform.

## Implementation Plan

1. Extract clip editor control geometry into small structs/functions, following the existing shared
   draw/hit-test pattern used elsewhere in the UI.
2. Add reusable low-level controls for icon button, segmented control, toggle, compact menu, stepper, and
   hover status. Do not hand-place magic text ranges in `on_down`.
3. Replace the current header text controls in `ClipEditor::draw` and `ClipEditor::on_down` with those
   controls.
4. Replace the footer instruction string with contextual status/tooltips and a shortcuts/help overlay.
5. Add hover state to the editor so movable handles and editable regions visibly respond before click.
6. Add screenshot-based manual QA captures for both MIDI and audio clip editors at narrow and wide dock
   heights.

## Alternatives Considered

- **Only restyle the current text labels.** Rejected. The problem is not just color; draw and hit-test are
  disconnected from visible controls.
- **Copy Ableton exactly.** Rejected. Ableton is a benchmark for interaction clarity, not a design skin.
  Vivid still needs its own graph-first and performance-first editing model.
- **Hide advanced tools to simplify the surface.** Rejected. The editor already has useful advanced
  features. The goal is to organize and disclose them, not erase them.

## Consequences

- `ClipEditor` will need a small UI-control substrate before more features are added.
- Some keyboard-first power tools become menu/toolbar-accessible, which helps discoverability and agents.
- The editor becomes a real product surface rather than an implementation surface.
- This ADR pairs with ADR-0049: audio clips and Sampler editing should share a sample-editing language.

## References

- Ableton Live 12 Manual: Clip View - https://www.ableton.com/en/manual/clip-view/
- Ableton Live Manual: Audio Clips, Tempo, and Warping - https://www.ableton.com/en/live-manual/11/audio-clips-tempo-and-warping/
- Ableton Live Manual: Editing MIDI - https://www.ableton.com/en/live-manual/12/editing-midi/
- Code: `app/src/ui/clip_editor.cpp`, `app/src/ui/clip_editor.h`, `app/src/app/input_editor.cpp`
