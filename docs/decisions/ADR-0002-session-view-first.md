# ADR-0002: Session View First

Status: accepted

Date: 2026-06-17

## Context

Vivid Classic's graph view was powerful but too low-level as the default authoring surface for
song-like audiovisual work. The user and agent need a surface organized around performance sections,
musical roles, clips, visual states, and bindings.

## Decision

Vivid 4 makes Session View the primary authoring surface.

The node graph remains available as a deeper implementation and inspection layer, but normal session
work should not require graph vocabulary.

## Consequences

- Product proofs start with tracks, clips, scenes, bindings, and transport.
- MCP tools should expose session-level concepts before raw topology.
- Native runtime/schema work waits until the Session View workflow is pressure-tested.
