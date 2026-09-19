# Continuous Creative Workflow — Gate 1: the Review experience

Status: in progress — implementation plan for the first proof gate

Date: 2026-09-18

Implements the first proof gate of the
[continuous creative workflow](../product/continuous-creative-workflow.md) against
[ADR-0061](../decisions/ADR-0061-creative-work-persists-across-refinement-rounds.md),
[ADR-0062](../decisions/ADR-0062-candidates-are-isolated-and-promoted-explicitly.md), and
[ADR-0064](../decisions/ADR-0064-review-is-a-workspace-alongside-creation.md). ADR-0063 (runner +
worker) is gate 3; nothing here schedules or executes background work.

## The gate

> Prototype one brief, a baseline, two rendered alternatives, a passage comment, and a preference
> decision. Use real media. Target: the creator can understand and steer the change within two
> minutes without reading an agent transcript. Measure this, do not assume it.

Decisions taken for this gate (confirmed 2026-09-18):

- **Proof project:** `examples/song-sketch` — a 4-scene song (Am–F–C–G, 96 BPM) with reactive
  visuals and a project-local C++ operator, so a version snapshot has to carry project-local code
  from the first day.
- **Promotion scope:** "Use this version" performs real promotion through the edit gateway when
  the candidate differs from the preferred version in `project.json` only. Candidates that change
  co-located assets or project-local code are auditioned and kept here; their promotion is gate 2.
- **Player:** review media plays through a platform AVFoundation player, not the visual graph and
  not the `core-visuals` movie decoder (a package the shell cannot include). Audio goes to the
  default output device, so the engine is untouched; the audio graph and session never see review
  playback.
- **Records location:** `<project>/work/` — it holds briefs and runs as well as reviews.

## Trunk facts this builds on

- The shell is one composed frame (`app/frame.cpp`): session grid + visuals graph + detail dock,
  with `FocusContext` as the single source of truth for the dock. Review is a sibling composition
  selected by a workspace switch in the transport bar, not another dock view.
- `EditGateway::revision()` is a monotonically bumped per-process counter; undo/redo restores the
  canonical-document projection wholesale (`persist_undo.h`) with a tiered audio rebuild. Promotion
  reuses that restore path.
- A project folder is `project.json` plus co-located assets (`app/project_paths.h`); a project-local
  package compiles into the folder on load (`app/project_io.cpp`). Build outputs (`*.dylib`) are
  git-ignored artifacts, not content.
- The module-layering ratchet (`tools/check_module_layering.py`) only allows downward includes; the
  records layer is a new low-rank module so `app/` and `cli/` may both depend on it.

## Work records — `<project>/work/`

Proposed format; version 1. Records are plain JSON the app reads and writes and a script or agent
can read without the app. Append-only files are JSON Lines. Every record carries `schema: 1`.

| Path | Record | Notes |
|---|---|---|
| `brief.json` | current brief | `rev`, `direction` (user wording), `protections[]` (`hard: true`, `scope`), `preferences[]`, `interpretation` (structured, optional). |
| `brief.log.jsonl` | one line per brief revision | Superseded briefs are never deleted; queued work references a `brief_rev`. |
| `versions/<id>/version.json` | immutable version | `id`, `parent`, `created`, `label`, `source` (`manual` / `candidate` / `promotion`), `brief_rev`, `document_sha256`, `dependencies` (assets, package sources, plugin identities with `state_present`). |
| `versions/<id>/project.json` + assets | the snapshot | A complete copy of the project folder minus `work/` and build outputs. Complete copies first; dedup is later. |
| `candidates/<id>.json` | candidate | `version`, `purpose`, `brief_rev`, `baseline` (version id), `provenance` (`runner`, `run`, `summary`), `evidence` (`media[]` with `recipe`, `technical[]`, `intent[]`), `status` (`proposed` / `kept` / `promoted` / `dismissed` / `superseded`). |
| `reviews/<id>.json` | review item | `question`, `baseline`, `candidates[]`, `passage` (`start_bar`, `end_bar`), `status` (`open` / `resolved`), `resolution` (`choice`, `candidate`, `at`, `feedback`). |
| `feedback.jsonl` | comments and decisions | `id`, `at`, `review`, `version`, `candidate?`, `passage?` (`bar` or `sec`), `object?`, `text` (original wording), `kind` (`comment` / `decision`), `interpretation?`, `dedup_key`. |
| `events.jsonl` | what happened | `promoted`, `promotion_undone`, `kept`, `dismissed`, `review_opened`, `review_visited`. Drives "since your last review". |
| `media/<candidate>/…` | rendered evidence | Referenced from candidate evidence with its playback recipe (`scene`, `start_bar`, `bars`, `fps`, `warmup_sec`). |

