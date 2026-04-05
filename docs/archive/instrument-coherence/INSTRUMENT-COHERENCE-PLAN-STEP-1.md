# Subgraph Instruments V1/V2

## Summary
Implement `#1` as a maturation of the existing subgraph-module system, not as a separate instrument architecture.

Current state: `v1` shipped; items below under `V2 Follow-Up` remain deferred.

V1 should be **module-file-first** and **surface-first**:
- composite/instrument definitions live in `.vivid-module.json`
- instances behave like a single node with a curated exposed control surface
- internal nodes remain part of graph truth at runtime via existing flattening, but the main UX centers on the exposed surface
- opening/editing internals happens by opening the source module separately, not by nested in-place editing

V2 can extend the same model to embedded graph authoring and richer nested editing.

## V1 Key Changes
- Extend `SubgraphModuleDef` so exposed params/ports can carry the same metadata Vivid already uses for normal operators:
  - param type
  - default/min/max
  - choice labels when relevant
  - group/section
  - display hint / layout hints
  - semantic metadata
  - description
- Keep `.vivid-module.json` as the canonical authored format in v1. Package manifests continue declaring modules through the existing package discovery path.
- Keep the current flatten-before-compile model as the execution contract. Do not introduce a second execution path for composites.
- Treat module instances as first-class synthetic operators in the UI/catalog:
  - show exposed controls only
  - use grouped sections in the inspector
  - preserve normal node-level wiring through exposed ports
- Finish module preset support:
  - allow module-level factory presets declared on the module definition
  - expose them on module instances through the same preset UI affordance as normal nodes
  - apply them by remapping preset values through the module’s exposed-param bindings before or during flattening/runtime recall
- Add a clear “open source module” action for a selected module instance. In v1 this is the escape hatch for editing internals.
- Preserve ordinary graphs as-is. Packages can still ship plain graphs and plain operators alongside module instruments.

## Public Interfaces / Schema
- Extend `.vivid-module.json` schema:
  - `module.params[]` gains optional metadata fields matching `ParamInfo` where sensible
  - optional `module.control_groups` or equivalent is allowed only if needed to avoid repeating section names; otherwise reuse per-param `group`
  - `module.presets` remains supported and becomes runtime-visible
  - optional module-level metadata such as `description`, `category`, and small instrument-facing tags may be added if needed for browsing
- Keep authored graph JSON as the source of truth for instances:
  - module instances are still nodes with `type=<module-name>` and stored current param values
  - no embedded subgraph definitions in graph JSON in v1
- Update synthetic operator generation so `make_operator_info(...)` produces rich `OperatorInfo` rather than today’s minimal float-only surface.
- Add runtime/query support so control-server and MCP introspection expose module metadata the same way they expose normal operator metadata.

## V1 Non-Goals
- No embedded subgraph authoring inside normal graph files
- No full nested graph editing inside the main graph UI
- No recursive/nested module expansion requirement in v1
- No separate “instrument engine” or hard-coded synth signal path
- No attempt to solve modulation assignment, browser taxonomy, or performance pages in this slice beyond what is strictly needed for module surfaces and presets

## V2 Follow-Up
- Embedded subgraph definitions inside regular graph JSON
- Peek-inside visualization for selected module instances
- Nested module editing workflows
- Recursive module flattening after single-level semantics are stable

## Test Plan
- Parsing:
  - module files with rich param metadata load successfully
  - invalid exposed-param metadata fails clearly without corrupting registry state
- Synthetic operator surface:
  - module instances expose grouped controls, descriptions, semantic metadata, defaults, and choices through runtime/query APIs
- Flattening:
  - existing connection/param/MIDI/variation remapping keeps working with richer metadata present
  - module presets apply to the correct bound internal params
- UI:
  - inspector for a module instance shows only exposed controls, grouped correctly
  - preset dropdown/save behavior works for module instances
  - “open source module” resolves the correct module definition
- Package integration:
  - package-declared modules load at startup and appear in the catalog
  - package graphs can instantiate module types without additional runtime branches
- Regression:
  - ordinary operators, ordinary graphs, and current package loading behavior remain unchanged

## Assumptions And Defaults
- Chosen default: **safer v1**
- Chosen default: **surface-first editing UX**
- Chosen default: module files are the only authored composite definition format in v1
- Existing flattening remains the execution model; implementation should not add a second live-runtime representation
- Module metadata should reuse existing `ParamInfo` / `OperatorInfo` concepts as much as possible instead of inventing a parallel instrument-surface schema
- Embedded graph subpatch authoring is explicitly deferred to v2, but v1 code should avoid blocking that future extension
