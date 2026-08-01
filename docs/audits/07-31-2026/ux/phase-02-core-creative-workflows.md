# Phase 2: Core Creative Workflows

Status: done (audited 2026-08-01)

## Verdict

**FAIL — 1×P1 (candidate-P0).** The core creative loop is otherwise healthy: I built a complete
audiovisual sketch from a blank project (instrument track → 4-note clip → `Plasma → Output` visual
→ an audio→visual mapping → playback), and it rendered and played correctly
(`evidence/phase-02/01-blank-sketch-mapped-playback.png`). **Save/reopen fidelity is excellent** —
verified twice that a saved project restores its tracks, clips, visual graph, and mappings exactly,
including edits to an example. Transport/playback and the rendered output stay synchronized. The
map-by-intent affordance is a highlight (`map_audio_to_visual_param` returns `"Atoms level drives
Plasma.warp"`), and failure messages are mostly clear and actionable.

The blocker is **undo**: the undo system is not integrated with project load, so **a single ⌘Z
after opening a project destroys the opened project.** Two clean repros — (a) with prior edits in
the session, opening a second project and pressing undo replays the *first* project's snapshot over
the *second*, replacing its graph, tracks, and mappings wholesale
(`evidence/phase-02/02-undo-after-open-corrupts-project.png` — the title reads "Vivid — neon" but
the content is the blank sketch); (b) even on a cold app, opening a project, changing one param, and
pressing undo collapses the visual graph to the default empty canvas. This directly fails the
acceptance criterion "the user can recover from ordinary mistakes with undo" and the failure mode
"undo breaks the user's sense of control." It is recoverable only by re-opening the file (in-session
state loss, not on-disk), which is why it is filed P1 rather than P0 — but the trigger (⌘Z after
Open) is a universal reflex and the blast radius is the whole project, so it is a P0 candidate for
the user's blocking call.

Findings: **1×P1 + 2×P2 + 2×P3.** Beyond undo: native/CLAP instruments advertised by
`list_instruments` cannot be added via `add_track` (VST3-only matcher), and "New Project" is an
asymmetric partial reset (keeps track lanes, wipes everything else).

## Purpose

Verify that the main creative loop works end to end: start a project, make sound, make visuals,
connect them, iterate, save, close, reopen, and continue.

## User Task

Complete a small audiovisual sketch using only release-candidate affordances and bundled examples.

## Hypothesis

If the core loop is healthy, a motivated first user can reach a satisfying result without developer
intervention or undocumented setup.

## Pressure Test

Run scripted walkthroughs for new project creation, example loading, audio graph editing, visual
graph editing, mapping, playback, save/reopen, and export or recording where applicable.

## Scope

- New project, open project, save, save as, autosave-visible behavior, and reopen.
- Bundled examples and demos that are candidates for release.
- Session view, audio graph, visual graph, bridge/mapping controls, transport, preview, and export
  or recording paths.
- Undo/redo and recovery for ordinary creative mistakes.

Out of scope: exhaustive plugin compatibility, performance profiling, or internal test coverage
except where a workflow cannot be verified manually.

## Audit Procedure

Run three walkthroughs and keep notes at each decision point.

1. Blank sketch: start from the first-run state, create a sound source, create a visual response,
   map audio to visuals, play, stop, save, reopen, and confirm the result.
2. Example remix: open a release candidate example, identify the audio source, identify the visual
   graph, make one audible edit, make one visible edit, undo each, redo each, save a copy, and
   reopen it.
3. Failure-aware edit: intentionally attempt one unavailable, invalid, or mistaken operation and
   confirm the UI explains how to recover.

For each walkthrough, record the happy path, the first point of confusion, and the first point where
a user might need outside help.

## Evidence To Collect

- Task transcript with time-to-first-sound, time-to-first-visual, and time-to-first-mapping.
- Screenshots of start, mid-edit, mapped playback, saved/reopened, and recovery states.
- List of commands or controls that were required but not discoverable.
- Save/reopen diff notes: what changed, what stayed stable, and what was ambiguous.

