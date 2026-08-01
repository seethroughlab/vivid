# Phase 4: Input, Accessibility, And Error Recovery

Status: done (audited 2026-08-01)

## Verdict

**PASS with follow-ups — no P0/P1.** The three load-bearing results are healthy. **Input has no
text-entry command leakage** — the only text fields are the four modal choosers (operator chooser,
audio chooser, param-add palette) and the Gemini-key modal, and every one **owns the keyboard while
open** (`key_callback` returns early; `char_callback` only feeds those four buffers); there is no
inline rename field to leak from. **Error surfacing is strong**: an Error-level event auto-raises a
**toast** (bottom-right) *and* lands in the **log** (`j`) with the full text, a level colour, and a
timestamp (`evidence/phase-04/01-error-toast.png`, `02-log-view-errors.png`), on top of node error
badges + the diagnostics panel — and a failed load now **leaves the current project intact** (Ph2
F4 fix). **Recovery is understandable**: autosave recovery is a native macOS dialog naming the
project ("Vivid found unsaved changes to …"). Contrast meets WCAG AA (the `dim` label colour on the
dark-steel background is ≈5.9:1), and focus is a consistent blue border.

Findings are **3×P3**, all accessibility/discoverability follow-ups: there is **no motion/flash
(photosensitivity) mitigation or warning** for the audio-reactive output; **quarantined operators
have no in-app surface** (only MCP), so an auto-disabled op is unexplained in the GUI; and the
**single-key shortcuts are undiscoverable** (no in-app list) with graph/session editing being
mouse-only. None block release, but the flash hazard and the accessibility bar are the phase's Open
Questions and are flagged for a product decision.

## Purpose

Verify that users can operate the release candidate confidently with mouse, keyboard, text entry,
menus, file dialogs, plugin windows, and error states.

## User Task

Perform common edits and recover from common mistakes without needing to restart the app or inspect
logs.

## Hypothesis

If input and recovery are release-ready, Vivid will feel resilient even when users explore the edges
of the interface.

## Pressure Test

Exercise selection, dragging, typing, popup/menu navigation, keyboard commands, undo/redo, file
open/save failures, plugin scan failures, bad operator loads, and crash recovery surfaces.

## Scope

- Pointer selection, dragging, canvas gestures, menus, popup menus, text entry, keyboard commands,
  file dialogs, transport controls, plugin windows, toast/error surfaces, autosave, and crash
  recovery.
- Accessibility basics: readable contrast, target size, keyboard reachability, focus visibility,
  motion/flash hazards, and screen text clarity.
- Recoverability of common user mistakes and expected external failures.

Out of scope: full assistive-technology certification unless it becomes a release requirement.

## Audit Procedure

1. Build an input matrix covering mouse, trackpad-like dragging, keyboard commands, text entry, and
   menu navigation for each primary view.
2. Run conflict tests: type while transport shortcuts exist, drag while selection changes, open
   popups while playback runs, and switch focus while edits are pending.
3. Trigger recoverable failures: invalid file path, missing asset, bad operator/package, failed
   plugin scan, and simulated crash recovery if available.
4. Inspect whether errors appear where the user is already looking and whether they suggest a next
   action.
5. Run accessibility spot checks on contrast, target size, focus visibility, text truncation, and
   keyboard-only reachability.

## Evidence To Collect

- Input matrix with pass/fail/needs-follow-up for each region and modality.
- Failure transcript: action, visible response, recoverability, and logs if relevant.
- Screenshots of focus, warning, error, and recovery states.
- Accessibility notes with concrete UI references.

## Deliverables

- Input conflict list with severity and reproduction steps.
- Error/recovery inventory with release action.
- Accessibility spot-check report.

## Acceptance Criteria

- Keyboard and pointer interactions do not conflict across active UI regions.
- Text entry captures and releases focus predictably.
- Error messages are visible, actionable, and do not silently disappear.
- Crash recovery, autosave, and quarantine states are understandable to a user.
- Basic accessibility checks cover contrast, readable text, target size, and keyboard reachability.

