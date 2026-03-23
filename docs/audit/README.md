# Release Audit

This folder is the active release-facing audit record for the current Vivid release cycle.

It is intentionally separate from older exploration and internal hardening notes:

- `docs/internal/CODE-AUDIT-TRACKER.md` remains useful source material
- `docs/release/` remains the operational release checklist
- `docs/testing/` remains the test-plan and manual-validation reference set

Use this folder for the current audit only.

## Files

- `release-audit-summary.md`
  - rolling summary of blockers, deferred issues, and current phase status
- `phase-0-baseline.md`
  - audit baseline and triage setup
- future `phase-N-*.md`
  - one document per audit phase

## Phase Document Contract

Every audit phase document should use the same shape:

1. `Scope Reviewed`
2. `Evidence Gathered`
3. `Findings`
   - ordered by severity
4. `Required Fixes For Release`
5. `Deferred Follow-Ups`
6. `Signoff Status`
   - `pass`
   - `pass with defer`
   - `block release`

## Summary Document Contract

`release-audit-summary.md` should track:

- current phase
- overall release status
- open blockers
- fixed blockers
- deferred issues
- links to completed phase documents

## Working Rules

- Record anything already known at audit start as `known at audit start`.
- Do not re-report baseline noise as a new finding in later phases unless the severity or understanding changes.
- Keep raw logs out of these docs; summarize evidence and point to the relevant tests or source docs instead.
- Treat the runtime docs in `docs/runtime/` and the interface/release/testing docs as living engineering contracts during the audit.
