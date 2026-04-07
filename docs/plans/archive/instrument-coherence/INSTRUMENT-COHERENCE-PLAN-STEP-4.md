# Importable Asset Library For Package Instruments V1/V2

## Summary
Implement `#4` as a **generic asset-library and import/index/cache layer** with a **wavetable-first** v1, not as wavetable synthesis logic in core.

Current state: `v1` shipped; items below under `V2 Follow-Up` remain deferred.

V1 should be **library-first** and **path-compatible**:
- package-owned assets remain ordinary files in package repos,
- user-imported assets are copied into a workspace-owned asset library,
- core owns import, indexing, metadata, preview/analysis, and derived-cache lifecycle,
- packages continue consuming ordinary file/string params and `prepare_instance_assets()` rather than a new synth-only runtime object,
- authored graph JSON continues storing canonical file paths in v1 rather than a new opaque asset-reference schema.

Key boundaries:
- package-owned wavetable voicing stays package-owned,
- existing file-param and graph save/load behavior stays central,
- the asset library is generic by asset kind even though `wavetable` is the first target,
- this does not create a package-specific browser or a new instrument runtime.

## Story
Step 1 makes a synth graph feel like one module. Step 2 gives that module a small local modulation surface. Step 3 gives packages a better reusable filter primitive. Step 4 gives the same package a **stable content pipeline** so factory assets and user-imported assets can live behind one coherent library surface.

For `vivid-wavetable`, that means:
- factory wavetables shipped in the package can be discovered consistently,
- user files can be imported into a known workspace library location,
- metadata and previews can be cached once and reused,
- package operators can rely on canonical paths and indexed metadata instead of each repo inventing a separate import/cache story.

The same pattern should still help non-wavetable packages later. A sampler, granular package, or hybrid AV package should be able to reuse the same import/index/cache shape with a different asset kind.

## Rationale
### Why this is useful
- Modern instruments feel coherent when factory content and user content share one browse/import path.
- Packages should not have to reinvent file import, deduping, preview generation, cache invalidation, and library browsing.
- Vivid already has operator-level warmup through `prepare_instance_assets()`, but that hook is per-instance and not a substitute for a shared asset library.

### Why this should stay path-compatible in v1
- Existing operators already consume file params and string params.
- Existing graph and preset persistence already knows how to store canonicalized file paths.
- Keeping paths as the persisted reference avoids forcing a graph-schema migration just to land the first useful asset-library slice.
- The asset library can still assign stable internal `asset_id` values for indexing and caching without making authored graph JSON depend on them.

### Why this is not wavetable synthesis in core
- Core owns import, discovery, metadata, and cache lifecycle.
- Packages still own wavetable playback, scanning, interpolation, voice architecture, and sound-design semantics.
- The feature should help many packages even if `vivid-wavetable` is the motivating example.

## Normal Workflow
### Package/factory asset workflow
- A package ships factory content in package-relative asset directories.
- V1 supports an optional manifest `assets` block and also a conventional fallback directory scan for `assets/wavetables/`.
- On package scan/startup, core indexes those assets as read-only library entries with package provenance.
- Package modules and browsers can show factory assets through the same metadata/query path used for imported user assets.

### User import workflow
- A user imports one or more wavetable files through a UI/control-server path.
- Core validates the file, copies it into the workspace asset library, computes metadata, and writes a sidecar index record.
- Core builds any derived preview/analysis/cache artifacts needed for browsing and package warmup.
- When the user chooses that asset for a node/module, Vivid writes the canonical workspace-relative path into the existing file/string param surface.

### Package operator workflow
- A package operator or module receives the canonical resolved file path through its normal param/file-param path.
- If the operator needs heavy one-time preparation, it uses the existing `prepare_instance_assets()` hook.
- The operator may also query indexed metadata indirectly through future browse/query APIs, but the core v1 execution contract remains ordinary file-param consumption.

## V1 Key Changes
- Add one new runtime subsystem family for importable assets:
  - asset discovery,
  - asset index persistence,
  - asset import/copy,
  - asset preview/analysis,
  - derived-cache lifecycle.
- V1 supports one asset kind:
  - `wavetable`
- Keep package assets and user assets separate by ownership:
  - package assets are read-only and discovered from package directories,
  - imported user assets are writable and live under the workspace root.
- Default workspace library layout:
  - `<workspace_root>/assets/library/wavetables/<asset_id>/source/<original_filename>`
  - `<workspace_root>/assets/library/wavetables/<asset_id>/asset.json`
  - `<workspace_root>/assets/library/.cache/wavetables/<asset_id>/...`
- Package discovery rules:
  - optional `vivid-package.json` `assets` block may declare per-kind asset directories,
  - if no `assets` block is present, v1 also scans the conventional package path `assets/wavetables/`.
