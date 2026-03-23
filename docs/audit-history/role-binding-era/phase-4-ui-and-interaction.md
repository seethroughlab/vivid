# Phase 4 — UI And Interaction Audit

## Scope Reviewed

Primary UI and interaction surfaces reviewed in this phase:

- retained node-graph controller structure in `src/ui/node_graph.*`
- inspector layout and draw behavior
- overlay layout and overlay interaction paths
- graph snapshot and the then-current role-binding UI read model
- theme/text-edit support and UI architecture guardrails
- existing inspector redesign notes in `docs/archive/INSPECTOR-UI-AUDIT-PLAN.md`
- earlier UI architecture exploration in `docs/internal/archive/CODE-REVIEW-PHASE4.md`

This phase focused on workflow readability and interaction quality, not runtime-core stability.

## Current Interpretation

This document remains historically accurate about the UI state audited during Phase 4, but parts of that UI model no longer exist in the current architecture.

At the time of this phase:

- the inspector consumed a role-binding-aware snapshot/read model
- the UI still exposed role-binding and reverse-reference surfaces such as `Referenced By`

Since then, role bindings have been removed. The current architecture uses owned embedded composition for host-local behavior, ordinary ports for transport, and explicit outputs for graph-visible sharing. The findings below should therefore be read as the UI recovery and signoff story for the then-current design, not as a description of the present-day inspector contract.

## Evidence Gathered

### Automated Phase 4 evidence bundle

Ran:

```bash
ctest --test-dir build --output-on-failure -R "test_theme_loader|test_overlay_layouts|test_inspector_layout|test_ui_overlay_interactions|test_ui_arch_guard|test_text_edit|test_graph_snapshot_contract"
```

Observed result:

- 7 of 7 matched tests passed

Passing lanes:

- `test_theme_loader`
- `test_overlay_layouts`
- `test_inspector_layout`
- `test_ui_overlay_interactions`
- `test_ui_arch_guard`
- `test_text_edit`
- `test_graph_snapshot_contract`

### Focused source-backed UI review

Reviewed:

- `src/ui/inspector_layout.h`
- `src/ui/node_graph_draw.cpp`
- `src/ui/node_graph_constants.h`
- `docs/archive/INSPECTOR-UI-AUDIT-PLAN.md`
- `docs/internal/archive/CODE-REVIEW-PHASE4.md`

Key current-state observations:

- the inspector width is now `400px`, which is an improvement over the older narrower baseline
- `InspectorLayout` normalizes legacy `layout_columns >= 3` metadata into:
  - two-up rows for knobs
  - full-width rows for most other widgets
- the draw path still assembles inspector rows manually in `node_graph_draw.cpp`, including:
  - labels
  - values
  - semantic hints
  - connection badges
  - MIDI/lock badges
  - the then-current role-binding and reverse-reference sections

### Manual UI validation note

The original Phase 4 audit was conservative because live visual validation had not happened yet.

That gap is now closed with deterministic live-session GUI capture from the running runtime via the
MCP bridge:

- `ensure_runtime(graph_path="graphs/gpu/instanced_shapes_demo.json")`
- `capture_image(mode="interface", node_id="shapes", save_path="/tmp/vivid_live_ui_review/instanced_shapes_shapes.png")`
- `capture_image(mode="interface", node_id="scale_lfo", save_path="/tmp/vivid_live_ui_review/instanced_shapes_scale_lfo.png")`
- `load_graph(path="graphs/gpu/particle_envelope_demo.json")`
- `capture_image(mode="interface", node_id="env", save_path="/tmp/vivid_live_ui_review/particle_envelope_env.png")`

Those captures provided the required whole-interface evidence for:

- one dense inspector (`Instanced Shapes`)
- one `LFO`
- one `Envelope`
- one case that, at the time, exposed `Referenced By`

The same live review session also attempted an optional sanity capture for
`../vivid-wavetable/graphs/extended/wavetable_dream_keys_demo.json` → `cp1`.
That follow-up is **not** part of the Phase 4 acceptance gate. It exposed a
separate missing-package runtime path when `WavetableSynth` was unavailable, so
it is tracked as follow-up runtime hardening rather than as a reopened Phase 4
inspector failure.

