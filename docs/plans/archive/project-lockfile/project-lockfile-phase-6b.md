# Project Lockfile — Phase 6b Execution Plan

Scope of this doc: Phase 6b only ("UI Indicator + Findings Modal") from [project-lockfile-reproducibility-plan.md](./project-lockfile-reproducibility-plan.md). Builds on Phase 6a ([project-lockfile-phase-6a.md](./project-lockfile-phase-6a.md)). Phase 6 was split into 6a (backend) and 6b (UI) to keep PRs reviewable.

## Context

Phase 6a gave the lockfile teeth — every graph load verifies, Strict mode disables affected nodes, and `GraphSnapshot.lockfile_status` carries the result. But without any UI surface, a GUI user has no indication that something's wrong beyond a node's existing `MISSING` rendering.

Phase 6b closes that gap:

- Small colored badge in the top-right perf bar indicating overall status.
- Clicking the badge opens a read-only findings modal listing each discrepancy.
- No per-node visual change — Phase 6a already populates the `MISSING` detail from the finding message, which flows through the existing tooltip pipeline.

After this phase, a user opening a graph with a stale lockfile sees the problem immediately; no CLI or snapshot dump required.

## Scope decisions (per user answers)

- **Badge placement:** top-right perf bar, inserted between the "Unsaved" badge and the recording/transport buttons. Matches the pattern used by `diagnostics_button_rect_` and friends.
- **No-lockfile state:** badge hidden entirely. `overall ∈ {Match, NoLockfile}` or `findings.empty()` → nothing drawn. Only `CompatibleDrift` (yellow) and `Mismatch` (red) render a badge.
- **Modal contents:** flat, read-only. Overall-status pill + scrollable list of findings (severity chip + id + subject + message + "-> suggestion"). No action buttons; those are deferred.
- **Node decoration:** reuse the existing `MISSING` pipeline. Phase 6a already writes the finding message into `missing_operator_detail`, which the existing error-tooltip draws on hover.

## Key facts from exploration

- Perf bar renders at the top via `draw_overlays` in `src/ui/graph/node_graph_draw_overlays.cpp`. `kPerfBarH = 28px`, `kPerfBtnH` for inner button height. A running `left_x` accumulates horizontally.
- Existing badges/buttons stash a `TransportValueRect` (`node_graph.h:892`) — `{x, y, w, h, visible}` — for post-draw click hit-testing. `OverlayRect` in `overlay_layouts.h` is a separate type and does NOT have a `visible` field; don't mix them.
- DialogManager state structs follow a simple pattern: `bool open = false;` + any draw-time data. Open/close methods just flip the flag and reset per-dialog state (scroll, buffers). `any_open()` and `wants_keyboard()` aggregate status.
- No built-in scrollable list widget; draw manually with `tr.push_clip_rect` / `pop_clip_rect` and a scroll offset.

## Deliverable

- `DialogManager::LockfileFindingsState` + `open_lockfile_findings(status)` / `close_lockfile_findings()` / `lockfile_findings_open()`.
- `draw_lockfile_findings(...)` in `dialog_manager_draw.cpp` — scrim, centered panel, overall-status pill, scrollable findings list, close button.
- `update_lockfile_findings_local(...)` in `dialog_manager_input.cpp` — close-button hit-test + outside-click dismiss. Runs inside `DialogManager::update`.
- `on_scroll` feeds the wheel into `scroll_y` (clamped non-negative at the edges).
- `TransportValueRect lockfile_badge_rect_` on `NodeGraphUI`.
- Badge draw in perf bar, only when `overall ∈ {CompatibleDrift, Mismatch}` and findings are non-empty.
- Click hit-test in `node_graph_input_click.cpp` opens the modal.
- Structural tests for the modal's state transitions + any_open/wants_keyboard inclusion.
- Three commits on the existing `worktree-project-lockfile` branch.

## Commit layout

1. **`Add lockfile findings modal to DialogManager`** — state struct + open/close helpers + draw + input handler + scroll. Seven structural tests (no GPU needed).
2. **`Add lockfile status badge to graph view`** — perf-bar badge + `TransportValueRect lockfile_badge_rect_` member + click routing.
3. **`Add Phase 6b execution plan doc for project lockfile`** — this doc.

