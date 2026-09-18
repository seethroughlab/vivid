# Continuous Creative Work: Discussion Plan

Status: draft for discussion — proposed direction, not an accepted ADR or implementation spec

Date: 2026-09-17

## Product premise

Vivid should support a creator with a full-time job who can give occasional feedback while songs
and visuals develop over time. The central loop is direction → bounded exploration → review →
refinement. Work persists across conversations and application restarts; each review should require
little reconstruction of context.

The goal is useful creative progress per minute of human attention. Continuous operation means
work can resume when appropriate; it does not require constant computation or constant changes.
Finishing and exporting remain possible milestones within a continuing project.

Confirmed in discussion: the first review surface is Vivid on the creator's Mac. Background work
may explore freely in candidates within the brief and protections; the creator chooses what becomes
current. These preferences are settled inputs to the proposed design, not yet accepted ADRs.

## A concrete day

1. Before work, the creator says: “Keep the drums and bass. Make the chorus feel more expansive,
   and let the visuals open up with it. Give me two directions.”
2. Vivid records that direction against the current project version. It distinguishes hard
   protections from preferences and shows how the request was interpreted.
3. An external agent explores candidate copies within the time, cost, and scope available. It can
   discard weak attempts, render excerpts, and check technical and musical/visual intent.
4. The creator returns to two synchronized audiovisual excerpts, a short account of what changed,
   and one useful question. The current preferred version remains available for comparison.
5. The creator chooses A and comments at bar 25: “Keep this opening, but the particles get too busy.”
6. The decision and comment are saved against that exact candidate and passage. The next round
   starts from the chosen version and revises the particle behavior within the existing direction.
7. If the creator does not return, the system preserves the results. It may continue independent
   authorized work, but does not repeatedly branch an unresolved choice into an ever-growing queue.

This is a proposed scenario, not a promise of current functionality.

## Proposed product rules

- Work and feedback outlive the agent conversation. A fresh agent can recover the brief, current
  version, unresolved choices, protections, and why previous candidates were kept or rejected.
- Exploration preserves a known preferred version. Choosing a version and merely previewing it
  are different actions.
- Human decisions and automated evaluation are recorded separately. A model's score cannot stand
  in for the creator's preference or silently establish a new creative direction.
- Review is optional until a meaningful choice is needed. Routine iterations do not ask for
  approval. Stated constraints bound what can proceed without further direction.
- Silence is neither acceptance nor rejection. A full review queue creates backpressure.
- When the creator returns to edit, their session remains responsive and their changes are protected.
- All surfaces use the same durable work records. Chat, native UI, and a possible remote review
  surface must not maintain competing copies of feedback or project decisions.

## Interface proposal

Introduce an explicit **Review** workspace alongside **Create**. Create retains the session and
visual graph. Review emphasizes playback, comparisons, feedback, and resuming work. Preserve the
last selected workspace; test whether returning to pending reviews should offer a shortcut rather
than forcibly changing the creator's context.

The first review screen should contain:

- Current preferred version, immediately playable.
- A short “since your last review” summary.
- A small queue of decisions, each with one question, candidate media, and the baseline.
- A compact work indicator: working, ready for review, waiting, paused, or blocked, with a reason.
- A visible control to pause or resume background work.

A review item supports synchronized A/B playback, a clearly labeled listening-level match option,
and jumping to the relevant bars or seconds. Matching playback levels must not alter the authored
mix. Keep access to unadjusted playback when judging loudness or dynamics. A short excerpt needs
access to surrounding context; a good eight-bar loop is not proof of a good song.

Actions should distinguish **Use this version**, **Keep as an alternative**, **Revise**, and
**Dismiss**. A comment alone does not necessarily choose a version. Feedback can refer to audio,
visuals, their relationship, a project object, or a passage. Cross-domain review uses shared
workspace chrome with explicitly labeled audio/visual controls.

“Use A's music with B's visuals” is a useful eventual interaction, but is a new composition that
requires compatibility checks and a new preview. It is not a free merge of two independent files:
tempo, mappings, timing, and dependencies may differ.

