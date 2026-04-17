# Project Lockfile and Reproducibility Plan

Status: shipped. All nine phases (0 - 8) landed on `worktree-project-lockfile`. Finalization (CLI asset wiring, core-commit capture, doc alignment) completed 2026-04-17. See the Rollout Notes appendix for deviations from this plan and deferred follow-ups.

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

## Implementation Phases (as shipped)

The feature landed across nine commits/phases on `worktree-project-lockfile`. Per-phase execution plans live alongside this doc under `docs/plans/project-lockfile-phase-*.md`.

| Phase | Subject | Key artifacts |
|-------|---------|---------------|
| 1 | Lockfile model + JSON parser. | `src/runtime/packages/project_lockfile.{h,cpp}`, `tests/packages/test_project_lockfile.cpp`. Pure data module, canonical key order, diff-stable round-trip. |
| 2 | Lockfile generation + RuntimeAPI write path. | `build_lockfile_for_graph`, `RuntimeAPI::write_project_lockfile`. Inspects active graph + registry only. |
| 3 | Verify + classifications + RuntimeAPI read path. | `verify_project_lockfile`, `LockfileStatus` (`Match` / `CompatibleDrift` / `Mismatch` / `NoLockfile`), severity levels (`Info` / `Warning` / `Critical`), finding classifications (`missing_package`, `missing_operator`, `compatible_update`, `incompatible_update`, `linked_unpinned`, `abi_mismatch`, `asset_missing`, `asset_changed`, `descriptor_drift`). |
| 4 | Control-server dispatch + MCP tools. | `/lockfile/write`, `/lockfile/verify`, `/lockfile/status` endpoints and matching MCP tool handlers. All consumers share one `LockfileStatus` object. |
| 5 | CLI subcommands. | `vivid lock`, `vivid verify-lock`, with `--pretty`, `--graph`, `--output`. Exit codes `0` / `1` / `2` / `3` for match / drift / mismatch / io. |
| 0 | Provenance plumbing (landed **after** 5). | Descriptor hashing (`OperatorRegistry::descriptor_hash`), git metadata via `cmake/git_version.cmake` → `VIVID_CORE_COMMIT`. Ordered last because earlier phases did not strictly depend on it. |
| 6a | Strict-mode loader: per-node disabling on critical findings. | `LoadMode::{Studio, Strict, Recovery}` applied before `load_manifest`. |
| 6b | UI indicator + findings modal. | `DialogManager::open_lockfile_findings`, findings banner on the graph view, per-node drift indicator in the inspector. |
| 7 | `vivid export --strict` gate. | `ExportPipeline` checks the lockfile first; returns exit 2 with structured JSON on mismatch. |
| 8 | Asset content hashing. | `asset_changed` classification, `AssetLibrary`-backed sha256 of referenced workspace/package assets. |
| — | Finalization (2026-04-17). | CLI `AssetLibrary` wiring for `vivid lock` / `verify-lock` / `export --strict`; `VIVID_CORE_COMMIT` capture via configure-time git; full ctest pass; doc rewrite. |

### Dependency resolution modes

Three modes landed in Phase 6a, applied at the earliest point in `GraphLoader` (before `load_manifest`):

- **Studio** — default authoring mode. Warnings surface in the findings banner; nothing is disabled.
- **Strict** — used by `vivid export --strict` and the production gate. Critical findings disable affected nodes so the graph cannot silently drift in production.
- **Recovery** — best-effort load, preserves missing-operator placeholders, and produces a complete dependency report for the user to act on.

### Lockfile generation

```bash
vivid lock --graph path/to/graph.json
vivid verify-lock --graph path/to/graph.json [--pretty]
vivid export --strict --graph path/to/graph.json --output <dir>
```

RuntimeAPI methods:

- `write_project_lockfile`
- `verify_project_lockfile`
- `get_project_dependency_status`

Generation inspects the active graph + registry only — never the full installed package set. Only what the graph actually references is captured.

### Package install integration