## Files

### Modified
- `src/ui/dialogs/dialog_manager.h` — `LockfileFindingsState` struct + open/close/query methods + draw-method declaration + `#include "runtime/packages/project_lockfile.h"`.
- `src/ui/dialogs/dialog_manager.cpp` — state initialization; `any_open()` includes the modal; `wants_keyboard()` deliberately excludes it.
- `src/ui/dialogs/dialog_manager_draw.cpp` — `draw_lockfile_findings(...)` + dispatch entry in `draw()`.
- `src/ui/dialogs/dialog_manager_input.cpp` — `update_lockfile_findings_local` file-local helper + `on_scroll` branch.
- `src/ui/graph/node_graph.h` — `TransportValueRect lockfile_badge_rect_` member.
- `src/ui/graph/node_graph_draw_overlays.cpp` — badge draw right after the "Unsaved" badge block. Resets `lockfile_badge_rect_ = {}` at the top; populates `{x, y, w, h, true}` when drawn.
- `src/ui/graph/node_graph_input_click.cpp` — badge hit-test + click routing to `dialogs_.open_lockfile_findings(snap_.lockfile_status)`.
- `cmake/tests/20-ui-and-common.cmake` — register the new test target.

### New
- `tests/ui/test_dialog_manager_lockfile.cpp` — seven structural tests.

## Modal design

### State

```cpp
struct LockfileFindingsState {
    bool open = false;
    vivid::LockfileStatus status;  // copy taken at open time
    float scroll_y = 0.0f;
    OverlayRect close_btn{};       // populated during draw; w > 0 == drawn
};
```

`open_lockfile_findings(status)` copies the status, resets scroll, clears `close_btn`, and flips open. `close_lockfile_findings()` flips it off and resets scroll. The copy is deliberate — the modal stays stable while the underlying `RuntimeCore::lockfile_status()` churns frame-to-frame (e.g. on a re-load).

### Draw

- Scrim full-screen.
- Centered panel: 560px wide; height = `52 (header) + list_h + 48 (close button)` where `list_h = min(max_list_h, max(findings.size() * 72, 72))`.
- Header: "Lockfile verification" + overall pill aligned right.
- Body: scrollable findings list inside `push_clip_rect`. Each row (≈72px):
  - Colored severity chip (INFO blue / WARN yellow / CRIT red) + `id`.
  - `subject` in dim text to the right of id.
  - `message` on a new line.
  - `-> suggestion` in dim text on the next line (when non-empty).
- Close button bottom-right. Rect stashed in `close_btn` for the input handler.

### Scroll