### Persistent direction and protection

Show a concise project brief with desired character, current focus, preserved elements, and things
to avoid. Retain the user's wording alongside any structured interpretation.

Separate hard protections (“do not change these notes”) from preferences (“keep the feeling of this
groove”). Protection scope must be intelligible: preserving notes does not preserve perceived sound
if downstream effects, tempo, or mappings can still change. Report the covered objects and relevant
dependencies. Ambiguity need only block the affected work; other authorized work can continue.

Feedback is project-specific by default. Inferring a permanent, global taste profile from one
rejected candidate would overgeneralize the creator's intent.

## Proposed durable concepts

These are provisional discussion terms, listed in the glossary under "Background Refinement
Vocabulary (proposed)". Their relationship to the planned Take, Variation, and Variation Well
concepts is stated in ADR-0062: the well is the same audition/keep/choose pattern at cell scope,
stored in the document, and is where a promoted or kept clip-scoped candidate lands.

| Concept | Responsibility |
|---|---|
| Brief | Versioned creative direction, preferences, and protections. |
| Version | Immutable project snapshot with parentage and referenced assets/code/plugin state. |
| Candidate | A version proposed for a purpose, with its source brief, provenance, and evidence. |
| Work item | A bounded objective, scope, dependencies, budget, and stopping conditions. |
| Review item | A question about exact candidate versions and playable evidence. |
| Feedback | Original comment/choice, target version and passage, interpretation, and resolution. |
| Run | One execution attempt against a work item, recording inputs, progress, and outcome. |

The preferred version is an explicit reference stored in the project document, so it saves, loads,
and undoes with the document. The open editing session may contain newer unsaved changes; those must
not be confused with the preferred snapshot. A candidate can concern a single clip or a whole
project; a clip-scoped one becomes a Take in the cell's well when promoted or kept.

Record authored workflow metadata in portable, versioned project records. Keep media and opaque
plugin state as referenced assets, with integrity checks. Store machine-specific execution details,
credentials, and temporary caches separately. Durable run outcomes must survive even though live
process handles do not. This extends the older project-format sketch's authored/runtime distinction;
it does not make every transient status part of the creative document.

Candidate isolation must include mutable assets, shaders, and project-local code, not just a copy
of project.json. Full snapshot format, deduplication, retention, and asset packaging need a later
implementation design. Accepted versions and unresolved review evidence cannot be silently pruned.

## Architecture and ownership

| Component | Owns |
|---|---|
| Vivid project/work layer | Versions, briefs, work items, feedback, reviews, validation, and atomic promotion. |
| External agent runner | Scheduling, reasoning, exploration strategy, provider use, and reporting progress. |
| Vivid execution worker | Loading an isolated candidate, applying edits, rendering, and producing evidence. |
| MCP bridge | Discovering capabilities and exposing the same project/work operations to external clients. |
| Review UI | Playback and authoring decisions over those shared records. |

An external runner must actually be available for work to start. Writing a work item through MCP
does not itself launch or schedule an agent. Show “queued; no runner connected” distinctly from
“working.” A runner can change without losing creative history.

Recommend proving a separate local execution process before designing an in-process multi-session
engine. Another Window shares the same App/session today and would not isolate background edits.
Separate processes still require resource limits and validation of plugin, GPU, device, recovery,
asset-path, and port behavior. They do not automatically guarantee background compatibility.

Start with one active worker and one project. Playback in the foreground takes priority; pause or
throttle background rendering as necessary. Saving and reviewing results should work without a
connected agent. Closing a review window, quitting the worker, and putting the computer to sleep
have different consequences and must produce truthful status. Running while the machine sleeps
requires another execution host and is outside the proposed first local milestone.

## MCP contract direction

Add a small work/version/review layer above existing editing tools. Illustrative tool families:

| Family | Operations and guarantees |
|---|---|
| Work | Read/update brief; create/list/claim work; report progress; pause/cancel. |
| Versions | Snapshot; create isolated candidate; inspect changes; promote against an expected revision. |
| Review | Publish/list review items; attach artifacts; record feedback; resolve choices. |
| Execution | Submit render/evaluation job; read status/result; cancel; discover capabilities. |
| Updates | Read changes after a cursor, with stable event IDs and a resync path for expired cursors. |

