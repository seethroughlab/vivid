# Phase 5: Agent And MCP Experience

Status: done (audited 2026-08-01)

## Verdict

**PASS with follow-ups — no P0/P1.** This is the product's headline surface (ADR-0040:
"MCP-native creative coding is the public promise"), and it holds up. An agent can **see, explain,
and modify the same creative objects the UI exposes**, in product vocabulary. A full
inspect → explain → edit → verify → undo scenario ran clean on the `neon` project
(`evidence/phase-05/agent-scenario-transcript.md`): `inspect_session_overview` returns structured
counts **plus** a human summary ("3 tracks, 5 scenes at 124 BPM, playing; visuals have 9 op nodes");
`explain_signal_flow` returns plain language ("transport.beat_pulse drives node:4.size amount=0.18
range=[0.22,0.4]"); `map_audio_to_visual_param` reports the resolved effect ("master level drives
**Instancer**.spread") using the node's op name; and **the agent edit entered the same undo history
as a UI edit** — `undo` reverted it with label "Connect Mapping". Failure paths are structured and
honest (`not_found "no visual node with node_id 999"`, `bad_arg "no vivid-package.json in …"`,
`unknown_method`). The ~204-tool surface is product-vocabulary-aligned with **no dev/debug/internal
tools leaking**, and the one experimental cluster (Gemini music-eval) **fails closed** with no fake
results.

Findings are **2×P3**, both docs/vocab: the surface has **no documented release-status tiering** (the
Open Question "which methods are the first-release promise?" is unanswered, and the experimental
music-eval cluster isn't labeled as such — this also resolves Phase-1 F5 on the MCP side); and one
vocabulary straggler (`inspect_bindings` vs the "mapping" convention everywhere else). Neither blocks
release.

## Purpose

Verify that agent-facing workflows are inspectable, useful, and aligned with the public promise of
MCP-native creative coding.

## User Task

Ask an agent to inspect a project, explain the current audiovisual state, make a constrained edit,
and report what changed.

## Hypothesis

If the agent experience is credible, users can trust automation because it can see, explain, and
modify the same creative objects the UI exposes.

## Pressure Test

Run representative MCP/control-server scenarios for introspection, edit operations, audio analysis,
visual analysis, project recovery, mappings, and package interactions.

## Scope

- MCP/control-server methods exposed as release-facing or docs-facing capabilities.
- Agent-readable project, graph, audio, visual, mapping, package, and diagnostic state.
- Agent edit commands that change creative state.
- Error reporting, undoability, and explainability of agent-driven edits.

Out of scope: general agent personality, remote hosting, or unsupported private commands unless
they leak into public docs or examples.

## Audit Procedure

1. Inventory public or semi-public control methods and group them by inspect, edit, analyze,
   recover, package, and internal.
2. Run an agent-style scenario: inspect project, explain current state, make a constrained edit,
   verify result, undo or revert if supported, and summarize changes.
3. Compare method names, response fields, and error messages with product vocabulary.
4. Test failure paths: invalid object id, unsupported edit, bad package/operator, missing project,
   and stale state.
5. Mark methods that should be hidden, renamed, documented, or explicitly considered development
   surface for first release.

## Evidence To Collect

- Control method inventory with release status.
- Transcript of at least one successful agent edit scenario.
- Transcript of at least three failure scenarios.
- Vocabulary mismatch list across UI, docs, and command responses.

## Deliverables

- MCP readiness matrix: method group, user value, risk, release status, and docs status.
- Agent workflow findings with reproduction commands or transcripts.
- Public/private control-surface recommendation.

## Acceptance Criteria

- Agent-readable state uses the same vocabulary as the UI and docs.
- Edit commands have deterministic, inspectable effects.
- Errors are structured enough for an agent to explain and recover.
- Agent changes are undoable or clearly scoped.
- The control surface does not expose release-unsafe commands as polished public affordances.

## Failure Modes

- The agent can mutate state that the UI cannot explain.
- The agent reports success after a partial or failed edit.
- Control methods leak internal terms into user-facing output.
- MCP examples depend on unreleased local-only setup.

## Evidence Log

Method: enumerated the MCP bridge tools (`mcp/vivid_mcp.py`, 204 `@mcp.tool`s) + `mcp/README.md`;
drove the running build (commit `a85f5966`, post-#211) with `neon` loaded through a full agent
scenario + failure paths (`evidence/phase-05/agent-scenario-transcript.md`); and cross-checked names,
response fields, and errors against the glossary. Paths relative to repo root.

### A. MCP readiness matrix (by group)

| Group | Representative tools | User value | Risk | Release status | Docs |
|---|---|---|---|---|---|
| **Inspect** | `inspect_session_overview`, `inspect_track/scene`, `get_session/graph/health`, `list_tracks/params/operators`, `get_project_status` | high — the "see" half of the promise | low | **release** | in README |
| **Explain** | `explain_scene`, `explain_signal_flow`, `explain_mapping`, `inspect_bindings`, `suggest_mappings` | high — plain-language reasoning | low | **release** | in README |
| **Edit** | `add_node`, `connect_nodes`, `add_track`, `set_clip`, `connect_mapping(_by_intent)`, `map_audio_to_visual_param`, `set_*_param`, `launch_scene/clip`, `set_bpm` | high — the "modify" half | med (mutates state; **undoable**) | **release** | in README |
| **Analyze / perceive** | `analyze_frame/audio/spectrum`, `capture_frame/audio`, `compare_frames`, `run_quality_check`, `get_perf` | high — the verify loop | low | **release** | in README |
| **Recover / project** | `new/save/load_project`, `validate_project`, `diff_project`, `list_quarantine`, `unquarantine`, `undo/redo` | high — safety + repair | low | **release** | in README |
| **Package / authoring** | `install_operator_package`, `validate_operator_package`, `scaffold_*`, `reload_project_files`, `fork_shader`, `build_operator_package` | med — creative-coding | med (compiles code) | **release** | in tutorials |
| **Video export** | `export_video`, `start/stop_video_export`, `video_export_status` | med | low | **release** (recorder shrink caveat, code Ph3 P3-04) | thin |
| **Music-eval (Gemini)** | `configure_music_eval_backend`, `evaluate_audio_musically`, `music_eval_status/result` | med — AI critique | med (external API key + backend) | **EXPERIMENTAL** (fails closed) | key-gated; **not labeled experimental** (→ F1) |

No `debug`/`internal`/`_raw`/dev-only tools are exposed. The only external-dependency cluster is
music-eval, and it **fails closed** (no fake verdict without a key — `configure_music_eval_backend`
docstring), which is the right safety posture.

### B. Agent scenario transcript (success)

Full transcript in `evidence/phase-05/agent-scenario-transcript.md`. Highlights:

- **Inspect** → `inspect_session_overview`: `{counts:{tracks:3, scenes:5, mappings:1,
  visual_nodes:9}, summary:"3 tracks, 5 scenes at 124 BPM, playing; …"}` — structured **and**
  human-readable.
- **Explain** → `explain_signal_flow`: "transport.beat_pulse drives node:4.size amount=0.18
  range=[0.22,0.4]" — resolves the mapping to plain language.
- **Edit** → `map_audio_to_visual_param{master.level → node 4.spread}`: "master level drives
  **Instancer**.spread" — deterministic + inspectable, names the op not just the id.
- **Verify** → `get_mappings`: 1 → 2.
- **Undo** → `undo`: `{did:true, redo_label:"Connect Mapping"}`; `get_mappings`: 2 → 1. **The agent
  edit is in the same undo history as a UI edit** (answers Open Q2).

### C. Failure paths (structured + honest)

| Attempt | Response |
|---|---|
| map to `node_id:999` | `not_found` "no visual node with node_id 999" |
| `install_operator_package /no/such/pkg` | `bad_arg` "no vivid-package.json in /no/such/pkg" |
| unknown method `frobnicate` | `unknown_method` "unknown method: frobnicate" |
| async CLAP `add_track` | `ok` + `loading:true` — **honest** (queued, not loaded); poll `plugin_load_status` |
| `connect_nodes{node:4 ← node:4}` (self-loop) | `ok:true` — **permissively accepted** (cycle-safe topo; see note) |

Every failure returns a structured `{ok:false, code, error}` an agent can branch on, and the async
path reports "loading" rather than a false success. The one soft spot is that the graph accepts a
self/cycle connection without a warning — harmless (the executor is cycle-safe) and possibly valid
for feedback effects, but a non-feedback node feeding itself could warrant a warning. Noted, not
filed.

### D. Vocabulary

Method names + response fields track the glossary closely: session / track / scene / clip / mapping
/ operator / graph / preview / transport, and human summaries name the **op** ("Instancer.spread"),
not raw ids. Two small notes:

- **`inspect_bindings`** is the lone "bindings" tool amid a "mapping" convention (`connect_mapping`,
  `get_mappings`, `explain_mapping`, `list_mapping_*`, `suggest_mappings`, `disconnect_mapping`) —
  a straggler (ties Phase-1 F2 bridge/mapping/binding). (→ F2)
- Technical id grammars leak into some fields (`node:4.size`, `track_0.level`, `param:0:1:3`) — but
  they're *paired with* human summaries and are the PRD's own binding grammar, so they read as
  precision, not a vocabulary leak.

### E. Findings

#### F1 (P3): The MCP surface has no documented release-status tiering; music-eval isn't marked experimental

- Surface: `mcp/README.md` + the 204 `@mcp.tool`s in `mcp/vivid_mcp.py`.
- Impact: the README groups tools by function but never says **which are the first-release promise vs
  experimental**, so an agent or user can't tell stable tools from ones with external/unfinished
  dependencies. The clearest case is the **Gemini music-eval** cluster — it's honestly documented as
  key-gated and fail-closed, but not labeled *experimental*, and its GUI counterpart is a top-level
  "Eval" menu (this **resolves Phase-1 F5** on the MCP side). Touches the acceptance criterion "the
  control surface does not expose release-unsafe commands as polished public affordances."
- Evidence: `mcp/README.md` (no status/tier headings); `configure_music_eval_backend` /
  `evaluate_audio_musically` docstrings (key-gated, fail-closed, but no "experimental" marker).
- Smallest acceptable fix: add a release-status note to `mcp/README.md` — mark the core
  inspect/explain/edit/analyze/project loop as the first-release promise and the music-eval cluster
  **experimental (requires a Gemini API key)**; align the GUI "Eval" menu label (Phase-1 F5).
  Owner/status: Unassigned | P3.

#### F2 (P3): `inspect_bindings` is a vocabulary straggler vs the "mapping" convention

- Surface: `mcp/vivid_mcp.py` (`inspect_bindings`) vs every other bridge tool ("mapping").
- Impact: the bridge is otherwise consistently "mapping" across ~8 tools; the lone "bindings" name
  makes an agent guess whether bindings and mappings are the same thing (they are). Small
  mental-model friction; ties to Phase-1 F2 (bridge/mapping/binding vocabulary).
- Evidence: §D; tool list.
- Smallest acceptable fix: rename to `inspect_mappings` (keep `inspect_bindings` as a deprecated
  alias), or pick the single bridge noun product-wide alongside the Phase-1 F2 decision.
  Owner/status: Unassigned | P3.

## Open Questions

*(answered)*

- **Which MCP methods are part of the first-release product promise?** The
  inspect / explain / edit / analyze / transport / mapping / project core — the ADR-0040 creative
  loop (inspect → author → map → save → reload → verify). The **music-eval (Gemini)** cluster is
  **experimental** (external API key + backend). Package/authoring + video-export are release but
  secondary. This tiering should be **documented** (F1).
- **Should agent edits always enter the same undo history as UI edits?** They **already do** —
  verified live (the agent's `map_audio_to_visual_param` was reverted by `undo` with label "Connect
  Mapping"). This is the right design; keep it. Agent and human edits share one history via the
  EditGateway (`process_pending` → `note_edit`).
- **What is the public wording for commands that are useful but still experimental?** For music-eval:
  label it **"Experimental — requires a Google Gemini API key; fails closed with no result if
  unset."** The docstrings already carry the key-gated/fail-closed facts; F1 adds the explicit
  "experimental" tier so it isn't read as a finished headline feature.

## Follow-Up Plans

- **F1** (MCP release-status tiering + mark music-eval experimental) and the **Phase-1 F5** GUI "Eval"
  label are the same decision — do them together as a small docs + label change.
- **F2** (`inspect_bindings` → `inspect_mappings`) should ride along with the Phase-1 F2 bridge/mapping
  vocabulary decision so the noun is settled once, product-wide.
- **Cross-refs:** the strong result here rests on work already merged this program — agent edits are
  undoable because of the EditGateway (and the Ph2 F1 undo-on-load fix), errors are structured/clear
  partly from the Ph2 F5 message work, and `list_quarantine`/`unquarantine` exist over MCP (the GUI
  gap is Phase-4 F2). The MCP↔UI vocabulary alignment discharges much of the Phase-1 "agent uses the
  same vocabulary" promise.
