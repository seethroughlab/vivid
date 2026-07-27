# ADR-0033: Composition Editing Ergonomics Are Core Audio Work

Status: proposed

Date: 2026-07-26

Extends [ADR-0017](ADR-0017-every-edit-is-reversible.md),
[ADR-0023](ADR-0023-shared-graph-ui-substrate.md), and
[ADR-0022](ADR-0022-session-audio-graph.md).

## Context

Vivid now has serious audio graph power: note, audio, and control edges; cross-track routing; clips
and generators as nodes; plugin nodes; stable global node ids; undo; and MCP access. But a powerful
engine is not automatically a professional composition surface.

Professional audio tools let users edit musical structure quickly and reversibly. They support
selection, duplication, bypass, audition, batch movement, and clear object identity. Without those
ergonomics, the user can technically build a sophisticated patch but cannot comfortably compose with
it.

The classic-platform gap document already identifies several graph-editing gaps: multi-select,
copy/paste/duplicate, Delete/Backspace, bypass/solo, and sticky notes. These are not polish after the
audio engine; they are how users safely operate a non-linear audio engine.

## Decision

Treat composition editing ergonomics as core audio work, implemented through the shared graph
substrate where possible.

1. **Multi-select is a first-class graph state.** Both visual and audio graphs support marquee,
   additive selection, group drag, and keyboard focus. Selection is stable across view transforms and
   undo boundaries.

2. **Copy, paste, duplicate, and delete work on graph selections.** Audio selections preserve nodes,
   pinned params, key ranges, plugin identity, sampler references, and internal edges. Pasted nodes
   receive new stable ids; edges to objects outside the copied set are deliberately omitted unless
   the command explicitly asks to keep them.

3. **Bypass is structural and audible.** Audio node bypass preserves graph shape and UI identity
   while routing signal according to node kind: effects pass input, sources silence or disable note
   generation, modulators freeze or disable by explicit mode. The bypass state is persisted and
   undoable.

4. **Solo/audition exists at the right level.** Track solo/mute already exists; graph-node solo should
   audition the upstream path to that node or isolate a source/effect branch without permanently
   rewriting edges.

5. **Sticky notes and labels are part of explainability.** Graph annotations are persisted,
   copyable, and addressable by MCP so a user or agent can leave intent in the session, not only in a
   separate document.

6. **Every edit routes through EditGateway.** These commands are not UI-only conveniences. They must
   be undoable, saveable, exposed to MCP where useful, and covered by canonical persistence tests.

## Alternatives Considered

- **Leave graph ergonomics to agents/MCP.** Rejected. Agent workflows are central, but a professional
  tool cannot require an agent for basic selection and duplication.
- **Implement audio graph UX separately from visual graph UX.** Rejected where the shared substrate
  can carry it. Separate semantics are fine; duplicate hit-testing and selection machinery are not.
- **Treat bypass/solo as mixer-only features.** Rejected. In a graph-based audio system, users need
  to audition branches and nodes, not only whole tracks.

## Consequences

- **Positive:** The existing session audio graph becomes playable as a composition surface, not only
  inspectable as topology.
- **Positive:** Shared graph work benefits visual and audio authoring together.
- **Tradeoff:** Copy/paste and bypass semantics are more complex for graph nodes than for linear
  device chains, especially around plugins, samplers, and cross-track edges.
- **Follow-up:** Add graph-selection tests, audio-copy/paste round-trip tests, bypass audio tests,
  and MCP tools for duplicate/delete/bypass/annotate.

