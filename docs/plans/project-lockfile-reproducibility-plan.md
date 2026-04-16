# Project Lockfile and Reproducibility Plan

Status: proposal and implementation plan. Fills the roadmap gap around library and package version pinning without changing the graph JSON contract in the first pass.

## Goal

Make a Vivid graph reproducible on another machine and at a later date.

Today Vivid has a strong package system: manifests, install/link/rebuild, package tests, compatibility diagnostics, and package metadata. The missing production layer is a project-level record of exactly which Vivid core, packages, package sources, assets, and operator builds were used when a graph last worked.

The target user experience: when opening a production graph, Vivid can say "this environment matches," "this graph can run with compatible updates," or "these exact dependencies are missing."

## Current State

**Already present and reusable:**

- Package manifest and in-memory registry — `PackageInfo` in `src/runtime/packages/package_manager.h:38` with `name`, `version`, `vivid_core` SemVer range, `path`, `source_scope`, `linked`, `operators`, `gpu_operators`, `modules`, `assets`.
- Package compatibility classification — `PackageUpdateClass` and `PackageUpdateAssessment` in `package_manager.h:87` (`UpToDate`, `CompatibleUpdate`, `IncompatibleUpdate`, …). The lockfile report should reuse these rather than invent a parallel taxonomy.
- Per-node provenance on disk — `NodeDef.pkg_name` / `NodeDef.pkg_version` in `src/runtime/graph/graph.h:79`. Graphs already serialize which package provided each node at save time.
- Graph-level declaration — `GraphContentMeta.requires_packages[]` in `graph.h:32` (name-only today; no version range).
- Missing-operator classification at compile time — `CompiledNode.missing_operator_reason` in `src/runtime/graph/compiled_graph.h:454` with values `"not_found" | "not_built" | "abi_mismatch" | "load_failed" | "disabled"`. Lockfile diagnostics should align.
- Operator registry provenance — `OperatorProvenance` in `src/runtime/operators/operator_registry.h:71` tracks `package_name`, `package_path`, `abi_mismatch`, `load_failed`.
- Core version and ABI constants — `VIVID_CORE_VERSION` in `src/runtime/core/workspace_manager.h:11`; `VIVID_OPERATOR_ABI_VERSION` in `src/operator_api/types.h:11`.
- Diagnostic shape for UI/API — `{id, severity, node_id, message, suggestion}` used by `run_diagnostics` in `src/runtime/control/control_server_checks.cpp:41`.
- JSON infrastructure — `nlohmann/json`; `Graph::save_to_string()` / `Graph::load_from_json_doc()` in `graph.h:356` are good reference patterns.

**Missing — must be built as prerequisites:**

- Git metadata capture. `PackageInfo` has no `source_url` or `git_commit`. `PackageManager::install` (`package_manager_install.cpp`) already shells out to git but discards the resolved commit.
- Operator descriptor hash. The registry has no content fingerprint for a loaded operator. Needed to detect silent ABI-compatible operator drift (e.g., a param was renamed without a manifest version bump).
- Graph-adjacent project concept. A graph is a standalone file; there is no "project" wrapper and no sibling-file discovery. Workspace root (`~/Documents/Vivid`) is the closest existing anchor.
- Unified dependency status API. `run_diagnostics` surfaces missing operators, but there is no endpoint that reports "environment state vs. lockfile" as a first-class object.

## Proposal

Add a project lockfile, tentatively named `vivid.lock`, living next to the graph for single-file projects and at the project root once Vivid grows a directory project format. It is:

- Generated and updated by Vivid (never hand-edited in normal flow).
- Diffable in Git.
- Consumed by UI, control server, CLI, export, and MCP through a single status object.

The lockfile is not a replacement for `vivid-package.json`. Package manifests describe what a package provides. The lockfile records what this project actually used.

## Lockfile Contents

JSON, v1. Top-level shape:

```json
{
  "lockfile_version": 1,
  "generated_at": "2026-04-16T14:32:00Z",
  "graph": {
    "path": "demo.json",
    "schema_version": 4,
    "content_hash": "sha256:…"
  },
  "vivid_core": {
    "version": "0.1.0",
    "commit": "optional-dev-commit",
    "operator_abi": 15
  },
  "packages": [
    {
      "name": "vivid-wavetable",
      "version": "1.2.0",
      "vivid_core": ">=0.1.0 <0.2.0",
      "source": {
        "kind": "git",
        "url": "https://github.com/…",
        "commit": "abc123…"
      },
      "linked": false,
      "linked_path": null
    }
  ],
  "operators": [
    {
      "type": "WavetableSynth",
      "package": "vivid-wavetable",
      "package_version": "1.2.0",
      "descriptor_hash": "sha256:…",
      "operator_abi": 15
    }
  ],
  "assets": [
    {
      "asset_id": "workspace:wavetable:foo",
      "kind": "wavetable",
      "path": "assets/wavetables/foo.wav",
      "content_hash": "sha256:…"
    }
  ]
}
```

