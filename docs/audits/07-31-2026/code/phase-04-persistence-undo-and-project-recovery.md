# Phase 4: Persistence, Undo, And Project Recovery

Status: done (audited 2026-07-31)

## Verdict

**FAIL — 3×P1 (two are candidate-P0), + 2×P2, 3×P3.** The resilience *scaffolding* is
genuinely strong: autosave writes a separate slot and never touches the user's file;
crash recovery treats the warm snapshot as diagnostic-only and gates any restore behind a
consent modal (last-known-good is never overwritten without an explicit Save); save/load
error paths are truthful and roll back on failure; unknown/missing JSON fields degrade
gracefully. **But** three release-grade gaps sit on top of it: a use-after-free when a
Full-tier undo runs with a plugin editor window open (P1-01, candidate P0), silent
parameter data-loss when a degraded project is saved (P1-02, candidate P0), and the entire
save/load restore path has **no end-to-end round-trip test** (P1-03). The two candidate-P0s
are crashes/data-loss under realistic sequences — I've marked them P1; **flagging for your
call on whether either blocks the RC outright, as with Phase 2's P0-01.**

## Purpose

Verify that user work survives ordinary editing, save/load cycles, crashes, autosave, undo/redo, and
project format evolution.

## User Task

Create a project, make layered edits, undo and redo changes, save, reopen, simulate recovery, and
confirm the audiovisual result is preserved.

## Hypothesis

If persistence and recovery are healthy, the first release will protect user trust even when the app
or an operator fails.

## Pressure Test

Audit project serialization, undo capture, autosave, crash snapshots, quarantine handling, schema
compatibility, and file action error paths.

## Scope

- Project file format, serialization/deserialization, undo manager, edit gateway, file actions,
  autosave, crash recovery snapshots, quarantine, package/operator references, and example project
  compatibility.
- State produced by audio graph, visual graph, mappings, clips, transport, plugin params, and
  package metadata.

Out of scope: long-term migration framework beyond the first-release compatibility promise.

## Audit Procedure

1. Build a release-supported state inventory and identify where each item is authored, stored,
   serialized, and restored.
2. Run round-trip tests for representative projects: blank sketch, bundled example, package-backed
   graph, and plugin-using project if release-supported.
3. Trace undo/redo capture through representative edits across audio, visual, mapping, and project
   metadata.
4. Simulate or inspect failure paths: save failure, load failure, crash snapshot, autosave recovery,
   missing package/operator, and unknown fields.
5. Compare persisted state with agent/control-server-visible state to catch hidden divergence.

## Evidence To Collect

- State inventory table with source of truth, serializer, undo behavior, and tests.
- Round-trip command summaries or manual transcripts.
- Before/after project snippets for representative edits if useful.
- Recovery scenario notes with user-visible outcomes.

## Deliverables

- Persistence coverage report.
- Undo/recovery risk list with release severity.
- Compatibility and migration notes for release examples and user projects.

## Acceptance Criteria

- One source of truth is serialized for every release-supported creative object.
- Undo/redo records user-intent operations, not accidental implementation side effects.
- Save/load round trips are covered by tests for representative audio, visual, bridge, and package
  state.
- Recovery flows never overwrite the user's last known good project without consent.
- Unknown or future project fields fail gracefully.

## Failure Modes

- Round trips preserve structure but change audible or visible output.
- Undo misses hidden state or restores stale pointers.
- Autosave/recovery creates competing versions without explaining them.
- Schema changes break existing examples or user projects.

## Evidence Log

Method: three source sweeps (state inventory + round-trip; undo capture/restore; recovery
+ error paths), with the two candidate-P0 findings re-read directly. Serializer
`session_to_json` (`persist.cpp:21`); deserializer `session_from_json_scoped`
(`persist.cpp:518`).

### A. Release-state inventory

