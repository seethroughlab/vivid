# WGSL Filter Architecture: Clean-Break Refactor Plan

## Summary

WGSL filters should stop being a special-case subsystem and become first-class
operator types.

This refactor removes the pseudo-type `WGSLFilter`, removes graph-owned inline
filter definitions, and removes the split between "preset filters" and "real
operators." After this change, every `.wgsl` filter file is scanned, parsed,
and registered as a normal operator type.

Built-in filters, package filters, and project-local filters all use the same
runtime path:

1. discover `.wgsl` file
2. parse header metadata
3. build descriptor
4. register operator type
5. instantiate normally in the graph

Because Vivid is still pre-alpha, this is a clean break with no backward
compatibility layer.

---

## Why Refactor

The current WGSL filter system is architecturally expensive:

- registry code has a separate WGSL preset subsystem
- graph compilation has special fallback paths for filters
- graph persistence stores inline filter definitions separately from nodes
- runtime mutation treats filter selection as a topology-changing string param
- save/load materializes working shader files from graph JSON
- UI and command flows distinguish built-in presets, user filters, and normal operators

This makes filters a parallel concept rather than part of the operator model.

For Vivid, that is the wrong direction. The environment should make operator
authoring and composition feel uniform. Filters should be operators.

---

## Target Architecture

### First-class shader operators

Each `.wgsl` file becomes a concrete operator type at registry scan time.

A filter named `Blur` registers as type `Blur`, not as a preset behind a
generic `WGSLFilter` node. The descriptor is built from the file's JSON header,
using the same header-driven metadata model that exists today.

### One runtime path

There is no separate "WGSL preset" category.

The graph compiler should only need normal operator lookup by type name. If a
shader-backed operator exists, it is already registered under its real type and
can be instantiated like any other operator.

### Source-file ownership, not graph-owned shader blobs

Shader source belongs to files on disk, not to the graph JSON.

Built-in filters live in core `filters/`.
Package filters live in `<package>/filters/`.
Project-local filters live in `<graph_dir>/filters/`.

Graphs reference those operators by type name only.

---

## Clean-Break Decisions

### Remove `WGSLFilter`

Delete the pseudo-type `WGSLFilter` entirely.

There should no longer be a node whose type is `WGSLFilter` plus a string param
that selects a filter. A filter node's type is the actual operator type.

### Remove graph `filters`

Delete `FilterDef`, `Graph::filters_`, and all `filters` JSON serialization.

The graph should not store inline shader source, copied param metadata, or
separate filter definitions. It should store only normal nodes and normal
connections.

### No backward compatibility

Old graphs using:

- `WGSLFilter`
- graph-level `filters`
- filter-selection via string param `filter`

are no longer supported.

The graph schema should be bumped and older graphs using the removed model
should be hard-rejected rather than migrated.

### No live descriptor mutation

We do not introduce dynamic descriptor mutation as a first-class runtime/UI
feature.

If a shader file changes only in shader body, it can hot-reload in place.

If a shader file changes descriptor shape by modifying header-defined params,
ports, or time-dependence metadata, that is treated as a topology change:
invalidate caches, rescan shader operators, and rebuild the graph.

This keeps the runtime model simple and avoids weakening the current invariant
that descriptor shape changes require rebuild.

---

## Implementation Plan

### 1. Replace WGSL preset scanning with shader-operator scanning

Replace `scan_wgsl_presets()` with a new shader operator scan path that:

- walks a directory of `.wgsl` files
- parses each header
- builds a config object for the shader-backed operator
- registers a loader directly under the parsed operator name

There should be one registry entry per shader operator type.

The registry should expose generic shader-operator queries such as:

- `is_shader_operator(type_name)`
- `shader_operator_source(type_name)`

and should stop exposing WGSL-preset-specific APIs.

### 2. Rename data-driven filter internals to match their role

The current `DataDrivenFilterConfig` and `DataDrivenFilter` are effectively
generic shader-backed operators.

Rename them to something accurate, such as:

- `ShaderOperatorConfig` / `ShaderOperator`
- or `WgslOperatorConfig` / `WgslOperator`

This keeps the implementation model but removes historical naming that suggests
a separate filter subsystem.

### 3. Simplify graph compilation to a single operator lookup path

Delete the graph compiler's special handling for:

- preset names resolved through registry WGSL config lookup
- `WGSLFilter` nodes that swap in per-instance descriptors

After the refactor, `registry.find(ndef.type)` should be sufficient for shader
operators just like any other operator.

### 4. Remove graph-owned filter persistence

Delete:

- `FilterDef`
- `Graph::filters_`
- load/save support for `filters`
- working-directory materialization of graph-owned filter shaders
- save-time syncing of working `.wgsl` files back into graph JSON

Graphs should no longer copy shader source into or out of JSON.

### 5. Normalize authoring around project-local shader files

`Clone & Edit` for a shader-backed node should:

- require that the graph has a saved path
- copy the source `.wgsl` file into `<graph_dir>/filters/<new_name>.wgsl`
- rewrite the header `name`
- rescan shader operators
- change the selected node's type to the new operator name
- rebuild the graph
- open the new file in the external editor

If the graph has no saved path yet, `Clone & Edit` should fail with a clear
message instead of inventing a temporary persistence model.

### 6. Remove filter-specific UI and runtime behavior

Delete UI/runtime branches that exist only for the old filter system, including:

- WGSL preset lists in graph snapshots
- special inspector flows for selecting a filter preset
- `set_string_param(..., "filter", ...)` rebuild handling
- built-in-preset versus user-filter branching in open/clone flows

Shader-backed operators should use the same catalog, inspector, and node model
as other operators.

### 7. Keep export by embedding shader source assets, not graph filter blobs

Export should still support shader-backed operators, but through the new model:

- discover which shader operator types are used by the graph
- embed their `.wgsl` sources in the standalone build
- materialize those files at standalone startup
- run the same shader-operator scan path
- build the graph normally

This keeps export support without preserving the old graph-owned filter system.

---

## Public API / Data Model Changes

### Removed

- graph node type `WGSLFilter`
- graph JSON field `filters`
- node string param `filter` as filter selector
- registry APIs centered on WGSL presets and user filters:
  - `scan_wgsl_presets`
  - `wgsl_config`
  - `wgsl_preset_names`
  - `is_wgsl_preset`
  - `register_user_filter`
  - `unregister_user_filter`
  - `is_user_filter`

### Added / Renamed

- shader-operator scan API
- shader-operator source lookup API
- generic "is shader-backed operator" query
- renamed config/runtime classes for header-driven shader operators

---

## Testing

The refactor is complete when all of the following hold:

- each built-in `.wgsl` file registers as a normal operator type
- package-local `.wgsl` files register as normal operator types
- project-local `.wgsl` files under `<graph_dir>/filters/` register as normal operator types
- graph compilation uses the normal registry lookup path for shader operators
- graph save/load contains no `filters` section
- `Clone & Edit` creates a new project-local shader operator and retargets the node
- shader body edits hot-reload without graph rebuild
- header/schema edits trigger rescan + rebuild and update inspector/connection validity correctly
- export works for graphs using built-in and project-local shader operators
- duplicate type names across native operators and shader operators are rejected clearly

---

## Recommendation

Do this refactor.

The current system solves the original problem, but it does so by creating a
parallel operator architecture. Since Vivid is pre-alpha, this is the right
time to remove that split completely and establish a single clean model:

**shader-backed filters are operators, not presets, not graph-owned special
cases, and not a separate subsystem.**
