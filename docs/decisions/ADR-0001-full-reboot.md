# ADR-0001: Full Reboot For Vivid 4

Status: accepted

Date: 2026-06-17

## Context

Vivid Classic proved many important ideas, but it also accumulated interface experiments,
implementation-era vocabulary, a large operator catalog, and graph-first assumptions that would make
Vivid 4 difficult to clarify in place.

## Decision

Vivid 4 starts as a clean reboot branch rather than a renovation of the Classic codebase.

Vivid Classic remains preserved as a branch, tag, reference, and parts bin. Code may be borrowed
only when it passes through the Vivid 4 product model.

## Consequences

- The Vivid 4 repo can stay small while the product shape is proven.
- Classic code remains available without dominating the architecture.
- Borrowed subsystems require explicit justification.
- Release versioning can restart for Vivid 4.
