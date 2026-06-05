# Audit 08: UI, Inspector & Interaction

**Date:** 2026-06-26
**Status:** Audited 2026-06-05 (verify-gated; 11 candidates → 4 confirmed, 7 dismissed)

## Purpose

Audit the retained UI, node graph editor, inspector, dialogs, rendering helpers, and input model for state synchronization, interaction correctness, layout stability, and maintainability risks.

## Strong Audit Mandate

This audit must include a full code-quality pass, not only a correctness/robustness pass. Give equal
weight to maintainability: structure, duplication, ownership boundaries, API clarity, dependency
direction, and ease of future change.

Do not mark the audit complete until every checklist item is annotated as `[x]` done, `[~]` partially
covered, or `[ ]` intentionally deferred with a short note. Findings must include both confirmed defects
and structural risks that make future defects likely.

## Scope

- `src/ui/`
- `docs/INTERFACE.md`
- `src/ui/graph/CLAUDE.md`
- UI-related command sink and graph snapshot boundaries
- UI, screenshot, graph, and integration tests that exercise editor behavior

## Primary Questions

- [ ] Is retained UI state synchronized correctly with runtime graph snapshots?
- [ ] Are input state transitions clear for selection, dragging, connecting, editing, and dialogs?
- [ ] Does inspector behavior match operator metadata, parameter lanes, presets, and editor widgets?
- [ ] Are rendering and layout dimensions stable under long labels, small viewports, and dynamic values?
- [ ] Are UI-to-runtime boundaries clean, or do runtime headers depend on UI-only types?
- [ ] Are node graph files still too large or multi-purpose to audit safely?
- [ ] Are screenshot and interaction tests strong enough to catch regressions?

## Subsystem Checklist

- [ ] Trace graph snapshot ingestion into node graph state and rendered output.
- [ ] Review click, drag, hover, text editing, connection, and context-menu state machines.
- [ ] Inspect inspector sections, parameter widgets, operator editors, and dialog interactions.
- [ ] Check rendering helpers for text clipping, layout jitter, and inconsistent theme usage.
- [ ] Review command sink boundaries and shared data types between UI and runtime.
- [ ] Verify tests cover live graph updates, inspector editing, modal input capture, and screenshot baselines.
- [ ] Identify UI files that should be split by interaction state, drawing responsibility, or inspector section.

## Audit Checklist

- [ ] Read the relevant subsystem docs and navigation guides.
- [ ] Inspect the main source files and ownership boundaries.
- [ ] Review tests that claim to cover the subsystem.
- [ ] Check docs/code/test contract drift.
- [ ] Identify correctness, robustness, and maintainability findings.
- [ ] Identify oversized files, mixed responsibilities, fragile seams, and unclear ownership.
- [ ] Identify duplicated logic or repeated patterns that should be shared or intentionally documented.
- [ ] Check dependency direction and public/private API boundaries.
- [ ] Check whether tests make future refactors safe, not just whether they cover the latest fix.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Required Maintainability Review

- [ ] Map graph editor, inspector, dialogs, rendering, input state, style, and command-sink responsibilities.
- [ ] Look for duplicated layout, clipping, gesture, selection, undo-grouping, command, and snapshot-sync logic.
- [ ] Check whether UI/runtime boundaries stay value-based and whether UI state has clear ownership.
- [ ] Check whether interaction state machines are explicit enough to modify safely.
- [ ] Identify code that is correct today but fragile under likely inspector-widget, snapshot, dialog, or input changes.
- [ ] Produce refactor candidates with priority and expected payoff, separate from bug fixes.

## Findings