## Deliverables

- Workflow scorecard for blank sketch, example remix, and failure-aware edit.
- Release-blocking workflow bugs with reproduction steps.
- Tutorial or docs gaps discovered by the walkthroughs.

## Acceptance Criteria

- Every primary command has a discoverable UI path.
- Playback, transport, and visible state stay synchronized.
- Save/reopen preserves user-created audio, visual, and mapping state.
- The user can recover from ordinary mistakes with undo or explicit reset paths.
- The workflow produces evidence suitable for release notes or a tutorial.

## Failure Modes

- A required workflow only works from tests, CLI, or developer memory.
- The app reaches an ambiguous state with no visible next action.
- Saving or reopening changes the creative result.
- Undo, selection, or focus state breaks the user's sense of control.

## Evidence Log

Method: drove the running release build (commit `45ba98a9`, post-#204) through three scripted
walkthroughs over the control-server / MCP surface — a first-class release surface per ADR-0040 —
cross-checking GUI affordances and error surfacing against `app/src`. Instrument used: **Atoms**
(an installed VST3) after native instruments proved unaddable (→ F2). Full-window screenshots under
`evidence/phase-02/`. Timings are reported as **step counts** (the meaningful UX effort for an
agent-driven flow), not automation wall-clock. Paths relative to repo root.

### A. Walkthrough 1 — blank sketch (build → map → play → save → reopen)

Sequence from a new project: `set_bpm 124` → `add_track` (instrument) → `set_clip` (4-note riff,
len 4) → `add_node Plasma` → `connect_nodes Plasma→Output` → `map_audio_to_visual_param` → launch +
play. Result rendered and played (evidence 01: Session shows track "Atoms" with a clip thumbnail;
audio graph `Notes→Atoms→Output`; visual graph `Plasma→Output` with a live plasma; the floating
Output renders full-frame).

- **Time-to-first-\*** (steps, each a single command/affordance): first sound-capable = 2 steps
  (add_track + set_clip); first visual = 2 steps (add_node + connect); first mapping = 1 step. The
  loop is short and each step succeeds atomically.
- **Highlight:** `map_audio_to_visual_param` accepts intent (track name + characteristic + node +
  param) and returns a plain-language summary — `"Atoms level drives Plasma.warp"` — with canonical
  `src`/`dst` for debugging. This is the strongest workflow affordance in the phase.
- **Save/reopen (the key persistence check):** `save_project` → `new_project` → `load_project`
  restored **track (Atoms), clip (4 notes, len 4), visual graph (Output←Plasma), and mapping
  (`track_0.level → node:1.warp`, amt/lo/hi intact)** exactly. **PASS.**
- **Friction:** the obvious beginner path — `list_instruments` → `add_track {instrument:"TestTone"}`
  — **fails** (→ F2). Had to fall back to an installed VST3.

### B. Walkthrough 2 — example remix (edit → undo/redo → save-copy → reopen)

Opened `examples/demos/projects/neon` (9 visual nodes, 1 mapping, tracks Cassette Drums/arp/bass).

- **Visible edit + save-copy + reopen:** `set_node_param node:4 size 0.22→0.6` → `save_project`
  (copy) → `load_project` (copy) restored **9 nodes, node4.size=0.6, mapping preserved**. An edit to
  an example survives save/reopen. **PASS.**
- **Audible edit undo:** `set_clip` on track 0 (56 notes → 1) then `undo` correctly restored **56
  notes** — session/clip undo reverts correctly within a settled session.
- **Undo after project-load — BROKEN (→ F1):**
  - *Repro A (cross-load contamination):* after Walkthrough 1's edits, opening neon and pressing
    `undo` **once** reported `undo_label: 'Add Track'` (a W1 action) and collapsed neon to **2 nodes
    (Output, Plasma), 0 mappings, track "Atoms"** — i.e. Walkthrough 1's entire project replayed over
    neon (evidence 02: title "Vivid — neon", content = the blank sketch). Re-opening the file
    restored neon (9 nodes, 1 mapping) — recoverable, in-session only.
  - *Repro B (missing baseline, cold app):* fresh app, `load_project neon`, `set_node_param size
    0.6`, then `undo` (with 1.5 s settle between each step) collapsed the visual graph to the
    **default 1-node canvas** (node 4 gone); `redo` restored it. So even with a clean stack the first
    undo after open reverts the visual graph to default rather than the loaded project.
  - Both go through the same `EditGateway::restore` path as GUI ⌘Z. Root cause: `load_project` /
    `file_actions::load_path` neither clears a stale undo stack nor seeds a baseline snapshot of the
    loaded project (contrast `new_project`, which calls `edit_gateway->mark_saved()`; load does not
    reset undo). See F1.

### C. Walkthrough 3 — failure-aware edits (does the app explain recovery?)

Agent-facing errors (MCP), each an intentional mistake:

| Attempt | Response | Recovery quality |
|---|---|---|
| `add_node` unknown op | `bad_arg` + **lists all valid ops** | excellent |
| `launch_clip` out of range | `out_of_range` "track 50 out of range **[0,3)**" | excellent (states range) |
| `set_bpm 99999` | `bad_arg` "bpm out of range **(0, 1000]**" | excellent (states bound) |
| `set_node_param` bad param | `not_found` "no param 'bogus' on that node" | good |
| `map` to bad node_id | `not_found` "no visual node with node_id 999" | good |
| `connect_nodes` to missing node | `not_found` "no node with that input_id" | good |
| `add_track` unmatched instrument | `not_found` "no instrument matched 'Nope' (or kMaxTracks reached)" | weak — conflates two failure modes, no next-step hint (→ F2/F5) |
| `load_project` bad path | `io_error` "read failed" | weak — no path, no reason (→ F5) |

GUI failure-surfacing infrastructure exists and was validated in the code audit (Phase 3): node
error badges, `get_ops.broken_ops`, `get_health.missing_ops`, Error-level toasts
(`ui/toasts.h`), and startup-scan errors routed to `app.log` (#197/#200). Not independently
re-triggered here — a hand-authored broken project used the wrong top-level schema key and loaded as
an empty project with `ok:true` and no warning, which is itself a gap (→ F4). **PASS with polish.**

### D. Param-addressing note (minor)

`list_params` is audio-only (`{track, device}`) and **silently ignores an unknown `node_id` arg**,
returning track-0 device-0 params — so `list_params {node_id:4}` on a visual node returned a
plugin's 2175 params, not the Instancer's. Visual params come from `get_graph`. The domain split is
fine, but the silent-default on an unrecognized arg is a robustness gap (folded into F5).

### E. Findings

#### F1 (P1 — candidate-P0): Undo after opening a project destroys the opened project

- Surface: `app/src/app/file_actions.cpp:55` (`load_path`/`new_project`) + the `load_project` /
  `load_session` control handlers vs. the undo manager (`app/src/app/undo_manager.*`,
  `edit_gateway.*`, ADR-0017).
- Impact: the undo system is not integrated with project load. A user opens a project and presses
  ⌘Z once — a universal reflex — and the opened project is silently replaced: either by the previous
  session's project (if edits preceded the open) or by the default empty canvas (cold app). Graph,
  tracks, and mappings are all affected. Recoverable only by re-opening the file (in-session state
  loss). Fails the acceptance criterion "recover from ordinary mistakes with undo" and the failure
  mode "undo breaks the user's sense of control."
- Evidence: Evidence Log §B, repros A & B; `evidence/phase-02/02-undo-after-open-corrupts-project.png`
  (title "Vivid — neon", content = the blank sketch). `file_actions::load_path` clears autosave +
  project path but never resets the undo history (grep: no `undo`/`clear_history` in the load path),
  whereas `new_project` calls `edit_gateway->mark_saved()`.
- Smallest acceptable fix: on project load (both `file_actions::load_path` and the
  `load_project`/`load_session` handlers), **clear the undo/redo history and seed a fresh baseline
  snapshot of the loaded project**, so a freshly-opened project starts with an empty, correctly-based
  undo stack. Mirrors the code-audit Phase 4 persistence/undo work. Owner/status: Unassigned | P1
  (candidate-P0 — user's blocking call).

#### F2 (P2): Native and CLAP instruments listed by `list_instruments` can't be added via `add_track`

- Surface: `app/src/audio/vst3_host.cpp:3843` (`load_instrument_spec`), reached from `add_track`.
- Impact: `list_instruments` advertises native instruments (TestTone, MovieAudio, Sampler) and CLAP
  instruments, but `load_instrument_spec` only matches **VST3** (`if (p.format != kFmtVST3)
  continue;`) or a `.vst3` path. So the obvious discover→create path — `list_instruments` →
  `add_track {instrument:"TestTone"}` — fails with "no instrument matched", and the *most
  beginner-friendly, no-plugin-install* instruments (the native ones) are unreachable via `add_track`.
  The alternatives (`set_track_clap_instrument`, `set_track_audio_instrument`) are not hinted by the
  error. This directly weakens the "make sound without developer setup" hypothesis.
- Evidence: Evidence Log §A friction; `add_track {instrument:"TestTone"|"Sampler"}` → `not_found`;
  `add_track {instrument:"Atoms"}` (VST3) → ok. Matcher at `vst3_host.cpp:3865`.
- Smallest acceptable fix: make `add_track`/`load_instrument_spec` resolve native and CLAP
  instruments by their `list_instruments` name; minimally, error with format-specific guidance that
  names the correct command. Owner/status: Unassigned | P2.

#### F3 (P2): "New Project" is an asymmetric partial reset (keeps track lanes + instruments)

- Surface: `app/src/app/file_actions.cpp:55` (`new_project`, File▸New / ⌘N) and the MCP `new_project`
  handler (`control_handlers_project.cpp:402`) — identical behavior (no parity gap).
- Impact: "New" clears all clips, fully resets the visual graph + mappings, and drops the project
  identity, but **keeps every track lane and its loaded instrument** (returns `tracks = nt`). So
  File▸New on a loaded song leaves the previous song's instrument tracks (with empty clips) behind —
  surprising for a "New" command, and asymmetric against the full visual reset. No visible indication;
  workaround is to remove each track manually.
- Evidence: `new_project` after a 1-track session returned `tracks: 1` and `list_tracks` still showed
  "Atoms", while the visual graph reset to just Output and mappings cleared; handler at
  `control_handlers_project.cpp:402-418` (loops `session_set_clip(...,nullptr,0)` but never removes
  tracks).
- Smallest acceptable fix: make New consistent — either fully reset (remove tracks too) or explicitly
  scope/label it ("clear content, keep instruments") so the retained lanes are intentional and
  visible. Owner/status: Unassigned | P2.

#### F4 (P3): `load_project` silently accepts a parseable-but-unrecognized project (empty, no warning)

- Surface: the `load_project` handler / persist load path.
- Impact: PRD §7 makes project text a first-class, hand-editable, diffable source of truth. A
  structural typo in `project.json` (e.g. a wrong top-level key) loads as an **empty project** with
  `ok:true` and no warning — the user sees a blank session with no diagnostic. `validate_project`
  exists as a recovery path, but `load` itself does not warn.
- Evidence: a hand-authored file using `visual_graph` (real key is `graph`) loaded with `ok:true`,
  `op_nodes:0`, empty `get_graph`. Real schema top-level keys: `graph`, `tracks`, `scenes`, `master`,
  `pool`, …
- Smallest acceptable fix: on load, warn (toast/log) when a parsed project yields no `graph`/`tracks`,
  or auto-run `validate_project` and surface its warnings. Owner/status: Unassigned | P3.

#### F5 (P3): A few failure messages are weak / silently mis-default

- Surface: `load_project` bad-path (`io_error` "read failed"); `add_track` conflated failure
  (`"no instrument matched … (or kMaxTracks reached)"`); `list_params` silently ignoring `node_id`.
- Impact: most errors are excellent (they state valid ranges or list valid options), but these three
  leave the user guessing — the failed path/reason is omitted, two distinct failures are conflated,
  and an unrecognized arg returns wrong-but-plausible data instead of erroring.
- Smallest acceptable fix: include the path + reason in load errors; split the `add_track` failure
  modes; have `list_params` reject or warn on an unknown `node_id`. Owner/status: Unassigned | P3.

## Workflow Scorecard

| Walkthrough | Result | Notes |
|---|---|---|
| **Blank sketch** (build → map → play → save → reopen) | **PASS** | Loop is short and atomic; save/reopen fidelity clean; `map_audio_to_visual_param` is excellent. Friction: native-instrument `add_track` blocked (F2). |
| **Example remix** (edit → undo/redo → save-copy → reopen) | **FAIL** | Edit + save-copy + reopen clean; **undo after open corrupts the project (F1)**. Redo and in-session clip undo work. |
| **Failure-aware edit** | **PASS (w/ polish)** | Agent errors mostly clear/actionable; GUI surfaces exist (code Phase 3); weak load/`add_track` messages + silent malformed-load (F4/F5). |

Acceptance criteria: playback/transport/visible-state synchronized ✅; save/reopen preserves state
✅ (verified twice); recover from mistakes via undo ❌ (F1); evidence suitable for a tutorial ✅ (the
blank sketch is a clean minimal walkthrough); every primary command has a discoverable **UI** path —
**deferred to Phase 3/4** (this phase drove the agent surface; F2 and the Phase-1 F3 onboarding gap
are relevant inputs).

## Open Questions

*(answered)*

- **Is export/recording part of the first-release bar, or a documented scaffold?** Export Video is a
  shipped affordance (File▸Export Video + `export_video`/`start_video_export` MCP), backed by a real
  realtime AV recorder. Treat it as **in-bar**, with the known limitation that the recorder latches
  its dimensions and mishandles a mid-record output-resolution change (code-audit Phase 3 P3-04).
  Deep export UX belongs to Phase 6.
- **Which bundled example is the canonical new-user walkthrough?** For the guided path, the
  `mcp-native-first-project` tutorial (Phase 1). Among the demos, **`neon`** is a strong canonical
  populated example — it loads clean, exercises tracks/scenes/clips + a real visual graph + a
  mapping, and reads clearly. Recommend neon (or the tutorial output) as the canonical walkthrough.
- **What is the smallest satisfying audiovisual sketch for release validation?** Exactly the
  Walkthrough-1 sketch: **one instrument track + a 4-note clip + a `Plasma → Output` visual + one
  audio→visual mapping, playing.** It exercises the whole loop end-to-end and is small enough to be a
  release-notes/tutorial demo — once F1 (undo) and F2 (native instrument) are fixed so it can be
  built with a no-plugin-install instrument.

## Follow-Up Plans

- **F1 (undo-on-load) is the release-blocking fix** — own gated PR, clearing + re-baselining the undo
  history on load in both `file_actions::load_path` and the `load_project`/`load_session` handlers,
  with a save→edit→open→undo regression test. This is a code fix (audit-first: filed now, fixed
  after the user's blocking call), and pairs with the code-audit Phase 4 persistence/undo track.
- **F2/F3** — instrument-add coverage for native/CLAP, and a "New" reset decision (full vs.
  content-only), each its own PR.
- **F4/F5** — load-time validation warning + message polish (docs/robustness).
- **Cross-refs:** F2 compounds Phase 1 F3 (no-onboarding) — the beginner path is blocked at both the
  "what do I do" and the "add a sound" steps; feed both to Phase 6. GUI-path discoverability (the
  first acceptance criterion) is handed to Phase 3 (interface & IA) and Phase 4 (input). The clean
  blank-sketch (§A) is the concrete minimal sketch Phase 6 should ship as the first-run example.
