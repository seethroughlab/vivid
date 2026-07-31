# Release Audit Design

Status: proposed

This folder defines the first-release audit program. It is intentionally a planning scaffold:
these documents say what to inspect, what evidence to gather, and how to decide whether a finding
blocks release. They do not claim the audits have been performed.

Audit date: 2026-07-31.

## Top-Level Audits

- [UX audit](ux/README.md) - product behavior, workflow clarity, visual system, accessibility,
  agent-facing UX, and release readiness from a user's point of view.
- [Code audit](code/README.md) - architecture, realtime safety, rendering, persistence,
  package/operator boundaries, tests, and release infrastructure.

## Shared Audit Rules

- Every finding must include reproduction steps or a code reference.
- Every blocker must name the user impact and the smallest acceptable fix.
- Audit evidence belongs in the relevant phase file's evidence log.
- Cross-cutting release decisions should graduate to ADRs once they become durable constraints.
- UX and code phases may run in parallel, but any release-blocking mismatch between them must be
  reconciled before the release candidate is approved.

## Audit Rhythm

Each phase should produce a short verdict and a findings list before any implementation work starts.
Use this loop for each phase:

1. Prepare the audit surface: identify docs, source files, examples, scripts, commands, and app
   states that define the phase.
2. Run the walkthrough or code review procedure from the phase file.
3. Capture evidence while it is fresh: screenshots, command output summaries, file references,
   reproduction steps, and questions.
4. Classify findings using the shared severity table.
5. Decide the release action: fix now, explicitly waive, document as follow-up, or promote to ADR.

## Finding Format

Use this shape inside the relevant phase file or in a linked issue/PR:

```md
### P1: Short finding title

- Surface: UX or code area
- Impact: What user trust, creative workflow, data safety, or release process is at risk
- Evidence: Screenshot, command output, reproduction steps, or file references
- Smallest acceptable fix: The minimum change that removes the release risk
- Owner/status: Unassigned | assigned | fixed | waived
```

## Severity

| Level | Meaning | Release action |
|-------|---------|----------------|
| P0 | Crash, data loss, audio-hazard, security issue, or unrecoverable user workflow | Blocks release |
| P1 | Major workflow failure, hidden broken state, severe performance or accessibility issue | Blocks release unless explicitly waived |
| P2 | Confusing or incomplete behavior with a workaround | Triage before release |
| P3 | Polish, documentation, or follow-up opportunity | Track after release |

## Exit Criteria

- Every phase has an evidence log with pass/fail/needs-follow-up status.
- All P0 and unwaived P1 findings are fixed and verified.
- Remaining P2/P3 findings are documented with owners or follow-up plans.
- The release runbook and production gate match the final release candidate.
