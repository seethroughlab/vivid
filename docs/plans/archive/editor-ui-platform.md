# Proper Editor UI Platform Plan

## Summary

Build the editor-window API as a three-layer platform, with **interactive widgets/layout first**, then **editor host services**, then **richer vector/canvas primitives**. The current `VividDrawAPI` is already good enough for custom paint, but the real friction for editor authors is that they still have to hand-roll hit-testing, drag state, focus, and common controls in every operator. The next push should therefore make editor windows feel like a small, purpose-built UI toolkit rather than a bag of drawing callbacks.

The recommended outcome is:

- operator authors can build a solid editor from reusable widgets without rebuilding button/slider/grid behavior
- custom editors still have access to lower-level drawing for bespoke visuals
- editor windows gain the host services needed to feel like proper tools: clipboard, cursor control, drag capture, and popups/dialogs
- `DrumSequencer` and `MSEG` become the first two adopters and the proving ground for the new API

## Public API / Interface Changes

### 1. Add an interaction/widget layer on top of `VividDrawAPI`

Keep `VividDrawAPI` as the low-level paint surface, but introduce a new additive editor-side helper surface built around the existing `VividEditorContext`.

Recommended shape:
- add `src/operator_api/editor_ui.h` as the canonical widget/layout helper layer
- keep it header-only and operator-friendly like the existing draw helpers
- make it stateless at the API surface but able to use small operator-owned state structs for cursor/selection/expanded sections

Minimum first-wave widgets:
- `ui_button(...)`
- `ui_toggle(...)`
- `ui_tab_strip(...)`
- `ui_slider_h(...)`
- `ui_slider_v(...)`
- `ui_step_grid(...)`
- `ui_selectable_row(...)`
- `ui_icon_button(...)`
- `ui_scrollbar(...)`
- `ui_text_input_readonly(...)` for now, with editable text deferred until text-entry host support lands

Each widget should:
- draw itself through `ctx.draw`
- consume `ctx.mouse` / `ctx.events`
- return a simple result struct such as `pressed`, `changed`, `hovered`, `dragging`, `value`
- accept `VividInspectorTheme` so editors visually align with the rest of Vivid
- use explicit IDs or caller-provided stable keys where interaction state must persist

### 2. Add a small layout system for editor windows

Current editors do raw pixel math everywhere. Add an additive layout helper layer rather than a retained UI tree.

Recommended helpers:
- `UILayoutBox` / `UILayoutCursor`
- `ui_row(...)`
- `ui_column(...)`
- `ui_pad(...)`
- `ui_split(...)`
- `ui_stack(...)`
- `ui_scroll_region(...)`
- `ui_measure_text_block(...)`

Key rules:
- layout remains immediate-mode and deterministic inside `draw_editor(...)`
- layout helpers only compute rects and spacing; they do not own rendering
- widgets accept rects from the layout layer rather than embedding their own page layout policy

This gives editor authors a standard way to build inspectors, sidebars, transport rows, and sequencer panels without redoing geometry every time.

### 3. Expand `VividEditorContext` with host services

Add a dedicated editor host service surface rather than continuing to overload raw events and param commands.

Recommended additive context field:
- `VividEditorHostAPI host;`

First-wave `VividEditorHostAPI` functions:
- clipboard:
  - `set_clipboard_text`
  - `get_clipboard_text`
- cursor:
  - `set_cursor`
  - cursor kinds: arrow, ibeam, crosshair, hand, resize_h, resize_v, resize_diag
- pointer capture:
  - `capture_pointer`
  - `release_pointer`
  - `has_pointer_capture`
- focus:
  - `request_focus`
  - `has_focus`
- popup/menu:
  - `open_context_menu`
  - `close_context_menu`
- file/text utilities:
  - `open_file_dialog` for a later phase if desired, but leave it out of the first implementation unless a specific editor needs it immediately
- status/help:
  - `set_status_text`
  - `show_tooltip`