`DialogManager::on_scroll` gets a branch for the lockfile modal. Minimum bound is 0; max bound is computed by the clip rect (content off the bottom won't render). Keeping max-clamp in the clip-rect helper rather than in on_scroll avoids having to re-compute the window-size-dependent list height in the input path.

### Input

`update_lockfile_findings_local` is called from `DialogManager::update` before the other dialog updates (so that an outside-click-to-dismiss doesn't conflict with anything else). Close button uses the rect captured at draw time; outside-click dismisses based on the panel rect re-computed from the same size formula as the draw path.

## Badge rendering

```cpp
lockfile_badge_rect_ = {};
const auto& lf = snap_.lockfile_status;
const bool draw_badge =
    !lf.findings.empty() &&
    (lf.overall == vivid::LockfileOverall::CompatibleDrift ||
     lf.overall == vivid::LockfileOverall::Mismatch);
if (draw_badge && fw > 780.0f) {
    float r, g, b;
    if (lf.overall == vivid::LockfileOverall::Mismatch) {
        r = 0.95f; g = 0.35f; b = 0.30f;  // red
    } else {
        r = 0.95f; g = 0.82f; b = 0.30f;  // yellow
    }
    const std::string label = "LOCK " + std::to_string(lf.findings.size());
    float badge_w = tr.text_width(label.c_str()) + kPerfBtnPadX * 2;
    // hover + draw
    // lockfile_badge_rect_ = {left_x, btn_y, badge_w, kPerfBtnH, true};
    // left_x += badge_w + kPerfBtnMargin;
}
```

The `fw > 780.0f` guard matches the existing "Unsaved" badge pattern — hide decorative chrome when the window is too narrow.

## Click routing

Inserted in `node_graph_input_click.cpp` right after the existing `diagnostics_panel` handlers (so dial-panel click absorption runs first). Consumes the click and calls `dialogs_.open_lockfile_findings(snap_.lockfile_status)`.

```cpp
if (lockfile_badge_rect_.visible &&
    mouse_.x >= lockfile_badge_rect_.x &&
    mouse_.x <= lockfile_badge_rect_.x + lockfile_badge_rect_.w &&
    mouse_.y >= lockfile_badge_rect_.y &&
    mouse_.y <= lockfile_badge_rect_.y + lockfile_badge_rect_.h) {
    dialogs_.open_lockfile_findings(snap_.lockfile_status);
    mouse_.left_clicked = false;
    return;
}
```

## Tests

`tests/ui/test_dialog_manager_lockfile.cpp` — seven cases:

1. `test_default_state_is_closed` — `lockfile_findings_open()` is false on a fresh DialogManager.
2. `test_open_flips_state` — after `open_lockfile_findings(status)`, open is true, findings copied, scroll_y reset.
3. `test_status_is_copied_not_referenced` — mutating the source status after open doesn't affect the modal's copy.
4. `test_close_resets_state` — `close_lockfile_findings()` flips open off, resets scroll.
5. `test_reopen_replaces_findings` — calling open twice with different statuses replaces the internal copy.
6. `test_any_open_includes_lockfile_findings` — `any_open()` becomes true while open and returns to false on close.
7. `test_wants_keyboard_does_not_include_lockfile_findings` — read-only modal never blocks keyboard input.

Uses a `StubSink : UICommandSink` with trivial pure-virtual implementations (same pattern as `tests/ui/test_ui_widget_interactions.cpp`).

Registered in `cmake/tests/20-ui-and-common.cmake`. Links the same libs as other DialogManager-level UI tests (`vivid_runtime_testlib`, `vivid_ui`, `webgpu`, `glfw`, `nlohmann_json`, `stb_truetype`, plus Cocoa/Foundation on Apple).

## Manual visual verification

```bash
# 1. Write a lockfile for a demo graph.
./build/vivid lock --graph graphs/intro/audio_demo.json

# 2. Mutate the sibling vivid.lock — e.g., change one operator's
#    descriptor_hash to a stale value.

# 3. Launch the runtime on that graph.
./build/vivid graphs/intro/audio_demo.json

# Expect:
#   - A yellow or red "LOCK N" badge in the top-right perf bar.
#   - Clicking it opens a modal with the overall status pill and
#     a scrollable findings list.
#   - Clicking outside the modal or the Close button dismisses it.
#   - Wheel-scrolling scrolls the findings list.
```

## Verification

```bash
cmake --build build --target test_dialog_manager_lockfile vivid
ctest --test-dir build --output-on-failure -R dialog_manager_lockfile
```

Plus the manual visual smoke above.

## Acceptance Criteria

- Badge appears only when `overall ∈ {CompatibleDrift, Mismatch}` and findings are non-empty. Hidden entirely otherwise.
- Clicking the badge opens the findings modal with overall status + scrollable list.
- Close button and outside-click both dismiss the modal; scroll resets.
- `any_open()` reflects the modal's state; `wants_keyboard()` does not (read-only, never steals keys).
- `ctest -R dialog_manager_lockfile` stays green; Phase 6a's `test_project_lockfile` stays green.
- No changes outside the "Modified" / "New" list.
- Three commits on `worktree-project-lockfile` beyond the Phase 6a commits.

## Out of Scope (Phase 6b)

- Action buttons ("install package", "rebuild package", "open vivid.lock in editor") — deferred.
- Per-node padlock icon / "LOCKED" label distinct from "MISSING" — Phase 6a's `missing_operator_detail` wiring already surfaces the reason in the tooltip; no additional decoration needed.
- Modal animation / transitions — intentionally plain on/off for simplicity.
- Keyboard navigation / a11y polish — deferred.
- Phase 7 (export strict mode), Phase 8 (asset hashing).

Phase 6b closes the Phase 6 arc. After it lands, the lockfile feature is visible and actionable through the GUI, without any further infrastructure.