Field rules:

- `lockfile_version` — integer; bumped for breaking schema changes.
- `graph.content_hash` — sha256 of canonicalized graph JSON. Lets verify detect graph changes without a lockfile rewrite.
- `vivid_core.operator_abi` — mirrors `VIVID_OPERATOR_ABI_VERSION`. Mismatch is always incompatible.
- `packages[].source.kind` — `"git"` | `"local"` | `"registry"` (future). `commit` required for `"git"`; `linked_path` required for `"local"`.
- `packages[].linked` — true when the package was linked from a local directory at lock time. Linked entries still record a commit if the path is a Git worktree and clean.
- `operators[]` — only operators instantiated by at least one node in the graph. `descriptor_hash` covers the param/port descriptor table plus `operator_abi`.
- `assets[]` — only assets referenced by loaded graph params. `content_hash` optional in v1 for large media; always present for workspace-scoped wavetables and similar small assets.

Absent fields are treated as "unknown," not "doesn't match." This keeps linked-dev workflows usable without loud false positives.

## Implementation Phases

Phasing is chosen so each phase is independently reviewable and testable.

### Phase 0 — Prerequisites (plumbing for provenance)

Nothing in this phase is user-visible; it makes the lockfile possible.

1. Extend `PackageInfo` (`src/runtime/packages/package_manager.h:38`) with `std::string source_url`, `std::string git_commit`, `std::string resolved_at`. Update the manifest parser (`package_manager_manifest.cpp`) to preserve these fields across save/load of the discovery report.
2. Capture git metadata during install and link (both implemented in `package_manager_install.cpp`):
   - `PackageManager::install` — after clone, run `git rev-parse HEAD` and `git config --get remote.origin.url`; store on `PackageInfo`.
   - `PackageManager::link` — if the linked path is a Git worktree, capture commit and a `dirty` bool via `git status --porcelain`.
3. Add operator descriptor hashing. In `OperatorRegistry` (`src/runtime/operators/operator_registry.h`), extend `DeferredEntry` (line 20) with a `std::string descriptor_hash`. Compute once at load time from the `VividOperatorDescriptor` param/port tables and `VIVID_OPERATOR_ABI_VERSION`. Expose via a new `OperatorRegistry::descriptor_hash(type_name)` accessor.

Tests: unit tests in `tests/packages/test_package_git_metadata.cpp`; descriptor hash stability test in `tests/operators/test_operator_descriptor_hash.cpp`.

### Phase 1 — Lockfile model and parser

New files:

- `src/runtime/packages/project_lockfile.h`
- `src/runtime/packages/project_lockfile.cpp`

Responsibilities:

- `ProjectLockfile` struct mirroring the JSON schema.
- `load(path) -> std::expected<ProjectLockfile, LockfileError>` using `nlohmann::json`.
- `save(path, lockfile)` with canonical key order for diff stability.
- `validate_version(lockfile)` returns a structured error on future or malformed versions.
- `canonicalize_graph_hash(const Graph&)` utility reused by generate and verify.

No coupling to UI, control server, or CLI. Public surface is a plain value type + free functions.

Tests: `tests/packages/test_project_lockfile.cpp` — round-trip, canonical key order, version rejection, missing-field defaults.

### Phase 2 — Generation

Entry point: `ProjectLockfile build_lockfile_for_graph(const Graph&, const PackageManager&, const OperatorRegistry&, const AssetLibrary&)`.

- Walks `graph.nodes()`, resolves each node's `pkg_name`/`pkg_version` (already present in `NodeDef`) against the registry, and records only packages/operators/assets that are actually referenced.
- Fills `operator.descriptor_hash` from the registry.
- Fills `asset.content_hash` where the asset handler provides it; leaves it null otherwise (see Phase 8).

RuntimeAPI method: `RuntimeAPI::write_project_lockfile(graph_path, output_path_opt)`.
- Implementation lives in a new `src/runtime/control/runtime_api_lockfile.cpp` to mirror the existing `runtime_api_persistence.cpp` / `runtime_api_modulation.cpp` split.

Tests: generation golden test with a small fixture graph in `tests/packages/test_project_lockfile.cpp`.

### Phase 3 — Verification and status

Entry point: `LockfileStatus verify_lockfile(const ProjectLockfile&, const PackageManager&, const OperatorRegistry&, const AssetLibrary&)`.

