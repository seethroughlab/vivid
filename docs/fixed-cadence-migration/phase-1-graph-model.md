# Phase 1: Add Explicit Bridge Metadata to the Graph Model

## Summary

Add bridge metadata to the graph and compiled-edge model without changing runtime behavior. This phase is additive and non-breaking. Its only job is to let later phases carry explicit bridge intent through parsing, serialization, and compilation.

## Implementation Changes

- Extend the graph connection model in `src/runtime/graph.h`:
  - add `std::string bridge` to `ConnectionDef`
  - add `bool has_bridge() const`
- Update JSON parsing/serialization in `src/runtime/graph.cpp`:
  - parse optional `"bridge"`
  - round-trip it exactly when present
  - omit it when empty so existing graphs remain unchanged
- Extend `CompiledEdge` in `src/runtime/compiled_graph.h`:
  - add `BridgeKind`
  - add `bridge_kind` field on edges
- Use this exact enum set:
  - `None`
  - `Hold`
  - `Snapshot`
  - `LastSample`
  - `Rms`
  - `Peak`
  - `Waveform`
- Keep compiler behavior unchanged in this phase:
  - graph compiler may parse `bridge` into `bridge_kind`
  - executor behavior must not change yet
  - non-empty `bridge` is carried but ignored for execution
  - unknown bridge strings compile as `BridgeKind::None` and emit a compiler warning

Essential paths:
- `src/runtime/graph.h`
- `src/runtime/graph.cpp`
- `src/runtime/compiled_graph.h`

## Test Plan

- Add JSON round-trip coverage for connection `bridge`
- Verify omitted `bridge` still serializes exactly as before
- Verify compiled graphs can carry `BridgeKind` without changing executor behavior or existing test expectations
- Verify unknown bridge strings warn and fall back to `BridgeKind::None`

## Assumptions and Defaults

- `bridge` lives on connections, never on nodes
- JSON bridge values are lowercase:
  - `hold`
  - `snapshot`
  - `last_sample`
  - `rms`
  - `peak`
  - `waveform`
- `BridgeKind::None` is the default for all existing connections
- Unknown bridge strings are preserved in graph JSON but compile to `BridgeKind::None` with a warning