Names and schemas are intentionally not frozen. Existing edits need explicit candidate targeting or
an unambiguous worker-bound session. Calls must never implicitly fall back to the foreground project.

Required behavior:

- Mutations carry expected revisions where stale writes matter. Promotion checks the current
  document and brief/protection revisions atomically. If the creator edited meanwhile, retain the
  candidate and report a conflict; first version should not attempt automatic whole-project merging.
- Retried submissions and feedback use deduplication keys so reconnects do not create duplicate
  work, media, or decisions. One runner claims a work item through a renewable lease; an expired
  claim is detected before another runner takes over.
- Long work returns stable IDs promptly. Completion links the exact input version to output media
  and evaluation evidence. Publishing a review requires complete, accessible artifacts.
- A disconnected runner becomes interrupted or unknown after its lease expires, not silently
  successful. Resume from durable checkpoints; restarting an interrupted render from the beginning
  is acceptable initially. Sample-level render resumption is not required.
- Cancellation stops future work and requests the current operation to stop safely. Already finished
  candidates remain inspectable. Project undo does not undo external execution or erase feedback.
- A new brief or protection invalidates dependent queued work. In-flight results retain their
  provenance and are marked superseded when appropriate; they cannot be promoted under stale rules.

Budget controls belong at the work/runner boundary: elapsed time, attempts, evaluation calls,
render resources, and provider spend where observable. Unknown cost is reported as unknown. Work
stops when its budget, review capacity, or useful exploration is exhausted. It must also be able to
say “no worthwhile improvement found” without manufacturing a review item.

## Evidence and review quality

Every preview identifies its version, source brief, rendered passage, render recipe, and dependencies.
For generative material, record available seeds and timing; preserve the actual rendered result even
when a plugin cannot reproduce it exactly. Feedback stays attached to the original version if later
edits change the passage's timing.

Use technical checks to reject broken output; use content/semantic checks to assess stated intent;
use human feedback to establish preference. Keep those results distinct, including evaluator
unavailability. Do not repeatedly optimize a single score and call it artistic progress.

Current exports render the current clip arming from beat zero. Reviewing a whole song or a section
transition needs an explicit playback recipe for scene launches/arrangement, start position, and
warm-up. Begin with a reproducible scene excerpt; prove passage and full-song rendering before
claiming unattended song development. This does not settle the separate timeline/Cue Path design.

## Existing decisions and implementation evidence

- [ADR-0006](../decisions/ADR-0006-agent-external-mcp.md) and
  [ADR-0008](../decisions/ADR-0008-agent-capability-surface.md): retain the external agent and allow
  contextual intent/feedback in Vivid. Clarify work scheduling and runner attachment.
- [ADR-0009](../decisions/ADR-0009-two-surface-bridge-and-native-reboot.md),
  [ADR-0013](../decisions/ADR-0013-focus-first-strict-zone-ui.md), and
  [ADR-0014](../decisions/ADR-0014-visual-graph-is-home.md): Review is an explicit alternate
  workspace; the authoring layout remains intact in Create. An ADR must qualify the graph-at-launch
  rule and explain shared review controls.
- [ADR-0017](../decisions/ADR-0017-every-edit-is-reversible.md): reuse canonical persistence and
  the edit gateway; persistent versions complement session undo. Promotion should be one reversible
  document action without rewinding the historical review record.
- [ADR-0018](../decisions/ADR-0018-a-bad-operator-must-not-cost-you-your-work.md): extend recovery
  to worker failure and interrupted work.
- [ADR-0026](../decisions/ADR-0026-evaluation-asserts-quality-against-intent.md): preserve explicit
  intent and honest evidence; do not equate evaluator success with human preference.