Source of truth is split cleanly: audio state = the opaque `vivid::session::Session*`
(reached only via `session_*` accessors — `session_to_json` never touches structs
directly); visual + bridge = `ui::NodeGraph& g`. `persist.cpp` serializes and restores:
scenes/master gain, per-track name/gain/mute/solo/kind/id, audio clips (PCM *not* persisted,
`persist.cpp:281`), MIDI clips + note-expression, per-scene generators, FX chains (by
catalog name), native audio ops, the per-track authoritative audio graph (id-remapped on
load, `:653-714`), cross-track control/audio/note edges, clip pool, the visual graph
(ops/params/edges/asset), mappings/bridge wires, transport, and window layout. Opaque
plugin state is base64 (VST3 zlib `z:`-prefixed) at three surfaces (track instrument,
CLAP effects, per-node), and — correctly per ADR-0030 — the host-owned authored param
*base* is serialized **separately** from the opaque plugin chunk (`persist.cpp:201-205` vs
`:184-192`), state applied first then base re-applied over it.

### B. Round-trip fidelity

**No golden full-document round-trip test exists.** `session_to_json` /
`session_from_json_scoped` have four callers, none a test; no `app/tests/` case serializes a
populated Session and deserializes it into a second Session to compare. The closest,
`test_clap_plugin_roundtrip.cpp`, hand-simulates the persist flow (set_state + param) and
asserts one float — it never goes through the JSON serializer. So the entire multi-hundred-
line restore path (track rebuild, graph id-remap, edge replay, cross-track edges,
generators, FX-by-name resolution) is uncovered. → P1-03.

### C. Undo capture + restore

Capture is correct: `EditGateway::note_edit` defers to end-of-frame `commit_frame`
(`edit_gateway.cpp:94-108`, after draw-settle), coalesces by key (300 ms), and groups
gestures — keyed to user-intent labels, not arbitrary writes. The canonical projection
(`persist_undo.cpp`) strips window/view/base/output-preview/launched-clip/opaque-plugin-
state — all correctly performance/view/plugin-owned, not document. MCP capture is central at
the `process_pending` chokepoint (`control_server.cpp:85-87`) via the `edit_methods` table.
Restore is tiered (`Skip`/`ParamsOnly`/`Full`, `edit_gateway.cpp:134-136`).

Restore-path issues: the mapping registry (string-keyed, stable-id), `ControlCtx`
(container pointers), and visual `EditorWindow` (id-bound) all survive a rebuild safely.
**The exception is floated native plugin GUI windows** → P1-01.

### D. Recovery / autosave / error paths (clean)