Returns a structured report:

```cpp
struct LockfileFinding {
  std::string id;          // e.g. "missing_package"
  Severity severity;       // info | warning | critical
  std::string subject;     // package name, operator type, or asset id
  std::string message;
  std::string suggestion;
};

struct LockfileStatus {
  enum class Overall { Match, CompatibleDrift, Mismatch };
  Overall overall;
  std::vector<LockfileFinding> findings;
};
```

Finding IDs — aligned with existing vocabulary:

- `match` — entry satisfies the lock.
- `missing_package` — locked package not installed.
- `missing_operator` — operator type unresolved (maps to `missing_operator_reason == "not_found"`).
- `compatible_update` — reuse `PackageUpdateClass::CompatibleUpdate`.
- `incompatible_update` — reuse `PackageUpdateClass::IncompatibleUpdate`.
- `linked_unpinned` — linked package with no commit or dirty worktree.
- `asset_missing`, `asset_changed` — asset file gone or content_hash differs.
- `abi_mismatch` — operator ABI drift (maps to `missing_operator_reason == "abi_mismatch"`).
- `descriptor_hash_mismatch` — same operator type/version but descriptor changed (silent drift).

RuntimeAPI methods:

- `RuntimeAPI::verify_project_lockfile(graph_path, lockfile_path)` — pure verification, no mutation.
- `RuntimeAPI::get_project_dependency_status(graph_path)` — convenience; discovers the sibling lockfile, returns `LockfileStatus`. Returns a well-formed "no_lockfile" status when absent.

### Phase 4 — Control server + MCP surface

- Dispatch entries in `src/runtime/control/control_server_dispatch.cpp` (pattern from `save_graph` at line 206):
  - `write_project_lockfile`
  - `verify_project_lockfile`
  - `get_project_dependency_status`
- MCP tool wrappers in `mcp/vivid_mcp.py` and `mcp/vivid_opdev_mcp.py` using the `@mcp.tool()` decorator (pattern at `mcp/vivid_mcp.py:753`). Keep the JSON body shape 1:1 with RuntimeAPI.

### Phase 5 — CLI

Add subcommands in `src/runtime/core/main.cpp` alongside the existing `export`/`install`/`scaffold-operator` pattern (lines 190, 205, 251):

```bash
vivid lock --graph path/to/graph.json [--output path/to/vivid.lock]
vivid verify-lock --graph path/to/graph.json [--lockfile path/to/vivid.lock]
```

Both subcommands delegate to the same RuntimeAPI entry points; the CLI surface is a thin translation to JSON.

Exit codes for `verify-lock`: `0` match, `1` compatible drift, `2` mismatch, `3` lockfile or graph I/O error. Enables CI gates.

### Phase 6 — Load modes and UI banner

Three modes wired into `RuntimeCore::load_graph`:

- **Studio** (default) — current behavior; emit warnings for mismatches via the existing diagnostics channel.
- **Strict** — missing or incompatible locked dependencies disable affected nodes (`missing_operator = true` with a new reason `"locked_unavailable"`) and block graph execution start. Selectable via settings or a load-time flag.
- **Recovery** — load everything possible; return a complete `LockfileStatus` without blocking.

UI: persistent dependency-status indicator in the main graph view — green/yellow/red driven by `LockfileStatus::Overall`. Clicking opens a panel listing findings with actions (install package, rebuild, open lockfile). Hook into the existing snapshot pipeline in `src/runtime/graph/graph_snapshot_builder.cpp`; the UI side lives near existing dialog/overlay code in `src/ui/dialogs/` and the graph overlay in `src/ui/graph/`.

### Phase 7 — Export gate (strict production mode)

- Add `bool strict_mode` and `std::string lockfile_path` to `ExportOptions` (`src/export/export_pipeline.h:14`).
- In `ExportPipeline::resolve_operators()`, when `strict_mode` is set, call `verify_lockfile`; refuse to export on `Mismatch` with a structured error.
- CLI wiring: `vivid export --strict` forwards the flag.

### Phase 8 — Asset content hashes

Optional but recommended before declaring feature-complete. Asset handlers gain a `content_fingerprint(AssetEntry) -> std::string` hook. Populated fingerprints land in the lockfile; missing ones leave `content_hash` null. This keeps the feature useful for heavy media without blocking v1.

## File Layout Summary

**New files:**

- `src/runtime/packages/project_lockfile.h`
- `src/runtime/packages/project_lockfile.cpp`
- `src/runtime/control/runtime_api_lockfile.cpp`
- `tests/packages/test_project_lockfile.cpp`
- `tests/packages/test_package_git_metadata.cpp`
- `tests/operators/test_operator_descriptor_hash.cpp`

