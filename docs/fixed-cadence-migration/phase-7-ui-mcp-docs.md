# Phase 7: Update UI, Control Server, MCP, and Docs for the Fixed-Cadence Model

## Summary

Update every user-facing surface so the fixed-cadence model is visible, inspectable, and teachable. This phase removes the last traces of the old implicit model from UI, API, and docs.

## Implementation Changes

### UI

- Remove cadence badge/toggle/cycle interactions from the node UI
- Render bridge edges distinctly from direct edges
- When dragging a frame↔audio wire, open a bridge picker that offers only valid bridge kinds

### Control server and MCP

Update the real repo surfaces:
- `src/runtime/control_server.cpp`
- `mcp/vivid_mcp.py`

Changes:
- remove `set_cadence_override`
- allow `connect` to accept `bridge`
- report fixed operator cadence in graph/node inspection
- report whether an edge is direct or bridged
- report bridge kind when present
- add `list_bridge_kinds` only if it materially simplifies clients and UI tooling

### Docs

- Rewrite architecture/runtime docs to teach:
  - fixed execution worlds
  - `_fr` / `_au` naming for paired operators
  - explicit bridge edges
  - no implicit cadence promotion
  - no scalar/audio coercion
- Remove references to:
  - audio-capable operators
  - cadence override
  - implicit scalar-to-audio behavior

Essential paths:
- `src/runtime/control_server.cpp`
- `mcp/vivid_mcp.py`
- `docs/ARCHITECTURE.md`

## Test Plan

- UI exposes bridge selection correctly
- Control-server and MCP inspection surfaces match runtime truth
- Docs no longer describe the old dual-cadence architecture
- Docs consistently describe `_fr` as fixed frame execution and `_au` as fixed audio execution for paired operators
- Manual graph-editing flow confirms users can still make frame↔audio patches easily, but explicitly

## Assumptions and Defaults

- The source of truth for MCP/control-server behavior in this repo is the runtime control server plus the Python MCP bridge
- No nonexistent CLI MCP server path is referenced in active migration docs