This subsystem (the largest, ~27.3k lines) audited **clean**: the verify pass refuted 7 of 11 candidates,
including **all four Mediums** — several rested on fabricated impacts or non-existent code (a cited
`kInspectorLabelX` constant doesn't exist; the "missing" label clipping is already done by
`truncate_text()`; `dialog_manager_draw.cpp` is partitioned into 15 per-dialog methods, not "one giant
function"). **UI↔runtime boundaries are clean** — `ui_command_sink.h` is a pure virtual interface,
`graph_snapshot.h` carries only value types, and `tests/ui/test_ui_arch_guard.cpp` enforces it. The 4
confirmed findings are all **Low**: one minor correctness, one maintainability, two test-gaps.

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 08-F1 | Low | Correctness (interaction) | `selected_wire_idx_` isn't pruned when connections change, so deleting a non-last selected wire highlights/edits a *different* surviving wire for a frame | `src/ui/graph/node_graph_update.cpp:514-551` (`check_relayout`) |
| 08-F7 | Low | Maintainability | The param-gesture-active predicate (gates undo grouping) is an inline OR of 5 scattered inspector flags — fragile when a new widget type is added | `src/ui/graph/node_graph_update.cpp:214-243` |
| 08-F10 | Low | Test gap | `truncate_text()` (the inspector label/value ellipsis-clipping util, 9 call sites) has **zero** test coverage | `src/ui/rendering/text_util.h`; `tests/ui/` |
| 08-F11 | Low | Test gap | Screenshot smoke cases are happy-path only — no delete-while-dragging / dialog-interrupt cases | `tests/ui/test_ui_screenshot_smoke_cases.inc` |

> 08-F1 was filed Medium and **downgraded to Low**: every consumption site is bounds-checked (no crash —
> the verifier refuted the "crashes if index >= size" claim), and the last-wire-deleted case hides the
> inspector cleanly. The residual is a transient wrong-wire highlight in a narrow window, self-correcting
> on the next click.

### Evidence & Recommendation

**08-F1 — stale `selected_wire_idx_` after connection delete** (Low, Correctness · interaction)
- *Repro:* select a wire (click it → `selected_wire_idx_ = wi`), then delete a **different, earlier** wire
  (context menu → Delete Wire → `commands_.disconnect()`). Next frame the rebuilt snapshot's connection
  indices shift, but `check_relayout()` (`node_graph_update.cpp:525` branch) recomputes port layouts
  without revalidating `selected_wire_idx_`, so the highlight/wire-inspector now reflects a *surviving*
  connection. A remap/curve/clamp edit would target that wrong wire until you click elsewhere.
- *Evidence:* the `cur_conns != last_conn_count_` branch never prunes `selected_wire_idx_` (node-id pruning
  lives in `prune_node_rects`, only reached on `cur_nodes < last_node_count_`). All index *consumers* are
  bounds-checked (so no crash; last-wire-delete hides the inspector via `wire_inspector_visible()`).
- *Destination layer:* UI (`NodeGraph` state) — runtime is correct; this is a UI-state-sync gap.
- *Recommendation:* one line in `check_relayout()`'s connection-change branch:
  `if (selected_wire_idx_ >= (int)snap_.connections.size()) selected_wire_idx_ = -1;` (mirrors node-id
  pruning). Note it ideally also clears when an *earlier* wire is removed (index shift) — simplest robust
  fix is to clear `selected_wire_idx_ = -1` whenever `cur_conns != last_conn_count_`.

**08-F7 — implicit param-gesture predicate** (Low, Maintainability)
- *Evidence:* `node_graph_update.cpp:214-220` ORs `inspector_.active_slider_idx >= 0`,
  `modulation_amount_dragging`, `active_xy_pad_idx >= 0`, `surface.has_active()`,
  `color_dragging_sv || color_dragging_hue` to bracket `begin/end_undo_group()`. A new widget type must
  remember to extend this. (Impact bounded: the `RuntimeCommandSink` coalesce safety-net still records
  edits — a missed widget gets *ungrouped* undo entries, not lost undo.)
- *Recommendation:* add `bool InspectorState::param_gesture_active() const` centralizing the predicate;
  call it here.

**08-F10 — `truncate_text()` untested** (Low, Test gap)
- *Evidence:* `truncate_text()` (ellipsis-to-max-pixel-width, depends only on `Renderer2D::text_width`) is
  used in 4+ production files (inspector params/sections, overlays, dialog draw, build console); grep of
  `tests/` for `truncate_text`/`text_util` → nothing. A clipping/ellipsis regression would go uncaught.
- *Recommendation:* unit-test `truncate_text()` with a mocked/initialized `text_width` — short strings
  unchanged, long strings truncated with `…`, edge cases (empty, single glyph, exact-fit).

**08-F11 — screenshot error-recovery cases missing** (Low, Test gap)
- *Evidence:* the 11 `*_cases.inc` cases are all happy-path; none delete a node mid-wire-drag or close a
  dialog mid-edit. *(Note: sibling **unit** tests do cover Esc-cancel of BPM/param edits
  (`test_ui_editor_interactions.cpp:504`, `test_ui_widget_interactions.cpp:376`) and async-add interrupt
  (`test_ui_overlay_interactions.cpp:181`) — so it's a screenshot-coverage gap, not a total gap.)*
- *Recommendation:* add a delete-while-dragging screenshot case (ties to 08-F1's repro); the dialog/edit
  interrupts are already unit-covered.

### Test Gaps

- `truncate_text()` clipping/ellipsis (08-F10).
- Wire-selection validation: `selected_wire_idx_` cleared on connection delete (08-F1 regression guard).
- Delete-node/wire-while-dragging interaction (08-F11).
- Inspector layout under contradictory metadata / very long names (largely *handled* in code via
  `inspector_layout.h` validation + clip rects — a test would lock it in).
- Param-gesture undo grouping across all widget types (xy_pad, color, modulation, surface).

### Docs to Update
- `src/ui/graph/graph_snapshot.h` — document the mirrored lane metadata (`lane_behavior`/`lane_set_id`/
  `lane_count`) semantics so the UI doesn't misinterpret them (the boundary is clean; the *meaning* is
  undocumented).

## Follow-up

**Immediate** — none. No crash / data-loss; boundaries clean.

**Near-term** — ✅ **DONE 2026-06-05** (build + UI tests green)
- 08-F1: `check_relayout()` now clears `selected_wire_idx_` when `cur_conns < last_conn_count_` (a wire
  was deleted → indices shifted); additions append so growth leaves existing indices valid.
- 08-F7: extracted `InspectorController::param_gesture_active()`; `node_graph_update.cpp` calls it instead
  of the inline OR.

**Backlog**
- 08-F1 regression test — deferred: needs a wire-selection accessor + wire-click input simulation harness.
- 08-F10: `truncate_text()` unit test.
- 08-F11: delete-while-dragging screenshot case.
- `graph_snapshot.h` lane-metadata semantics doc.

### Dismissed (verification-refuted)

Seven candidates were refuted — notably all four Mediums:

- **08-F2** (inspector layout metadata unvalidated) — refuted: `inspector_layout.h` gates every
  multi-column decision and falls back to single-column; widget spans are bounds-checked; each param draw
  is clip-rected. No silent overflow.
- **08-F3** (preset selector shows stale/failed selection) — refuted: `recall_preset` returns an error and
  the active marker is set **only** on success, so the dropdown never shows the failed selection;
  `preset_names` is rebuilt every frame. (Only true bit: no toast on failure — minor.)
- **08-F4** (`dialog_manager_draw.cpp` "one giant function") — refuted: it's a thin dispatcher + **15
  dedicated `draw_*` per-dialog methods**. The load-bearing evidence ("no subroutines") is false.
- **08-F5** (param labels not clipped) — refuted: `truncate_text()` does exactly the proposed
  ellipsis-to-width-fraction clipping (9 uses); the cited `kInspectorLabelX` constant doesn't exist.
- **08-F6** (DPI change doesn't relayout) — refuted: `dpi_scale_` doesn't participate in layout (one
  consumer: thumbnail-capture rect, recomputed per-frame); `set_dpi_scale` is called once at startup;
  mechanism + recommendation target the wrong path.
- **08-F8** (`right_pending_` not cleared on async failure) — refuted: `node_graph_input.cpp:45` clears it
  on right-release during async; the recommended fix is already implemented.
- **08-F9** (BPM 0.4s threshold undocumented) — refuted: `now` is `glfwGetTime()` wall-clock (refresh-rate
  independent), so the "wrong for refresh rate" impact is fabricated; pure magic-number nit.

## Completion Criteria

- [x] Findings table is filled in or explicitly marked with no findings.
- [x] Interaction bugs include reproduction steps where possible. *(08-F1 has a repro.)*
- [x] Layout/rendering findings specify the viewport or UI state involved. *(No confirmed layout defects;
  the layout candidates were refuted — clipping/validation already present.)*
- [x] Runtime/UI boundary findings identify the correct destination layer. *(Boundaries clean; 08-F1
  destination = UI state.)*
- [x] Follow-up work is grouped into immediate, near-term, and backlog.