## Failure Modes

- A focused text field leaks commands to the app.
- Failure states appear only in logs.
- Undo/redo changes hidden state without visible confirmation.
- Plugin or operator failures leave the project half-loaded or unrecoverable.

## Evidence Log

Method: read the input routing (`app/src/app/input.cpp` `key_callback`/`char_callback`,
`input_typing.cpp`); drove the running build (commit `34a44ca7`, post-#210) to trigger an Error-level
failure and captured the toast + log; and inspected the recovery / quarantine / motion-flash /
contrast surfaces in code. Screenshots under `evidence/phase-04/`. Paths relative to repo root.

### A. Input matrix + conflict tests

| Region / modality | Result | Notes |
|---|---|---|
| Text entry (4 modal choosers + Gemini key) | **PASS** | each owns the keyboard while open (`key_callback` returns; `char_callback` feeds only these); no leak |
| Inline rename (track/scene/node) | **n/a** | no GUI inline-rename text field exists (rename is MCP-side), so nothing to leak |
| Single-key shortcuts (`M/H/J/L/Space/R/Tab/\``, `⌘Z/⌘⇧Z`, `1-9`) | **PASS (conflict-free)** | fire only when no modal/chooser owns the keyboard; safe given no inline text editing |
| Musical typing (`` ` `` toggle; A/W/S/E… = notes) | **PASS w/ micro-conflict** | note-letters (incl. `H/J/L`) are swallowed as notes while typing is ON — correctly mode-disambiguated; only `M` (not a note) still toggles the mapping overlay during musical typing (→ F3 note) |
| Pointer select / drag on canvases | **PASS** | drag gestures group into one undo entry; a leaked release is reconciled by the frame watchdog (`edit_gateway::close_open_group`) |
| Undo/redo (`⌘Z`) | **PASS** | routes through the clip editor first (note-undo) then the document; the change is itself the visible confirmation; Edit-menu labels track history. (The Ph2 F1 undo-on-load blocker is fixed.) |
| Popups during playback | **PASS** | overlays (`M/H/J/L`) toggle while the transport runs without stealing transport keys |

**No text-entry command leakage** (the phase's primary failure mode) and **no cross-region key
conflict** were found. The only ergonomic wart is the musical-typing `M` micro-conflict (P3, folded
into F3).

### B. Error / recovery inventory

| Failure | Surface(s) | Actionable? | Recoverable? |
|---|---|---|---|
| Bad file path / malformed project (Open) | **toast** (bottom-right, red) + **log** (full text, leveled, `j`) | yes — names the path + reason (Ph2 F5) | yes — **current project left intact** (Ph2 F4); evidence 01/02 |
| Missing operator in a loaded project | node error badge + `get_ops.broken_ops` + `get_health.missing_ops` + diagnostics panel | yes — names the op | yes — project loads, node badges |
| Shader compile error | node badge + toast + last-good pipeline kept | yes | yes (last-good) |
| Startup operator/shader scan failure | routed to `app.log` (code audit #197/#200) | yes | yes |
| Crash / unsaved work | **native recovery dialog** on next launch ("Vivid found unsaved changes to …") | yes — names the project | yes — reloads the autosave slot |
| Repeat-crasher operator (quarantine, ADR-0018) | **MCP only** (`no_quarantined_operators`, `scan_quarantine`) | — | auto-disabled, but **no GUI explanation** (→ F2) |

Error surfacing is a clear PASS on the acceptance criteria ("visible, actionable, don't silently
disappear") — the toast is transient but the log keeps the full leveled record. The one gap is
quarantine (F2).

### C. Accessibility spot-check

| Check | Result | Reference |
|---|---|---|
| Contrast (text/labels on dark steel) | **PASS (AA)** | `text` #E6EBEF ≈14:1; `body` ≈8:1; `dim` #8D9499 on `bg` #121214 ≈**5.9:1** (`ui_style.h:17-30`, "legible on steel"). Caveat: a few labels drawn at ~0.72 alpha dip toward the 4.5:1 line |
| Focus visibility | **PASS** | consistent blue (`sel` #5A8CD9) 1px border marks selection/focus |
| Text truncation | **PASS** (post-Ph3 F3) | shader-lib descriptions now ellipsize; toast truncates a long path but the log shows it in full |
| Target size | **OK (desktop mouse)** | 44px isn't the bar for a mouse app; node port dots + `ARM/VIZ` chips are small/fiddly but hit-tested accurately (code audit Ph3 §D) |
| Keyboard-only reachability | **PARTIAL** | overlays/transport/undo/scene-launch are keyboard-reachable, but graph/session **editing** (drag nodes, wire, click clips) is mouse-only — no keyboard node navigation |
| Motion / flash (photosensitivity) | **HAZARD — no mitigation** | UI feedback flashes are mild (queued-clip pulse ≈2Hz, under the 3Hz threshold), but the **audio-reactive output can strobe** with no reduce-motion toggle, flash-limit, or warning (grep: no `reduce.?motion`/`flash.?limit`/`photosens`) (→ F1) |

### D. Findings

#### F1 (P3): No motion/flash (photosensitivity) mitigation or warning for the reactive output

- Surface: the visual output pipeline + first-run/docs (no `reduce-motion` / flash-limit / warning
  anywhere in `app/src`).
- Impact: audio-reactive visuals can strobe rapidly (that's the point), but a first *public* build
  ships with no photosensitivity warning, no reduce-motion toggle, and no flash-limiting — a real
  seizure-risk consideration for a public release. (In-app *feedback* flashes are safe: the
  queued-clip pulse is ≈2Hz, below the 3Hz photosensitive threshold — `session_view.cpp:534`.)
- Evidence: no matches for `reduce.?motion|flash.?limit|photosens|strobe` in `app/src`; the output is
  fully user-/agent-authored with no rate cap.
- Smallest acceptable fix: a **photosensitivity note** in the first-run copy / release notes (a
  reduce-motion / flash-limit toggle is a larger follow-up). This is the phase's accessibility-bar
  Open Question — flagged for a product decision. Owner/status: **fixed (note)** | P3.
- **RESOLVED** (branch `fix-p3-first-run-polish`): added a **Photosensitivity note** to the first-run
  guide (`site/content/start-here.md`) — warns that audio-reactive visuals can flash/strobe, suggests
  a smaller output window / lower intensity, and states Vivid does not yet flash-limit. A reduce-motion
  toggle remains the larger follow-up.

#### F2 (P3): Quarantined operators have no in-app surface

- Surface: `ui/diagnostics_panel` / `get_health` vs the quarantine system (ADR-0018,
  `scan_quarantine`).
- Impact: when an operator crashes repeatedly it is auto-disabled (quarantined) on the next launch —
  but this is exposed **only over MCP** (`no_quarantined_operators`). The diagnostics panel shows
  `missing_ops` (unregistered) but not quarantined ops, so a user whose operator silently stopped
  working has no in-app explanation of *why*. Touches the acceptance criterion "quarantine states are
  understandable to a user."
- Evidence: no `quarantin*` reference in any `ui/*.cpp` draw path; `run_quality_check
  no_quarantined_operators` is the only surface.
- Smallest acceptable fix: add a "quarantined operators" row/section to the diagnostics panel (and
  `get_health`), naming the op + crash count + the unquarantine path. Owner/status: **fixed** | P3.
- **RESOLVED** (branch `fix-p3-first-run-polish`): the diagnostics panel (`ui/diagnostics_panel.cpp`)
  now scans `scan_quarantine(crash_dir)` while open and draws a **"QUARANTINED OPERATORS (auto-disabled;
  restart to re-enable)"** section listing each op + crash count (gold). `diag_geom` grows for the
  section. So a user whose op silently stopped now sees why in-app, not only over MCP.

#### F3 (P3): Single-key shortcuts are undiscoverable; keyboard-only editing is partial

- Surface: `app/src/app/input.cpp` shortcut set; no in-app shortcut list; no menu entry for it.
- Impact: the app is driven by single-key shortcuts (`M/H/J/L` overlays, `Space/R` transport, `Tab`
  add-node, `` ` `` musical typing, `⌘Z/⌘⇧Z` undo, `1-9` scene launch, `Esc` close) with **no in-app
  cheat-sheet** — a user can't discover them (compounds Phase-1 F3 onboarding + the Phase-3 keybind
  hand-off). Separately, graph/session **editing** is mouse-only (no keyboard node navigation), so
  keyboard-only operation is partial. The musical-typing `M` micro-conflict (M toggles mappings while
  "typing music") lives here too.
- Evidence: input matrix §A; `menu_bar.mm` has no Help/shortcuts entry; grep found no shortcut-list
  overlay.
- Smallest acceptable fix: an in-app shortcut cheat-sheet (e.g. a `?` overlay) + list the shortcuts
  in the docs; full keyboard navigation is a larger, post-release follow-up. Owner/status:
  **fixed (cheat-sheet)** | P3.
- **RESOLVED** (branch `fix-p3-first-run-polish`): a **`?` keyboard-shortcut cheat-sheet** overlay
  (`draw_shortcuts_overlay`, toggle `?` / Esc) lists the official set (Space/R, Tab, `` ` ``, 1–9,
  M/H/J/L, ⌘Z/⌘⇧Z, ⌘N/O/S, ?, Esc) — verified live (`evidence/phase-04/03-shortcuts-cheatsheet.png`).
  Full keyboard-only *editing* (node navigation) and the musical-typing `M` micro-conflict remain the
  larger post-release follow-up.

## Open Questions

*(answered)*

- **Which keyboard shortcuts are official release behavior?** The single-key set is: `Space`
  play/stop, `R` record, `M/H/J/L` mapping/diagnostics/log/shader-library overlays, `Tab` add-node
  (context: audio vs visual by cursor), `` ` `` musical-typing toggle, `⌘Z` / `⌘⇧Z`(+`⌘Y`) undo/redo,
  `⌘N/⌘O/⌘S/⌘⇧S` file, `1`–`9` launch scene, `Esc` close overlay/chooser. These are the official set;
  they should be **documented and surfaced in-app** (F3).
- **What errors are allowed to be developer-log-only during first release?** In practice only
  Debug/Info entries are log-only (appropriate); every **Error**-level event is user-facing (toast +
  log), and the previously stderr-only startup-scan errors were routed into `app.log` by the code
  audit (#197/#200). So the dev-log-only tier is correctly limited to non-actionable detail.
- **What accessibility bar is required for the first public build?** A product decision (flagged).
  Recommended **minimum** for first release: a photosensitivity note (F1) + an in-app shortcut
  cheat-sheet (F3) + surfacing quarantine (F2). Full keyboard navigation and formal AT certification
  are explicitly **out of scope** for this phase and are post-release work.

## Follow-Up Plans

- **F1** (photosensitivity note) and **F3** (shortcut cheat-sheet) are the two release-facing
  accessibility items; both are small (copy + a `?` overlay) and pair well with the Phase-6 first-run
  work. **F2** (surface quarantine in diagnostics) is a self-contained UI addition.
- **Cross-refs:** F3 closes the Phase-3 keybind-discoverability hand-off and compounds Phase-1 F3
  (no in-app onboarding) — together they argue for a small first-run/help surface, which Phase 6
  should scope. The clean input/error/recovery results here rest in part on the just-merged Phase-2
  fixes (F1 undo-on-load, F4 project-intact-on-failed-load) and code-audit Phase-3 (badges/toasts).
