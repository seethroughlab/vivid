# ADR-0023: Shared Graph UI Substrate

Status: accepted (implemented 2026-07-18)

Date: 2026-07-18

Implemented across PRs #49 (steps 1–6 + GraphCanvas v1), #55 (step 5 catalog), #57 (step 7
`list_operator_catalog`), #61 (#1 — GraphCanvas owns the pan/zoom camera), #64 (#2 — the
`GraphModelAdapter`), #67 (#3b — the audio editor draws + hit-tests in world space, "true zoom"),
#69 (#3c — GraphCanvas owns the shared card loop), and #72 (#3d — GraphCanvas owns the shared camera
gestures). The two editors now share the `node_canvas.h` marks, the `NodeView` world-space transform,
`CardPorts` geometry, the `card()` chrome, the `GraphModelAdapter`, the `GraphCanvas` card loop, and the
camera gestures; wire enumeration, port/preview content, and the model-entangled select/drag/rewire
gestures stay per-editor by design. See [`../product/graph-ui-abstraction-plan.md`](../product/graph-ui-abstraction-plan.md)
for the phase-by-phase record.

Extends [ADR-0007](ADR-0007-node-graph-contextual-deep-view.md),
[ADR-0013](ADR-0013-focus-first-strict-zone-ui.md),
[ADR-0014](ADR-0014-visual-graph-is-home.md), and
[ADR-0022](ADR-0022-session-audio-graph.md).

## Context

Vivid now has two graph editors with the same product shape but different implementations:

- the visual node graph, where the visual graph is the primary authoring surface and the bridge
  maps musical characteristics into visual params
- the audio graph deep view, where a track/session audio topology is edited as nodes, wires,
  params, plugins, notes, and modulators

The shared UI direction is already visible. `ui/node_canvas.h` centralizes graph cards, wires,
ports, preview wells, error vocabulary, and pan/zoom math. `ui/chooser.h` gives both graph
surfaces the same type-to-filter Tab palette. The control/API layer also has a shared operator
descriptor JSON helper so visual and audio discovery expose similar param and port metadata.

But the abstraction stops too early. `ui/node_graph.*` still owns visual-specific model access,
layout, interaction state, mapping registry access, shader assets, selection, chooser spawning,
and inspector hooks. `ui/audio_node_graph.*` separately computes audio node layout, ports, param
cells, plugin pin rows, waveforms, key-range handles, and hit-test geometry over `Session`
introspection. The two editors share a look and some widgets, but they do not share a graph-editor
contract.

The broader native UI has the same problem at the container level: the panel consolidation audit
found that panels, cards, menus, dock headers, graph nodes, clip cells, toolbar buttons, modals,
and editor shells still draw similar container concepts through local code paths. This makes the
UI harder to evolve consistently and makes graph work harder to localize.

## Decision

Build a shared graph UI substrate, then migrate the visual and audio graph editors onto it in
small, behavior-preserving steps.

The target layering is:

1. **Graph model adapter.** A small domain adapter describes nodes, stable ids, edge endpoints,
   ports, params, labels, badges, selection, node health, and optional preview data. Visual and
   audio graphs keep their existing runtime models; the adapter is only the UI-facing view of
   those models.

2. **Graph canvas.** A reusable canvas owns graph-region geometry, pan/zoom, shared node-card
   vocabulary, wire drawing, port drawing, selection treatment, ghost-wire drawing, hit-test
   primitives, and chooser anchoring. It builds on `node_canvas.h` rather than replacing it.

3. **Graph interaction controller.** Common interaction state moves out of the domain views:
   node drag, port drag, selection, pan, zoom, context-menu anchoring, and basic rewire gestures.
   Domain adapters provide command callbacks for operations such as connect edge, set param,
   add node, remove node, expose plugin param, or set node position.

4. **Domain-specific overlays remain domain-specific.** Visual GPU thumbnails, shader asset
   controls, audio waveforms, plugin pinned rows, key-range handles, compound audio widgets, and
   mapping-source pickers stay in visual/audio modules as hooks drawn inside shared canvas slots.