Install integration is **out of scope** for this feature. The lockfile surfaces what is missing and why; installing is the user's action via existing package flows. Lockfile findings expose source URLs and commits when known, but `verify-lock` will not trigger network work on its own.

### Asset integration

Asset capture landed in Phase 8. The lockfile records assets referenced by graph params whose descriptors declare `asset_kind`. The `AssetLibrary` provides sha256 content hashes for workspace and package assets. `asset_changed` is a Warning, not a Critical — a content-hash drift should not freeze a graph in strict mode because benign edits are the norm.

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

## Rollout Notes

Tracks deviations from the original plan, known gotchas, and deferred follow-ups surfaced during implementation.

### Deviations from the original plan

- **Phase ordering.** Phase 0 (provenance plumbing) originally ran first but landed last — Phase 1 did not need descriptor hashing or git metadata to produce a valid lockfile, so shipping 1 - 5 first kept each phase small and reviewable.
- **Phase 6 split.** The "load modes + UI" phase split into **6a** (strict-mode per-node disabling in the loader) and **6b** (findings banner + modal). 6a is a pure behavior change; 6b is pure UI. Splitting kept each review focused.
- **Strict gate placement.** `vivid export --strict` runs the lockfile check **before** `load_manifest` (the earliest possible point), not before `resolve_operators` as the plan suggested. Running earlier means strict-mode failures are reported without any partial graph construction happening first.
- **`asset_changed` severity.** Landed as Warning rather than Critical. Content hash drift on an asset (a wavetable edit, a movie re-render) is benign during authoring; freezing the graph would fight normal use. Strict export still tolerates `asset_changed`.
- **Descriptor hashing.** Uniform sha256 over the serialized `VividOperatorDescriptor` rather than a per-`AssetKindHandler` hook. Simpler, stable, and good enough for silent drift detection.
- **CLI `AssetLibrary` wiring.** Deferred to finalization. Initially the three CLI subcommands left `AssetLibrary` unset on their local `PackageManager`, so `assets[]` from the CLI was silently empty. Fixed in the 2026-04-17 finalization pass.

### Known gotchas

- Built-in (seed) operators carry `OperatorMapEntry.abi_version == 0`. The ABI-mismatch classification tolerates zero for built-ins and compares exact values for installed packages.
- `vivid_core` findings (e.g., `abi_mismatch` on the core subject) are intentionally not node-disabling in strict mode — a core-level drift freezes the whole graph, which is worse than surfacing the warning and letting the user choose.
- `VIVID_CORE_COMMIT` falls back to empty string on non-git builds (release tarballs, third-party distros). Don't compare `lf.vivid_core.commit` as a strict equality when it's empty on either side.
- Lockfile generation holds the `RuntimeAPI` mutex. Do not call it from inside another `RuntimeAPI` method on the same thread.

### Deferred follow-ups

Tracked for later work; not blocking the v1 release.

- **Per-`AssetKindHandler` fingerprint hook.** Today we sha256 the asset bytes. Kind-specific metadata (sample rate, frame count, codec) would produce more actionable drift messages.
- **Streaming sha256.** Large assets are read into RAM before hashing. A streaming implementation would make the lockfile safe for multi-GB samples.
- **WGSL shader source hashing.** GPU operator descriptors don't include shader source, so hot-edited filters can drift silently. Hash shader source alongside the descriptor.
- **Richer Recovery mode.** Today Recovery preserves missing-operator placeholders. A curated "drift dashboard" with explicit remediation affordances (install this package, rebuild that operator, open lockfile) would close the loop.
- **Modal action buttons.** The findings modal reports drift but doesn't act on it. Action buttons for `install`, `rebuild`, and `open-lockfile` would make remediation one-click.
- **`linked_unpinned` integration tests.** Unit coverage exists; end-to-end coverage of linked-package drift does not. Add when a linked-package regression suite is justified.
- **Workspace-level lockfiles.** Today the lockfile lives next to a single graph. A project-root `vivid.lock` that aggregates multiple graphs would support directory-project layouts once that format exists.

