# Phase 1: Product Map And Promises

Status: proposed

## Purpose

Verify that the shipped product shape matches the release promise in the PRD, glossary, UI
principles, ADRs, examples, and website-facing claims.

## User Task

A new user should be able to understand what Vivid is, what the audio surface does, what the visual
surface does, and how the bridge between them becomes a creative object.

## Hypothesis

If the product map is coherent, the release candidate will not present competing mental models or
make promises that the app cannot satisfy.

## Pressure Test

Read the user-facing docs and first-run surfaces, then map every visible concept to the canonical
product vocabulary.

## Scope

- Product docs: `docs/product/PRD.md`, `docs/product/glossary.md`, and
  `docs/product/ui-principles.md`.
- Current product decisions, especially the two-surface bridge, session/audio graph, visual graph,
  agent capability, and release-gated tutorial ADRs.
- User-facing release docs, website content, example descriptions, menu labels, diagnostics text,
  and first-run empty states.
- Any command or agent response that explains the project to a user.

Out of scope: rewriting the product vision or auditing code quality unless a product promise cannot
be traced to the app.

## Audit Procedure

1. Build a concept inventory from docs and visible UI labels: session, track, clip, audio graph,
   visual graph, operator, bridge, mapping, package, preview, transport, and project.
2. For each concept, record the canonical definition, where users encounter it, and whether the app
   teaches it through behavior.
3. Compare release-facing copy against the actual release candidate. Mark each claim as shipped,
   scaffolded, hidden, or unsupported.
4. Identify product promises that require evidence from later phases.
5. Write a short "first-release promise" paragraph that later phases can approve or challenge.

## Evidence To Collect

- A concept map table linking docs, UI labels, examples, CLI/MCP names, and release copy.
- Screenshots or notes for first-run and primary navigation states.
- A list of unsupported or ambiguous claims with suggested wording.
- Open questions that need an ADR, glossary update, or release-note callout.

## Deliverables

- Product promise matrix: claim, source, release status, evidence phase, and release action.
- Vocabulary mismatch list with severity.
- Draft release promise text, no longer than one paragraph.

## Acceptance Criteria

- Primary concepts match `docs/product/glossary.md` and current ADRs.
- Audio, visual, and bridge domains are distinct without feeling like separate products.
- User-facing copy does not promise missing or unreleased capabilities.
- There is a clear first-release definition of "done" for the product experience.

## Failure Modes

- Docs describe a product that the UI does not expose.
- UI labels introduce synonyms that fracture the mental model.
- The bridge is treated as hidden implementation detail rather than user-facing material.
- Marketing, examples, or release notes imply unsupported features.

## Evidence Log

- Pending.

## Open Questions

- Which examples are intended to define the first-release promise rather than demonstrate internal
  experiments?
- Which agent/MCP capabilities are public product surface on day one?
- What claims should be explicitly labeled as coming soon?

## Follow-Up Plans

- Link copy edits, ADR updates, or release-note corrections here.