5. **Chooser entries use one addable-node contract.** The existing `Chooser` remains the widget,
   but visual operators, native audio ops, plugins, note ops, modulators, and future graph objects
   should be represented by one catalog-entry shape with domain/kind/spawn metadata. Domain
   callbacks decide what the selected row means.

6. **Agent discovery follows the same shape.** Add a unified discovery surface such as
   `list_operator_catalog(domain?)` that includes domain, kind, params, ports, semantic metadata,
   package/plugin origin, and spawn capability. Existing `list_operators` and audio discovery
   endpoints remain compatibility wrappers until agents and tests migrate.

7. **UI containers converge separately but consistently.** Graph nodes should use the same
   selected-item and container vocabulary as the rest of the interface. The graph substrate should
   align with the panel vocabulary from the consolidation audit: `WorkspaceCanvas`, `ZonePanel`,
   `DetailDock`, `ItemBox`, `Recess`, `OverlayPanel`, and `Separator`.

## Migration Plan

1. Document the adapter/canvas/controller contracts in `docs/product/graph-ui-abstraction-plan.md`.

2. Extract shared graph primitives first: node bounds, port rects, ghost-wire math, selection
   treatment, pan/zoom helpers, and chooser anchoring. This phase should not alter behavior.

3. Move audio graph drawing and hit testing onto the shared canvas first. `AudioNodeGraph` is the
   better first migration target because it is already mostly a deterministic view over session
   introspection.

4. Move visual graph drawing and hit testing in smaller pieces. `NodeGraph` owns more state, so
   mapping registry ownership, visual param plumbing, shader assets, and persistence helpers should
   remain stable while layout/draw/interaction code is extracted.

5. Normalize graph catalogs and chooser entry construction after both graph editors consume the
   shared canvas. This avoids forcing catalog changes before interaction behavior is stable.

6. Split remaining input routing after graph controllers exist. `input.cpp` should install GLFW
   callbacks and preserve modal priority, while graph-specific behavior lives behind the graph
   interaction controllers.

7. Add the unified operator catalog endpoint and migrate MCP parity tests last, with old endpoints
   kept as compatibility surfaces.

## Alternatives Considered

- **Keep two independent graph editors and only share visual helpers.** This is the current path.
  It is low risk short term, but every new graph capability has to be implemented twice or wired
  through one-off helpers.

- **Replace both editors with a large generic graph framework at once.** This would create a cleaner
  boundary on paper, but it risks destabilizing the primary visual surface and the newer audio graph
  at the same time.

- **Make the visual graph implementation the base class for audio.** The visual graph carries
  visual-specific state such as mapping registry access, shader assets, GPU thumbnails, and visual
  persistence helpers. Inheriting from it would preserve the wrong ownership model.

- **Unify the runtime graph models first.** ADR-0022 may eventually make audio a session-wide DAG,
  but the UI substrate should not wait for the runtime migration. A UI adapter lets the current
  per-domain models converge visually and interactively without forcing engine topology changes.

## Consequences

- **Positive:** audio and visual graphs gain one interaction language, one node/card vocabulary,
  one chooser pattern, one selection model, and one place to improve graph ergonomics.

- **Positive:** future graph surfaces, including a session-wide graph from ADR-0022, can reuse the
  same UI substrate instead of inheriting either current graph editor's domain-specific baggage.

- **Positive:** agents get a cleaner discovery model when the UI catalog and MCP/control catalog
  converge around domain, kind, params, ports, semantics, and spawn capabilities.

- **Cost:** the extraction must be staged carefully. The visual graph is a primary surface and
  `NodeGraph` currently mixes model access, view state, interaction, mapping, chooser, and inspector
  responsibilities.

- **Cost:** some duplication will remain intentionally. Preview rendering, plugin-specific
  inspectors, shader asset editing, audio key ranges, and mapping-source menus are domain behavior,
  not generic graph behavior.

- **Follow-up:** validation should run after each phase: native build, native tests, graph/editor
  interaction tests, and the MCP parity guard when control discovery changes.