**Modified files:**

- `src/runtime/packages/package_manager.h` — `PackageInfo` fields.
- `src/runtime/packages/package_manager_manifest.cpp` — serialize new fields into/out of the discovery report.
- `src/runtime/packages/package_manager_install.cpp` — capture git commit + URL on install; capture commit + `dirty` flag on link.
- `src/runtime/operators/operator_registry.h` / `.cpp` — descriptor hash computation + accessor.
- `src/runtime/control/runtime_api.h` — declare new methods.
- `src/runtime/control/control_server_dispatch.cpp` — dispatch entries.
- `src/runtime/core/main.cpp` — `lock` / `verify-lock` subcommands.
- `src/runtime/core/runtime_core.cpp` — load-mode plumbing.
- `src/runtime/graph/compiled_graph.h` — add `"locked_unavailable"` reason.
- `src/export/export_pipeline.h` / `.cpp` — strict-mode gate.
- `mcp/vivid_mcp.py`, `mcp/vivid_opdev_mcp.py` — tool registrations.
- `cmake/tests/40-packages-media-misc.cmake` — register new test targets.

## UI and API Behavior

Dependency status indicator reuses existing diagnostic rendering:

- Green: `LockfileStatus::Overall::Match`.
- Yellow: `CompatibleDrift` — compatible package/core updates, `linked_unpinned`, or `descriptor_hash_mismatch` with matching ABI.
- Red: `Mismatch` — missing package/operator/asset, incompatible update, ABI mismatch.

Findings surfaced through MCP and the control server use the stable classifications listed in Phase 3. They align with `PackageUpdateClass` and `missing_operator_reason` so no parallel taxonomy exists.

## Testing

- Lockfile JSON round-trip, key ordering, forward-version rejection.
- Classification matrix: each finding ID has at least one test.
- Load modes (studio, strict, recovery) — studio warns, strict disables affected nodes, recovery reports without blocking.
- Export refusal in strict mode with missing dependencies.
- MCP / control-server response shape contract test.
- Git metadata capture on install (uses a local fixture bare repo).
- Linked package with clean vs. dirty worktree reports `linked_unpinned` correctly.
- Operator descriptor hash stable under rebuild, changes when param/port table changes.

Target registration follows the pattern in `cmake/tests/10-runtime-control-graph.cmake:1-96`; add to `cmake/tests/40-packages-media-misc.cmake`:

```cmake
add_executable(test_project_lockfile tests/packages/test_project_lockfile.cpp)
target_include_directories(test_project_lockfile PRIVATE src tests)
target_link_libraries(test_project_lockfile PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_project_lockfile COMMAND test_project_lockfile
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_project_lockfile PROPERTIES TIMEOUT 15)
```

Run:

```bash
cmake --build build --target test_project_lockfile test_package_manager test_graph
ctest --test-dir build --output-on-failure -R "project_lockfile|package_manager|graph"
```

## Open Questions

1. **Sibling discovery rule.** `vivid.lock` next to the graph, or discovered via walk up to workspace root? Recommendation: next to the graph for v1; revisit when project-directory format lands.
2. **Dirty linked worktrees.** Should `vivid lock` refuse to write when a linked package has uncommitted changes, or record the commit plus a `dirty` flag? Recommendation: record with `dirty: true` and surface as `linked_unpinned`; never block authoring.
3. **GPU shader content.** WGSL source inside packages isn't currently hashed separately. Does `descriptor_hash` need to cover shader source to catch filter edits? Recommendation: include a shader-source hash folded into `descriptor_hash` when the operator is a filter; defer broader GPU hashing.
4. **Network policy.** "Install missing dependencies from the lockfile" — always user-initiated, never on graph open. Confirmed in the proposal, called out here because the UI will want an obvious "install all" affordance.
5. **Auto-update on save.** Should `save_graph` refresh `vivid.lock` automatically? Recommendation: no for v1 — keep lock generation explicit to avoid surprise diffs.

## Acceptance Criteria

- `vivid lock` writes a `vivid.lock` describing exactly the packages, operators, and assets referenced by the given graph.
- `vivid verify-lock` returns a structured `LockfileStatus` and an exit code suitable for CI gating.
- Strict mode prevents silent production drift — export refuses to proceed; affected nodes are disabled with `"locked_unavailable"`.
- Studio mode preserves current fast authoring behavior while surfacing warnings through the existing diagnostic channel.
- MCP, control server, and CLI return the same `LockfileStatus` shape.
- Linked local packages are first-class: lockfile records the absolute path + commit when clean, flags `linked_unpinned` when not.
- No change to `graph.json` schema in v1.