`project.json` gains a top-level `preferred_version` (a version id). It is part of the canonical
document projection, so it saves, loads, and undoes with the document (ADR-0062 §1, as reconciled).

Every version's `dependencies` entry records a path and a SHA-256. Loading a version compares them
and reports what is missing or changed; it never substitutes content (ADR-0062 §2).

## PRs

### PR 1 — work records + preferred version
- New module `app/src/work/` (layer rank 10, beside `platform`): record types, load/save, append
  helpers, `snapshot_version()` (copy the folder minus `work/` and build outputs, hash dependencies),
  `check_dependencies()`.
- `preferred_version` in `session_to_json` / `session_from_json`; kept by the canonical projection.
- Headless tests: round-trip every record; append-only files survive a partial line; snapshot
  excludes `work/` and `*.dylib`; changed asset detected; `preferred_version` survives projection.

### PR 2 — review player
- `platform/review_player.{h,mm}` + stub: one `AVPlayer` per clip, frames via
  `AVPlayerItemVideoOutput` → wgpu texture → `Renderer2D` textured quad; `play_synchronized(a, b, at)`
  via `setRate:time:atHostTime:`; `seek(sec)`; `set_audition_gain()` (volume only, labeled).
- Test: headless stub round-trip of the state machine (play/pause/seek/sync request bookkeeping).

### PR 3 — Review workspace
- `Window::workspace` (`Create` / `Review`), persisted in window state; switch in the transport bar;
  a pending-review count on the switch, never an automatic context change.
- Review screen (ADR-0064 §2): preferred version + play, "since your last review" from
  `events.jsonl`, decision queue from open reviews, work status chip (honest: "no runner").
- Review item (ADR-0064 §3–§5): the question; baseline / A / B synchronized playback; jump to bar;
  passage comment (bar or second, stored with original wording); Use / Keep / Revise / Dismiss;
  "neither" through Dismiss-all. Audio and visual controls labeled (§6).
- Promotion: `EditGateway` command that restores the candidate's `project.json` through the undo
  restore path and sets `preferred_version`; refused when the candidate's dependencies differ from
  the current folder (gate 2) or when `revision()` moved since the review was opened (stale).
- Every action writes `work/` records and works with no control-server client connected.

### PR 4 — real media + the measurement
- `mcp/gate1/publish_review.py`: loads a project folder, saves + snapshots the baseline, records the
  brief, renders the baseline excerpt (`export_av`), renders two directions on **isolated copies**
  (`load_project` the copy → edits → `save_project` → `export_av` → `work_snapshot_version … into=<original>`),
  writes candidate records with provenance + evidence (export result, measured RMS for the level match),
  reloads the original, publishes one review item. Nothing in `work/` is hand-written.
- Control server + MCP: `work_status`, `work_snapshot_version` (the two things only the app can do
  well; the full tool families are gate 2). `save_project` over MCP now clears the dirty flag like
  the GUI save (it never did).
- Leaving Review no longer creates a `work/` folder in a project that never had records.
- `docs/roadmap/gate1-measurement.md`: the protocol — the creator returns cold, two minutes, no
  transcript; eight observations, the ADR-0064 probes (no agent, undo/redo, feedback on an older
  preview, neither, back to Create, save prompt); the result goes into ADR-0064's acceptance section.

## Out of scope for gate 1
- Any runner, worker, job, lease, or background execution (gate 3).
- MCP tool families for work/version/review (gate 2 proves persistence through them).
- Asset- or package-carrying promotion; automatic merge; storage dedup.
- Phone/browser review; remote execution.