## Findings

### 1. Core UI interaction contracts look healthy in the tested surfaces

- Severity: `note`
- Workstreams:
  - `overlay interaction`
  - `theme and text editing`
  - `snapshot-fed UI contracts`
- Evidence:
  - all targeted UI tests passed
  - `test_overlay_layouts` and `test_ui_overlay_interactions` cover deterministic overlay geometry and click behavior
  - `test_text_edit` and `test_theme_loader` keep basic editing/styling support covered
  - `test_graph_snapshot_contract` remained green for the then-current UI read model, including role-binding snapshot truth
- Current read:
  - there is no evidence here of broadly broken UI interaction plumbing

### 2. The inspector system needed a broader readability refactor, and that follow-up is now complete

- Severity: `fixed`
- Workstreams:
  - `inspector layout`
  - `inspector readability`
  - `then-current role-binding and metadata density`
- Evidence:
  - the inspector planner now grants `two_up` only for explicit adjacent compact-safe pairs; unsafe or broken legacy pairings collapse to `full`
  - standard param rows now reserve vertical space for their actual widget stack instead of compressing dropdowns, bools, and metadata into the same row rhythm as plain sliders
  - the then-current role-binding and `Referenced By` sections were reworked into stacked cards instead of dense inline hint/action rows
  - the then-current role-binding headers no longer exposed runtime-scope wording like `Per-Voice` / `Shared` in the main inspector surface, which kept the visual UI focused on role label, target, and actions instead of runtime implementation detail
  - deterministic screenshot evidence now shows:
    - no overlap in `Instanced Shapes`
    - clean historical `Referenced By` presentation on `scale_lfo`
    - readable `Envelope` inspector hierarchy with custom content and reverse references
    - cleaner historical role-binding headers after removing the misleading scope label from visual inspectors
- Current read:
  - this was a real release-facing problem
  - the implementation pass addressed the geometry and hierarchy issues strongly enough to close the item for release
  - that specific role-binding UI model has since been removed as part of the architecture simplification

### 3. Automated UI coverage is good for contract safety, but still thin for visual quality

- Severity: `defer`
- Workstreams:
  - `UI test depth`
  - `visual regression confidence`
- Evidence:
  - the current UI tests validate layout math, interaction dispatch, and architecture boundaries
  - they do not provide a strong visual or screenshot-level regression signal for inspector readability, truncation quality, or information hierarchy
- Current read:
  - this is worth improving later
  - it should not block Phase 4 signoff by itself

### 4. The archived inspector redesign plan still matches the codebase well enough to use as the active implementation guide

- Severity: `note`
- Workstreams:
  - `Phase 4 implementation direction`
- Evidence:
  - the archived plan’s main claims still line up with the current code:
    - legacy column metadata is still advisory
    - the inspector still relies on normalization rather than explicit row semantics
    - readability issues are now more about density and hierarchy than outright geometry corruption
- Current read:
  - the archived plan should be revived as the implementation basis for the release-facing inspector cleanup rather than reinvented from scratch

## Required Fixes For Release

### Immediate release blockers

- None established by this phase.

### Required before release, but not currently classified as standalone blockers

- None remaining from Phase 4 after the inspector recovery pass and manual screenshot signoff.

## Deferred Follow-Ups

Still explicitly deferred from this phase:

- deeper visual-regression tooling for UI screenshots/layout quality beyond the new screenshot smoke lane
- broader GPU-capable automation for GPU-only demo verification
- broader UI modularity cleanup for `NodeGraphUI` beyond the inspector card/section helper extraction already landed

## Signoff Status

- `pass`

Reason:

- the tested UI contracts are healthy
- the follow-up inspector recovery addressed the previously open readability issue
- manual screenshot signoff now exists for the required high-frequency inspector cases via the live
  running-instance MCP workflow

---

**Note (March 2026):** Role bindings were an intermediate design that has since been removed. The codebase now uses owned embedded composition for host-local modulation, ordinary ports for graph transport, and explicit outputs for cross-domain sharing. See `docs/EMBEDDED-OPERATOR-SLOTS.md` for the current architecture.
