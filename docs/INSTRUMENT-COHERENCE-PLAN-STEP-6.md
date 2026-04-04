# Instrument-Oriented Preset And Browser Support V1

## Summary
Implement `#6` by making graph content metadata a first-class core concept, then extending the existing example browser to recognize instrument-ready graph entries without introducing a new preset file format or a separate instrument runtime.

V1 should keep the browseable unit as a graph file. Instrument libraries become an opt-in metadata profile on top of ordinary graph JSON, and the browser reads saved graph state, active variation, and selected preview controls as the "preset snapshot" for that entry.

## Story
Step 1 makes a graph patch feel like one coherent module. Step 2 gives that module a small local modulation surface. Step 5 makes the same module feel playable. Step 6 is the browse-and-recall layer that makes those instrument-ready graphs feel like a coherent library instead of an undifferentiated pile of examples.

For `vivid-wavetable`, that means package graphs can be marked as instrument content, grouped into readable families, distinguished as hero or reference entries, and surfaced with a compact preview of the controls that matter. The package still ships normal graph files. Core owns the metadata contract and browser behavior that lets those graphs feel like an instrument library.

For non-instrument use cases, existing graph browsing should remain intact. Ordinary demos, experiments, and package examples should still browse cleanly without being forced into an instrument taxonomy.

## Rationale
### Why this is useful
- Vivid already has graph metadata and an example browser, but the current model treats all graph content as the same kind of thing.
- Instrument-like packages need a compact way to say "this is a playable library entry" versus "this is a raw example or reference patch."
- The current graph save/load path does not make metadata a first-class part of the core `Graph` model, which limits reuse.
- A small metadata extension and browser refinement is enough to make package graph libraries feel more coherent without inventing a second preset architecture.

### Why this should stay graph-file-first in v1
- Graphs are already the source of truth for saved state, variations, package references, and current parameter values.
- Using graph files as the browseable preset unit avoids introducing a parallel preset format too early.
- The existing example-discovery and graph-meta-edit paths provide a practical seam for this work.

### Why this is not a new preset engine
- This does not add a second preset serialization system.
- It does not make each variation into a standalone browser item.
- It does not introduce package-specific preset containers.
- It extends graph metadata and browser semantics on top of the current graph model.

## Normal Workflow
### Instrument library workflow
- A package author saves a normal graph file that represents an instrument-ready patch.
- The author marks it as instrument content in graph metadata.
- The author adds compact library metadata such as category, family, role, playability, and preview-control references.
- The browser groups and filters that graph alongside other package content, but clearly distinguishes it from raw examples.
- Opening the entry still loads the normal graph with its saved live state and active variation.

### Ordinary example workflow
- A core or package graph with no instrument metadata continues to load as a normal example entry.
- Existing title, description, difficulty, domains, tags, and package requirements remain enough for ordinary browsing.
- The browser still supports searching and filtering these entries without requiring new metadata.

## V1 Key Changes
- Promote graph content metadata from ad hoc file-edit helper state into the core graph model so normal `Graph::load()` and `Graph::save()` preserve it.
- Introduce a core metadata struct for graph content and serialize it through the normal graph JSON `meta` block.
- Keep existing metadata fields:
  - `id`
  - `title`
  - `description`
  - `tags`
  - `difficulty`
  - `domains`
  - `requires_packages`
  - `featured_rank`
  - `estimated_minutes`
- Add optional instrument-oriented fields:
  - `content_kind`
  - `category`
  - `family`
  - `role`
  - `playability`
  - `preview_controls`
- `content_kind` should stay compact in v1:
  - `example`
  - `instrument`
- `role` should stay compact in v1:
  - `hero`
  - `reference`
  - `utility`
- `playability` should stay compact in v1:
  - `self_playing`
  - `midi`
  - `hybrid`
- Treat `preview_controls` as metadata references to existing node params, not as duplicated value storage.
- Keep the saved graph state and saved `active_variation` as the browseable snapshot. No new preset payload is introduced.
- Extend graph discovery so package and core graph entries carry enough metadata to distinguish instrument-ready content from ordinary examples.
- Extend the existing example browser into a unified content browser with an instrument/example kind filter rather than adding a second browser surface.
- Show compact instrument badges in the browser detail/list surfaces:
  - category
  - family
  - role
  - playability
  - package provenance
