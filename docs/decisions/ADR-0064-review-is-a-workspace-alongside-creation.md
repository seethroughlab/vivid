# ADR-0064: Review Is a First-Class Workspace Alongside Creation

Status: proposed — native review interaction proof pending

Date: 2026-09-17

Follows: [ADR-0061](ADR-0061-creative-work-persists-across-refinement-rounds.md).
Amends, upon acceptance: [ADR-0013](ADR-0013-focus-first-strict-zone-ui.md) and
[ADR-0014](ADR-0014-visual-graph-is-home.md).
Preserves the authoring surfaces of [ADR-0009](ADR-0009-two-surface-bridge-and-native-reboot.md).

## Context

The session grid and visuals graph are appropriate places to create and edit. A returning creator
with two minutes available instead needs to hear/see what changed and provide useful direction.
Making them reconstruct agent activity from graph changes or a conversation wastes that time.
The confirmed first review surface is Vivid on the creator's Mac.

## Decision

1. **Provide explicit Review and Create workspaces.** Create retains the audio session and visuals
   graph. Review emphasizes media, comparisons, and decisions. Preserve the selected workspace;
   offer a pending-review shortcut without forcibly changing context. ADR-0014's graph-is-home rule
   applies within Create rather than requiring the graph to be visible in every workspace.
2. **Make the return legible.** Show the preferred version with playback, a brief “since your last
   review” summary, a small decision queue, and work status with pause/resume. Status distinguishes
   work in progress, review readiness, waiting, paused, and blocked, with the relevant reason.
   A raw tool transcript is supporting detail rather than the main review surface.
3. **Review perceptible differences.** Each item asks one focused question and includes the baseline
   and candidate media. Provide synchronized A/B playback at the relevant bars/seconds and access
   to surrounding context. Visual comparisons play motion with its audio. Listening-level matching
   is labeled, reversible, and affects audition only; unadjusted playback remains available.
4. **Make choices unambiguous.** Use this version, Keep as an alternative (for a clip-scoped
   candidate, this places a take in the cell's Variation Well), Revise, and Dismiss have separate
   meanings. Previewing and commenting do not implicitly promote. “Neither” is a valid
   answer. Choosing and providing feedback must persist without requiring a connected agent.
5. **Attach feedback to its subject.** Comments target a version and, optionally, a passage or
   project object. Preserve original feedback and expose its interpreted scope. The creator can
   inspect protections and current direction, then enter the relevant editor for detailed work.
6. **Keep domain identity clear.** Shared review chrome hosts an audiovisual decision; audio and
   visual controls remain explicitly identified. This qualifies strict domain zoning for the
   combined review surface without blending the authoring editors into an ambiguous control panel.

Rendered reviews remain playable while the runner is disconnected or busy. Native review is the
first scope; phone/browser access and remote execution are independent future decisions.

## Alternatives Considered

- **Review only in external chat.** Can be a client of the same records, but loses native playback,
  project context, and direct access to the editors as the primary workflow.
- **Add a permanent review panel beside every editor.** Crowds the creation surfaces and competes
  with the focused layout without serving occasional review well.
- **Replace the graph with a universal dashboard.** Weakens the established visual authoring model.
- **Show all generated experiments.** Transfers the agent's filtering work to the creator.

## Consequences

The shell gains workspace navigation and a media-focused review surface. Review uses the persistent
records from ADR-0061 and promotion contract from ADR-0062; it cannot maintain a second decision
store. Saved preview media reduces contention with the background engine but consumes storage.

Current scene exports do not reproduce an entire performance's scene launches. Review evidence must
include a playback recipe with timing, scene transitions, and warm-up when relevant. Begin with a
reproducible scene excerpt; do not present that proof as full-song review. A Cue Path versus timeline
decision is outside this ADR.

## Acceptance Evidence Required

Use real audio and moving visuals to compare a baseline and two alternatives. Measure whether a
returning creator can understand the change, choose or reject it, and leave passage-specific feedback
within two minutes without reading a transcript. Test no agent connection, old-version feedback,
neither-candidate selection, and returning to manual creation without losing context. Evidence is
pending; exact layout should follow this interaction proof.
