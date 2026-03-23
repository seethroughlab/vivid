# Return to Embeddable Ops

## Summary

This document is now a transitional summary.

The canonical design for this direction lives in:

- [EMBEDDED-OPERATOR-SLOTS.md](/Users/jeff/Developer/vivid/docs/EMBEDDED-OPERATOR-SLOTS.md)

That document is the source of truth for:

- why embedded operator slots should replace role bindings
- why ordinary signal ports are not enough for host-local reusable composition
- how embedded slots, ports, and explicit outputs should divide responsibility
- the intended public model and implementation direction

## Current Direction

The intended product direction is:

- remove role bindings entirely
- restore owned embedded composition
- use curated slot types rather than arbitrary bindable assignment
- keep editing inspector-first, with optional expanded local editors rather than graph-visible child nodes

## Why This Changed

Role bindings were introduced to replace the earlier embedded-slots direction. After further evaluation, they now look like unnecessary structural complexity for the remaining product needs.

The better simplification is:

- **ports** for generic transport
- **embedded slots** for owned reusable local composition
- **explicit outputs** for sharing host-local results back into the graph

## Historical Position

This file is retained so the reversal remains easy to find in the repo history, but it is no longer the primary design reference.

For the actual design and implementation direction, use:

- [EMBEDDED-OPERATOR-SLOTS.md](/Users/jeff/Developer/vivid/docs/EMBEDDED-OPERATOR-SLOTS.md)
