# UX Audit Phases

Status: proposed

The UX audit answers whether Vivid feels like a coherent first-release instrument: understandable,
reliable, inspectable, and creatively useful without requiring insider knowledge.

## Phase Index

1. [Product Map And Promises](phase-01-product-map-and-promises.md)
2. [Core Creative Workflows](phase-02-core-creative-workflows.md)
3. [Interface System And Information Architecture](phase-03-interface-system-and-information-architecture.md)
4. [Input, Accessibility, And Error Recovery](phase-04-input-accessibility-and-error-recovery.md)
5. [Agent And MCP Experience](phase-05-agent-and-mcp-experience.md)
6. [First-Run, Examples, And Release Packaging](phase-06-first-run-examples-and-release-packaging.md)

## Shared UX Evidence

- Screen recordings or notes from task walkthroughs.
- Screenshots of each primary state at desktop-sized and constrained-window layouts.
- Notes on confusing labels, hidden modes, dead ends, and mismatches with product docs.
- Reproduction steps for every workflow break.
- Links to code findings when the UX issue is caused by implementation structure.

## UX Audit Method

Run UX phases with a release-candidate mindset: no source-code edits, no developer-only commands
unless the phase explicitly calls for them, and no assumed knowledge that is not visible in the app,
docs, examples, or agent surface.

For every walkthrough, capture:

- Starting state and build/source revision.
- The exact task attempted.
- The user's expected next action at each step.
- Any point where the UI requires guessing, log inspection, or restart.
- The final state and whether it is understandable without narration.

## Cross-Phase Dependencies

- Phase 1 defines the promises Phase 2 and Phase 6 must verify.
- Phase 3 supplies visual-system findings for Phase 4 accessibility checks.
- Phase 5 should be checked against both Phase 1 product vocabulary and Phase 2 workflow evidence.
- Phase 6 should only claim first-run readiness after blocker findings from Phases 1-5 are resolved
  or waived.