- Extend the graph metadata editor for simple new fields:
  - `content_kind`
  - `category`
  - `family`
  - `role`
  - `playability`
- Keep `preview_controls` JSON-authored in v1. The editor only needs to preserve them round-trip.

## Public Interfaces / Schema
- Add a graph-owned metadata struct, for example `GraphContentMeta`, as part of `Graph`.
- Parse and save the `meta` block through normal graph serialization instead of relying only on `GraphMetaEditData`.
- Accept legacy `meta.envs` as a load-time alias for `domains` so existing graphs remain compatible.
- Save only canonical `domains` in v1.
- Add optional JSON fields:
  - `meta.content_kind`
  - `meta.category`
  - `meta.family`
  - `meta.role`
  - `meta.playability`
  - `meta.preview_controls[]`
- `meta.preview_controls[]` entries should use a compact shape:
  - `node`
  - `param`
  - optional `label`
- Extend discovered browser-entry types so they carry:
  - graph path
  - package provenance when relevant
  - graph content metadata
  - content kind classification
- Extend runtime/query surfaces that already summarize graph/browser content so external tools can distinguish `instrument` from `example` entries using the same metadata contract.

## Browser Behavior
- Keep one browser surface in v1.
- Add a top-level kind filter such as:
  - `All`
  - `Instruments`
  - `Examples`
- Preserve current text search and existing difficulty/domain/package filtering behavior where it already exists.
- Sort instrument entries predictably:
  - package
  - category
  - family
  - title
- Continue to sort ordinary examples using current featured-rank-first behavior unless a shared ordering rule becomes obviously better during implementation.
- Show `preview_controls` as a small read-only control snapshot in the browser detail area by resolving the referenced saved node/param values from the graph file.
- Invalid `preview_controls` references should not block graph loading or browser display; they should simply be ignored or shown as unavailable.

## V1 Non-Goals
- No new preset file format
- No per-variation browser entries
- No dedicated package-specific instrument browser
- No mandatory taxonomy for all graphs
- No browser-side editing UI for `preview_controls`
- No new runtime instrument abstraction
- No requirement that non-instrument graphs adopt any of the new fields

## V2 Follow-Up
- richer browser grouping and visual affordances for instrument libraries
- authoring UI for selecting `preview_controls`
- optional favorites, ratings, or library curation fields
- per-variation or exposed-control snapshot browsing if real usage proves it is worth the added complexity
- tighter integration between Step 5 performance pages and browser preview surfaces

## Test Plan
- Graph save/load round-trip preserves old and new `meta` fields through normal `Graph::save()` and `Graph::load()`
- Legacy compatibility:
  - graphs with `envs` load correctly
  - graphs with `domains` load correctly
  - saves normalize to `domains`
- Existing graph metadata editor behavior still works for old fields
- New graph metadata editor fields save and reload correctly
- Browser discovery classifies `content_kind="instrument"` entries correctly
- Graphs with no `content_kind` remain visible and default to ordinary example behavior
- Package-backed graph discovery preserves package provenance for instrument entries
- Invalid `preview_controls` references do not fail graph load or browser discovery
- Browser filtering and sorting behave predictably for mixed example/instrument libraries
- Regression coverage proves current example browsing, graph opening, variations, and preset behavior remain unchanged for non-instrument graphs

## Assumptions And Defaults
- Chosen default: graph files remain the browseable preset unit in v1
- Chosen default: instrument support is opt-in through compact `meta` fields rather than a mandatory taxonomy
- Chosen default: `role` uses `hero`, `reference`, and `utility`
- Chosen default: `playability` uses `self_playing`, `midi`, and `hybrid`
- Chosen default: `preview_controls` are metadata references to existing params, not a second stored state payload
- Existing graph routing, variations, and per-node presets remain central; Step 6 adds browsing language and metadata polish, not a new preset architecture