Autosave: separate slot `user_data_dir()/autosave/` (never the user's file), 15 s cadence
while dirty (`frame.cpp:923-929`), best-effort. Crash recovery: the warm snapshot is
diagnostic-only (used to attribute a node id, never loaded as a document); the only
work-restoring path is a **binary consent modal** (`confirm_recover_autosave`,
`main.cpp:436-451`) that loads into memory and marks dirty — disk is overwritten only by an
explicit Save. Acceptance criterion "never overwrite last-known-good without consent" is
**upheld**. Save/load error paths are truthful: `SaveResult`/`LoadResult` carry errors,
`VLOG_ERR` toasts them, `project_io::load` rolls back to a safe default on failure rather
than leaving a half-loaded graph, and a failed save leaves the autosave slot intact.
Missing/quarantined operators load as `op_missing()` preserving node id/topology/edges/asset
(ADR-0019/0040), badged red + surfaced by `validate_project`.

### E. Findings

#### P1-01 (candidate P0): Full-tier undo/redo dangles floated native plugin GUI windows → use-after-free

- Surface: `app/undo_manager`/`edit_gateway` restore path vs `app/window` plugin windows
- Impact: a `Full`-tier restore tears down every track + clears the audio graph
  (`persist.cpp:363-364,657`), freeing the VST3 `IEditController` / `ClapHandle`. But
  nothing in the restore path closes the floated editor windows (`Window::track_win/fx_win/
  clap_win`, `window.h:173-175`) — which hold **raw handles into those freed instances**.
  The manual track-removal path closes them first *precisely to avoid this dangle*
  (`input_clipgrid.cpp:144-149`, comment: "a lingering window would dangle the plugin
  pointer"); the undo path omits that guard. `reap_plugin_windows` (`frame.cpp:198/202/206`)
  only closes windows already reporting `!is_open()` — a window over a freed instance still
  reports open, so the next frame's `is_open()`/render dereferences freed memory. **UAF
  crash** on undo/redo of Add/Delete Track, Set Instrument, or an audio-graph structural edit
  whenever a plugin editor is open — a common workflow. (`fx_win` is not even closed by the
  manual path — a latent second gap.)
- Evidence: `input_clipgrid.cpp:144-149` (guarded manual close) vs `persist.cpp:363-364,657`
  (restore teardown, no close); `frame.cpp:196-208`; `window.h:173-175`.
- Smallest acceptable fix: in the restore path (or a pre-`Full`-restore hook), close + null
  all `track_win`/`fx_win`/`clap_win` before track teardown, mirroring
  `input_clipgrid.cpp:144-149`; and/or make `reap_plugin_windows` close any window whose
  backing track/node no longer exists.
- Owner/status: Unassigned | **candidate release blocker** | own gated PR + regression test

#### P1-02 (candidate P0): a missing operator's parameter values are silently dropped on save-back

- Surface: `persist.cpp` save/load param loops vs `op_missing` nodes
- Impact: a missing/quarantined op has no live instance → `op_param_count_at(i)` returns 0.
  On **save**, `session_to_json` writes `params:{}` (`persist.cpp:319-320` loops 0×); on
  **load**, the param-restore loops (`persist.cpp:798-818`) copy nothing (nowhere to store —
  `VisualNode` has no raw-payload field). So opening a degraded project (references an
  uninstalled/quarantined package) and saving **permanently drops the user's tuned parameter
  values** for those nodes; only op_type/id/edges/asset survive. ADR-0018 "don't lose your
  work" holds at the topology level but not the parameter-data level.
- Evidence: `persist.cpp:319-320` (save loop keyed off `op_param_count_at`); `:798-818`
  (load loop); `ui/node_graph.cpp:101-103,458` (count 0 for missing op); `VisualNode` struct
  `gpu/visual_graph.h:28-45` (no orphan store).
- Smallest acceptable fix: retain the raw `params`/`file_params`/`pinned` JSON on the
  `VisualNode` for missing ops (an orphan payload) and write it back verbatim on save, so a
  round-trip through a degraded project is lossless.
- Owner/status: Unassigned | **candidate release blocker** | own gated PR + test

#### P1-03: No end-to-end save/load round-trip test

- Surface: `app/tests/` coverage of `persist.cpp`
- Impact: the core promise of this phase — user work survives save/reopen — has zero
  end-to-end automated evidence (§B). A regression in the restore path (id-remap, edge
  replay, FX-by-name, generators) would ship silently. Violates the acceptance criterion
  "save/load round trips are covered by tests for representative audio, visual, bridge, and
  package state."
- Smallest acceptable fix: a golden round-trip test — populate a Session + NodeGraph,
  `session_to_json` → `session_from_json_scoped` into a fresh pair, assert structural
  equality (and, ideally, a short rendered-audio/frame hash) for blank, bundled-example, and
  package-backed projects.
- Owner/status: Unassigned | P1 | own gated PR

#### P2-01: Several MCP document-mutating methods miss undo (and one drifts the projection)

`audio_graph_set_node_param_by_name`, `set_audio_op_param_by_name`,
`connect_mapping_by_intent`, `set_param_by_intent`, `audio_graph_load_sampler`,
`load_node_preset`, and `set_launch_quantize` mutate the document but are absent from
`cli/edit_methods.cpp` → no undo entry. `set_launch_quantize` is worse: `launch_quantum_bars`
*is* in the canonical projection (`persist.cpp:39,555-556`), so it changes a document field
with no undo entry and would trip the undo audit. Fix: add the genuine document-mutators to
the edit-methods table (with coalesce keys where apt). Owner/status: Unassigned | P2.

#### P2-02: The VIVID_UNDO_AUDIT safety net runs in no test or CI

The build-time guard that asserts no edit bypasses the gateway (`edit_gateway.cpp:146-170`)
is `OFF` by default, a plain `assert` (compiled out under `NDEBUG`), and referenced in no
CI workflow and no test — the two undo tests exercise only the pure projection + the undo
stack, never the gateway. It cannot catch a regression (and is exactly why P2-01 went
unnoticed). Fix: a headless test that drives `EditGateway` with `VIVID_UNDO_AUDIT` on across
representative edits. Owner/status: Unassigned | P2.

#### P3-01: Full restore reassigns audio-graph node ids → stale audio-editor selection

`Full` restore assigns fresh internal ids (`persist.cpp:658-679`); the audio node editor
caches `sel_node_` by id (`ui/audio_node_graph.h:201`) → wrong-node selection after undo (an
`int`, so not memory-unsafe). Fix: re-resolve or clear the selection post-restore.
Owner/status: Unassigned | P3.

#### P3-02: A wrong-*type* (not merely unknown) JSON field can throw uncaught from restore

Unknown/missing fields degrade gracefully (`.value(key,default)` throughout), but a handful
of `.get<T>()` calls (`persist.cpp:446,586,681,694,838`) assume the JSON type once a key is
present; `load_session` catches only the initial parse (`:872`), not the restore body, so a
type-corrupted-but-present field throws to the caller. Fix: wrap the restore body or
type-check those reads. Owner/status: Unassigned | P3.

#### P3-03: Stale `persist.h` header comment

`persist.h:66-71` still says "the track set itself is NOT persisted"; v2 made the track set
part of the document (`session_to_json:50-54`, rebuild `:362`). Fix: correct the comment.
Owner/status: Unassigned | P3 (docs).

## Open Questions (answered)

- **Format compatibility promise?** In code today: `classify_session_version` **refuses** a
  newer-than-supported file (`TooNew` → load aborts, `persist.cpp:526-530`) and **best-effort
  migrates** older/equal files (per-field defaults + `migrate_param_value`/`legacy_vop_name`).
  So the de-facto promise is "backward-migrate, forward-refuse." Recommend stating it
  explicitly in release docs and covering it with the version-guard test (which exists for
  the classifier but not the full load).
- **Which states need golden round-trip tests?** Currently **none** are covered end-to-end
  (P1-03). Recommend the representative set from the scaffold: blank sketch, a bundled
  example, a package-backed graph, and a plugin-using project — each `to_json → from_json`
  with structural + (ideally) rendered-output assertions.
- **How should users choose autosave / crash-snapshot / last manual save?** Today there is
  **no three-way picker**: the crash warm-snapshot is diagnostic-only (never a restore
  source), and recovery is a binary "Recover / Discard" modal over the autosave slot that
  never writes disk without an explicit Save. This is a deliberate design (consent upheld),
  not a defect — but if a three-way choice is desired it does not exist and would be new work.

## Follow-Up Plans

- **P1-01 fix PR (candidate RC blocker):** close + null `track_win`/`fx_win`/`clap_win`
  before the restore-path track teardown (mirror `input_clipgrid.cpp:144-149`), with an undo-
  with-plugin-window-open regression test.
- **P1-02 fix PR (candidate RC blocker):** retain an orphan `params`/`file_params`/`pinned`
  payload on missing-op `VisualNode`s and round-trip it verbatim, with a degraded-project
  save/load test.
- **P1-03:** a golden save/load round-trip test (blank / bundled example / package-backed).
- **P2-01:** add the genuine document-mutating MCP methods to `cli/edit_methods.cpp`.
- **P2-02:** a headless test that runs `EditGateway` with `VIVID_UNDO_AUDIT` on, so the guard
  actually gates regressions (would have caught P2-01).
- Cross-ref: P1-01 and the recovery flow both depend on the crash-attribution → quarantine
  pipeline that Phase 2's P0-01 (PR #190) hardens for plugins; Phase 6 should confirm the
  highest-risk persistence paths (round-trip, degraded save) get CI coverage.
