# Release Audit — Current Architecture

This folder is the active release-facing audit record for the current Vivid release cycle.

This audit applies to the current post-switch architecture:

- owned embedded composition for host-local behavior
- ordinary ports for graph transport
- explicit outputs for graph-visible sharing

It is intentionally separate from:

- `docs/audit-history/role-binding-era/`
  - the previous audit of the earlier role-binding-era architecture
- `docs/internal/`
  - internal exploration, scorecards, and design notes
- `docs/release/`
  - operational release checklist and release ops procedures
- `docs/testing/`
  - test plans and manual validation references

Use this folder for the **current** audit only.

## Files

- `release-audit-summary.md`
  - rolling summary of current blockers, deferred issues, phase status, and release confidence
- `phase-0-baseline.md`
  - current audit baseline and triage setup
- `phase-1-runtime-core.md` through `phase-6-release-readiness.md`
  - one document per current audit phase

## Phase Document Contract

Every phase document should use the same shape:

1. `Scope Reviewed`
2. `Evidence Gathered`
3. `Findings`
4. `Required Fixes For Release`
5. `Deferred Follow-Ups`
6. `Signoff Status`

Allowed signoff states:

- `pending`
- `pass`
- `pass with defer`
- `block release`

## Summary Document Contract

`release-audit-summary.md` should track:

- current phase
- overall release status
- open blockers established by the current audit
- fixed issues established by the current audit
- deferred issues established by the current audit
- links to completed current phase documents

## Working Rules

- Do not inherit old findings as current truth automatically.
- Old audit findings can inform what to inspect, but they are not signoff evidence for this audit.
- Record anything already known at current-audit start as `known at audit start`.
- Keep raw logs out of these docs; summarize evidence and point to the relevant tests or source docs instead.
- Treat runtime, interface, release, and testing docs as living engineering contracts during this audit.
