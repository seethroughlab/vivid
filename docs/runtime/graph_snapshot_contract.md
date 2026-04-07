# GraphSnapshot Contract

## Purpose

`GraphSnapshot` is the retained UI's read model for live runtime state.
This document makes that contract explicit so future UI work does not drift
back toward implicit or lossy behavior.

## Scope

`GraphSnapshot` is the frame-by-frame payload the UI can rely on for:

- nodes and operator metadata
- connections, including broken connections
- audio analysis
- MIDI mapping state
- variation / solo / recording state
- chooser catalog data

It is not a mutation surface. All writes still go through `UICommandSink`.

## Contract Rules

### 1. Graph truth must stay visible

Connections that exist in the graph must remain present in `GraphSnapshot`
even when an endpoint no longer resolves.

That means:

- broken connections are represented, not dropped
- UI code can inspect and render them
- users can still understand and fix invalid graph state

Relevant fields on `ConnectionSnapshot`:

- `invalid`
- `from_endpoint_missing`
- `to_endpoint_missing`
- `invalid_reason`

Helper semantics:

- `ConnectionSnapshot::is_broken()`
- `GraphSnapshot::find_connection(...)`
- `GraphSnapshot::broken_connection_count()`
- `GraphSnapshot::has_broken_connections()`

### 2. Snapshot data is UI-owned, not live runtime references

`GraphSnapshot` contains copied, UI-safe state for the current frame.
The UI should not assume mutability or cross-frame identity beyond what the
snapshot explicitly carries.

### 3. Package browser data is snapshot-backed

Package-browser content shown in the UI is treated as a cached snapshot that
is refreshed only at safe UI/runtime boundaries.

Current rule:

- refresh when browser opens
- refresh when a package action completes
- refresh during update/draw only when fetch state is `Ready` or `Error`
- refresh on any entry-content change, not only count changes

This keeps the browser from reading live mutable package-manager state directly.

### 4. `UICommandSink` remains the mutation boundary

`GraphSnapshot` should expose enough context for rendering and interaction
decisions, but not become a write channel.

Critical multi-step UI edits should either:

- use result-aware commands such as `try_add_node(...)`, or
- use result-aware wire mutation commands such as `try_connect(...)` /
  `try_disconnect(...)`, or
- be explicitly fenced so a failed early step cannot silently corrupt later steps

Current critical rule:

- chooser insert-on-wire must keep the original wire intact unless the
  replacement splice is fully established

## Testing Expectations

At minimum, regressions should protect:

- broken-connection visibility
- package-browser snapshot refresh behavior
- popup/text-edit flows that depend on active-field state
- chooser splice rollback when replacement wiring fails

Current coverage:

- `test_graph_snapshot_contract`
- `test_ui_overlay_interactions`