- [App architecture](../../app/ARCHITECTURE.md): one shared engine/document per process;
  main-thread editing. [Audio export](../../app/src/cli/control_handlers_audio_export.cpp) is
  synchronous; [AV export](../../app/src/cli/control_handlers_av_export.cpp) is asynchronous and
  already renders surface-free at a synthetic clock (the worker's render primitive), but both use
  the active App, and the process assumes a window-bound GPU context and a device-derived sample
  rate. Isolated durable execution needs device-free bring-up around them, not a new render loop.
- [MCP tools](../../mcp/vivid_mcp.py) supply editing, capture, comparison, and evaluation
  foundations. Clip writes already have a revision-check precedent in
  [audio handlers](../../app/src/cli/control_handlers_audio.cpp); project-wide guards are a new contract.

## Proposed sequence and proof gates

1. **Review experience.** Prototype one brief, a baseline, two rendered alternatives, a passage
   comment, and a preference decision. Use real media. Target: the creator can understand and steer
   the change within two minutes without reading an agent transcript. Measure this, do not assume it.
2. **Persistent creative loop.** Save versions, feedback, and work items; drive one round through
   existing tools on an explicitly separate working copy. Restart the agent and demonstrate it
   resumes with the same decisions and protections. This proves persistence, not unattended execution.
3. **Background execution.** Run one bounded job with an isolated worker while the creator reviews
   or edits. Prove responsive playback, conflict detection, cancellation, failure recovery, and no
   damage to the preferred version. Test app/runner restart and sleep/wake behavior.
4. **Repeated refinement.** Run several rounds across days. Verify the review queue stays small,
   feedback is applied, protected elements remain intact, and lack of improvement ends a round.
   Extend from scene excerpts to section transitions and full-song context.
5. **Broader access, if needed.** Phone/browser review, multiple projects, additional workers, or
   remote execution. Native Mac review is the confirmed first surface; remote access and remote
   execution are separate future decisions.

Pressure tests must also cover: no response for days; a manual edit while a candidate renders;
feedback on an older preview; an agent retry after a lost response; a worker crash; missing plugin
or asset; no evaluator; new protections during a run; and choosing neither candidate. Record actual
results before claiming these gates passed.

## Proposed ADRs

1. **[ADR-0061: Creative work persists across bounded refinement rounds](../decisions/ADR-0061-creative-work-persists-across-refinement-rounds.md).** Product loop, brief/feedback
   ownership, human authority, review capacity, and stopping conditions. Refines the PRD and ADR-0008.
2. **[ADR-0062: Candidates are isolated, versioned, and promoted explicitly](../decisions/ADR-0062-candidates-are-isolated-and-promoted-explicitly.md).** Snapshot boundaries, dependency
   integrity, preferred-version semantics, concurrency, and the relationship to undo/persistence.
3. **[ADR-0063: Background work is resumable and uses an external runner](../decisions/ADR-0063-background-work-uses-an-external-runner.md).** Host/runner/worker
   boundaries, durable work and job contracts, budgets, failure recovery, and MCP responsibilities.
4. **[ADR-0064: Review is a first-class workspace alongside creation](../decisions/ADR-0064-review-is-a-workspace-alongside-creation.md).** Playback, scoped feedback, returning-user
   navigation, and amendments to graph-home and domain-zone decisions.

These records are drafted with proposed status. Acceptance should name the evidence and remaining
limitations. The interaction, persistence, and execution proofs are pending; the documents do not
claim implemented behavior.

## Confirmed choices and remaining questions

- **Confirmed — review location:** Vivid on the creator's Mac. Phone/browser review is deferred.
- **Confirmed — authority:** freely explore candidates within the brief and protections; the creator
  explicitly chooses what becomes current. Automatic promotion is outside this first design.
- **Execution availability:** is an awake Mac sufficient initially, or must work continue when it is
  asleep? An always-on local host and a hosted worker have different plugin and asset requirements.
- **First creative scope:** improve an existing scene/song, or also develop a blank project? Recommend
  starting from an existing scene to test refinement separately from initial composition quality.
- **Stopping and review volume:** propose two meaningful alternatives per decision and one unresolved
  decision per work item initially. Validate rather than hard-code these as universal limits.

No runner, automation, production interface change, or accepted architectural decision is created by
this discussion document.