Host defaults:
- all functions are optional callbacks
- operators must behave safely when a capability is absent
- host-owned strings use the same lifetime rules as the existing draw/editor ABI surfaces

This is the main step that will make editor windows feel native instead of “raw canvas in a GLFW window.”

### 4. Add a second-wave low-level vector/canvas expansion

After widgets/host services, expand the paint API where current adopters still hit limits.

Recommended additive `VividDrawAPI` extensions:
- `draw_circle`
- `draw_ellipse`
- `draw_polyline`
- `draw_polygon`
- `draw_dashed_line`
- `draw_image`
- `measure_text_block`
- optional transform helpers:
  - `push_transform`
  - `pop_transform`
  - `translate`
  - `scale`
  - `rotate`

Do **not** start with a full retained path engine. The safer first step is to add the small set of primitives that rich editors actually need, then only add path/bezier building if `MSEG` or future editors prove it is necessary.

## Implementation Changes

### Widget/layout foundation

Implement the first reusable editor toolkit around the patterns already duplicated in `DrumSequencer` and `MSEG`.

Use the first wave to extract and standardize:
- button press + hover + active behavior
- slider drag semantics with pointer capture
- sequencer/grid cell hover/selection/drag behavior
- selection rows and simple tabs
- scrollable content regions
- text truncation/wrapping helpers for labels and captions

Adoption targets:
- convert `DrumSequencer` editor to the new step-grid, tab/button, and slider widgets
- convert `MSEG` editor to the new button/row/drag-handle/layout utilities while keeping its custom curve drawing

This ensures the API is driven by real editors instead of speculative abstractions.

### Host/runtime support

Update `editor_window_manager` to back the new host services:
- map cursor requests to GLFW cursors
- implement clipboard through GLFW
- add pointer-capture semantics per editor window
- track focused editor window and route focus state consistently
- support tooltip/status plumbing at the window level even if the first version is minimal

Keep command routing through `UICommandSink` / `RuntimeCommandSink`. Do not expose raw `RuntimeAPI` calls directly to operators.

### Documentation

Add a dedicated operator-authoring doc for editor UI:
- how to structure `draw_editor(...)`
- when to use widgets vs low-level drawing
- state patterns for selection, drag, and keyboard control
- host capability expectations and safe fallback behavior

Also update the runtime/editor architecture docs so the editor window contract stays current.

## Test Plan

### Automated

Add focused tests at three seams:

- widget helper tests:
  - button hover/press/release
  - slider drag and clamping
  - step-grid hit-testing and drag selection
  - layout rect generation and spacing
- host service tests:
  - pointer capture state transitions
  - cursor request routing
  - clipboard no-op safety when unavailable
- renderer/API tests:
  - new `VividDrawAPI` callbacks are populated by the host
  - any new primitive clamp/clip helpers behave correctly
- adopter tests:
  - `DrumSequencer` uses widget helpers for grid edits and command emission
  - `MSEG` uses the new shared interaction helpers where applicable

### Manual QA

Verify:
- `DrumSequencer` can be fully edited with the new grid/widgets
- `MSEG` drag behavior remains smooth and precise
- cursor shape changes correctly over resize/drag/text regions
- copy/paste flows work once clipboard support is wired
- editor windows remain correct across resize and mixed-DPI displays

## Assumptions And Defaults

- Priority is **widgets/layout first**, then **host services**, then **additional vector primitives**.
- The API stays additive; existing `draw_editor(...)` implementations keep working.
- Editor windows remain immediate-mode and operator-owned; this is not a retained UI framework.
- `VividDrawAPI` remains the paint layer, while `editor_ui.h` becomes the interaction/layout layer.
- Richer text editing, IME, and native file dialogs are follow-up items unless a near-term adopter requires them.
- `DrumSequencer` and `MSEG` are the reference editors for shaping this API, and new abstractions should only land if at least one of them uses them concretely.