- Asset index entries carry:
  - stable `asset_id`,
  - `kind`,
  - display name,
  - source scope (`package` or `workspace`),
  - package name when relevant,
  - canonical relative path,
  - source hash / modification fingerprint,
  - import/discovery timestamps,
  - lightweight generic metadata such as file format and byte size,
  - kind-specific metadata for wavetables such as sample rate, channel count, sample/frame counts, preview summary, and normalization/analysis results.
- Import pipeline behavior is fixed in v1:
  - copy source bytes into the workspace library,
  - do not mutate the user's original file in place,
  - compute metadata and preview/analysis eagerly enough that later browsing does not need to re-scan the raw file on every startup,
  - regenerate derived cache artifacts only when the source fingerprint or analyzer version changes.
- Persistence behavior is fixed in v1:
  - graphs and presets keep storing canonical file paths, not `asset_id`,
  - the asset library exposes `asset_id` for indexing and UI query purposes only,
  - canonical persisted paths should be workspace-relative when the asset lives under the current workspace root.
- Reuse existing operator/runtime patterns where possible:
  - `prepare_instance_assets()` remains the operator warmup hook,
  - existing file-param path resolution remains the execution path,
  - package scanning remains part of `PackageManager`,
  - example/browser metadata remains separate from the asset index.

## Public Interfaces / Schema
- Extend `vivid-package.json` with an optional `assets` block:

```json
{
  "assets": {
    "wavetables": ["assets/wavetables"]
  }
}
```

- Manifest semantics in v1:
  - keys are asset kinds,
  - values are package-relative directories,
  - unknown asset kinds are ignored safely by older tooling.
- Add a runtime asset-index format stored in the workspace library:
  - one `asset.json` sidecar per imported asset,
  - package-discovered entries may be materialized into the in-memory index and optional cache records without copying package-owned sources.
- Add asset-library query/mutation support through control-server/MCP surfaces:
  - list assets by kind and scope,
  - inspect one asset's metadata,
  - import an asset into the workspace library,
  - refresh/rebuild the asset index or cache when needed.
- No graph JSON schema change is required in v1 beyond ordinary file/string param storage.

## Cache And Lifecycle Behavior
- Cache keys are based on:
  - `asset_id`,
  - source fingerprint,
  - analyzer/cache version.
- Cache artifacts survive app restarts and package rebuilds.
- Rebuilding a package must not invalidate workspace user assets.
- Re-scanning packages refreshes package-asset discovery without touching imported user assets.
- If a package asset disappears or changes on disk:
  - its index entry is refreshed or removed on the next package scan,
  - any graph still referencing the old path behaves like any other missing file-param path today.

## V1 Non-Goals
- No wavetable playback, interpolation, or oscillator behavior moved into core
- No new graph-schema asset-reference object
- No requirement that packages stop using plain file params
- No asset editing or in-core wavetable authoring tools
- No cloud sync, sharing, or remote library service
- No broad multi-kind asset platform in v1 beyond the generic framework plus the `wavetable` kind
- No package-specific custom import pipeline hooks in v1
- No replacement of `prepare_instance_assets()` with a global asset execution model

## V2 Follow-Up
- Asset picker UI integrated into module and file-param controls
- Broader asset kinds such as samples, impulse responses, or LUTs
- Optional graph persistence by `asset_id`
- Batch import, duplicate detection, and rename/delete workflows
- Author-defined tags, favorites, and richer preview metadata
- Audition and preview actions for sound-design workflows

## Test Plan
- manifest parsing for valid and invalid `assets` blocks
- package scan discovering factory wavetable assets through declared and conventional directories
- import copying a source file into the workspace asset library and writing `asset.json`
- asset index round-trip load/save across app restart
- canonical persisted path generation for imported workspace assets
- merged asset listing returning both package and workspace entries with correct provenance
- cache invalidation when source bytes or analyzer version changes
- package rebuild and package re-scan leaving imported workspace assets untouched
- graph/preset regression coverage proving ordinary file-param save/load behavior remains unchanged
- package-backed validation proving a wavetable package can browse factory and imported assets through one stable content path

## Assumptions And Defaults
- Chosen default: `wavetable` is the only first-class asset kind in v1
- Chosen default: package assets are discovered read-only; user imports are copied into the workspace library
- Chosen default: authored graphs continue storing canonical file paths, not `asset_id`
- Chosen default: package manifests may declare asset roots, but `assets/wavetables/` is also scanned by convention in v1
- Chosen default: imported assets use per-asset sidecar metadata plus a persistent derived-cache directory
- Existing file-param execution, package scanning, and operator warmup hooks remain central; Step 4 adds reusable content plumbing, not a new instrument architecture
