# Project Lockfile and Reproducibility Plan

Status: proposal and implementation plan. This fills the roadmap gap around library/package version pinning without changing the graph JSON contract in the first pass.

## Goal

Make a Vivid graph reproducible on another machine and at a later date.

Today, Vivid has a strong package system: manifests, install/link/rebuild, package tests, compatibility diagnostics, and package metadata. The missing production layer is a project-level record of exactly which Vivid core, packages, package sources, assets, and operator builds were used when a graph last worked.

The target user experience is: when opening a production graph, Vivid can say "this environment matches," "this graph can run with compatible updates," or "these exact dependencies are missing."

## Proposal

Add a project lockfile, tentatively named:

```text
vivid.lock
```

The lockfile should live next to the graph for single-file projects or at the project root once Vivid grows a directory project format. It should be generated and updated by Vivid, diffable in Git, and readable by MCP tools.

The lockfile is not a replacement for `vivid-package.json`. Package manifests describe what a package provides. The lockfile records what this project actually used.

## Lockfile Contents

Use JSON for v1 so existing graph/package tooling can reuse current parsing patterns.

Suggested top-level shape:

```json
{
  "lockfile_version": 1,
  "vivid_core": {
    "version": "0.1.0",
    "commit": "optional-dev-commit",
    "operator_abi": 1
  },
  "packages": [
    {
      "name": "vivid-wavetable",
      "version": "1.2.0",
      "source": "https://github.com/...",
      "commit": "abc123",
      "linked": false,
      "vivid_core": ">=1.0.0 <2.0.0"
    }
  ],
  "operators": [
    {
      "type": "WavetableSynth",
      "package": "vivid-wavetable",
      "package_version": "1.2.0",
      "source_path": "audio/wavetable_synth",
      "descriptor_hash": "..."
    }
  ],
  "assets": [
    {
      "asset_id": "workspace:wavetable:...",
      "kind": "wavetable",
      "path": "assets/wavetables/foo.wav",
      "content_hash": "optional"
    }
  ]
}
```

Keep optional fields optional. For example, linked local packages may not have a stable remote URL, but they should still record absolute path, package version, and current commit if the path is a Git repo.

## Implementation Shape

### Lockfile model and parser

Add a small runtime package/project module, for example:

- `src/runtime/packages/project_lockfile.h`
- `src/runtime/packages/project_lockfile.cpp`

Responsibilities:

- load and save `vivid.lock`
- validate lockfile version
- normalize package entries
- compute a dependency status report against installed packages
- produce user-facing and machine-readable diagnostics

Do not couple lockfile parsing directly to the UI. The UI, control server, CLI, and MCP tools should all consume the same status object.

### Dependency resolution modes

Add three load modes:

- Studio mode: current behavior plus warnings for mismatches.
- Strict mode: missing or incompatible locked dependencies block graph execution or disable affected nodes.
- Recovery mode: load everything possible, leave missing operators visible, and produce a complete dependency report.

Strict mode should be used by production gates and export. Studio mode should remain the default while authoring so experimentation stays fluid.

### Lockfile generation

Add commands through CLI and RuntimeAPI:

```bash
vivid lock --graph path/to/graph.json
vivid verify-lock --graph path/to/graph.json
```

Runtime/control server methods:

- `write_project_lockfile`
- `verify_project_lockfile`
- `get_project_dependency_status`

Lockfile generation should inspect the active graph and registry rather than scanning every installed package. Only packages/operators/assets actually referenced by the graph belong in the project lock.

### Package install integration

When a graph with a lockfile is opened and dependencies are missing, Vivid should be able to offer a guided install path:

- list missing packages with source URLs when known
- install exact commits where possible
- warn when only a version range is available
- preserve linked-package workflows for development

Do not automatically install network dependencies on graph open. Make that an explicit user action.

### Asset integration

For v1, record only assets referenced by loaded graph params or module metadata. Content hashes are useful for production, but they can be optional initially to avoid blocking the lockfile on every asset handler.

Later, asset handlers can provide kind-specific lock metadata: sample rate, channel count, wavetable frame count, media duration, codec, and so on.

## UI and API Behavior

Add a dependency status panel or banner:

- green: environment matches lockfile
- yellow: compatible package/core updates detected
- red: missing or incompatible dependencies

MCP and control-server output should use stable classifications:

- `match`
- `missing_package`
- `missing_operator`
- `compatible_update`
- `incompatible_update`
- `linked_unpinned`
- `asset_missing`
- `asset_changed`

These classifications should align with existing graph load diagnostics where possible.

## Testing

Add coverage for:

- lockfile JSON round-trip
- missing package classification
- matching package classification
- compatible and incompatible package version changes
- linked local package entries
- graph load in studio, strict, and recovery modes
- export refusing to proceed in strict mode with missing dependencies
- MCP/control-server dependency status response shape

Verification should include package and graph tests:

```bash
cmake --build build --target test_project_lockfile test_package_manager test_graph
ctest --test-dir build --output-on-failure -R "project_lockfile|package_manager|graph"
```

Adjust target names to match the final test layout.

## Acceptance Criteria

- Vivid can write a `vivid.lock` for the active graph.
- Vivid can verify the current environment against that lockfile.
- Strict mode prevents silent production drift.
- Studio mode preserves current fast authoring behavior while surfacing warnings.
- Missing packages/operators/assets are reported with actionable, structured diagnostics.

