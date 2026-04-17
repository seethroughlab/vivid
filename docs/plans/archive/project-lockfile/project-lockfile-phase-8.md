# Project Lockfile — Phase 8 Execution Plan

Scope of this doc: Phase 8 only ("Asset Content Hashing") from [project-lockfile-reproducibility-plan.md](./project-lockfile-reproducibility-plan.md). Final phase in the master plan.

## Context

Phases 0–7 covered packages (content + commits) and operators (descriptor hashes) end-to-end. `ProjectLockfile.assets[]` was declared in Phase 1 but always emitted empty — Phase 2 deferred asset tracking, and `verify_lockfile` had `kAssetMissing` / `kAssetChanged` finding IDs declared but never emitted.

Phase 8 closes that loop. `vivid lock` now captures sha256 of every asset-bearing param's resolved file. `vivid verify-lock` emits `asset_missing` (Critical) and `asset_changed` (Warning) findings. The Phase 6b GUI findings modal surfaces them unchanged. The Phase 7 export gate stays intact — asset findings map to a lower severity tier so an intentionally edited sample doesn't fail strict CI, while a deleted dependency still does.

## Scope decisions (per user answers)

- **Full scope:** enumeration + hashing + generation + verify.
- **Which params count:** `VividParamDescriptor.asset_kind != nullptr` only. Explicit opt-in at the operator level.
- **Hashing strategy:** uniform sha256 over raw file bytes (`sha256_file` new helper, chunked 8 KB reader). Per-handler fingerprinting deferred.
- **`asset_changed` severity:** Warning (not Critical). Strict export tolerates benign content edits; a missing file remains Critical.

## Deliverable

- `std::string sha256_file(const std::filesystem::path&)` in `src/common/hash_util.{h,cpp}`.
- `PackageManager::asset_library() const` getter mirroring the existing setter.
- `build_lockfile_for_graph` walks node descriptors, enumerates asset-bearing params, resolves values via AssetLibrary + filesystem, hashes reachable files, populates a deduplicated + path-sorted `assets[]`. Gated on `pm.asset_library() != nullptr` for backward compat.
- `verify_lockfile` iterates locked `assets[]` and emits `kAssetMissing` (Critical) + `kAssetChanged` (Warning). Skips the drift check when the lockfile-side `content_hash` is empty (pre-Phase-8 lockfiles verify cleanly).
- Tests at each layer.
- Three commits on `worktree-project-lockfile`.

## Commit layout (as landed)

1. **`Add asset enumeration and content hashing to lockfile generation`** — `sha256_file` + PackageManager getter + generation pass + 7 tests (sha256_file primitives, null-AssetLibrary backward compat, populated content_hash, missing-file graceful degrade, cross-node dedup, insertion-order-independent sort).
2. **`Emit asset_missing and asset_changed findings in verify_lockfile`** — verify loop with asset_id → canonical_path re-resolution via AssetLibrary; empty-content_hash skip for pre-Phase-8 lockfiles; 4 verify tests (missing, changed, untouched, pre-Phase-8 skip).
3. **`Add Phase 8 execution plan doc for project lockfile`** — this doc.

## Files

### Modified
- `src/common/hash_util.h` / `.cpp` — `sha256_file(path)` chunked reader (8 KB). Returns empty string on I/O error so callers can treat "" as "no hash available."
- `src/runtime/packages/package_manager.h` — `asset_library()` accessor.
- `src/runtime/packages/project_lockfile.cpp` — asset-enum pass in `build_lockfile_for_graph`; asset-verify loop in `verify_lockfile`.
- `tests/packages/test_project_lockfile.cpp` — hand-built AssetTestOp operator descriptor with one `asset_kind` param + two non-asset distractor params; 7 generation + 4 verify tests + 2 sha256_file tests.

### New
- None.

## Enumeration algorithm

In `build_lockfile_for_graph`, after the operators loop:

```cpp
if (AssetLibrary* al = package_manager.asset_library()) {
    std::filesystem::path graph_dir =
        std::filesystem::path(graph.source_path()).parent_path();
    std::set<std::string> seen_paths;

    for (const auto& node : graph.nodes()) {
        const VividOperatorDescriptor* desc =
            operator_registry.probe_descriptor(node.type);
        if (!desc) continue;
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            const VividParamDescriptor& p = desc->params[i];
            if (!p.asset_kind || !*p.asset_kind) continue;
            auto it = node.string_params.find(p.name ? p.name : "");
            if (it == node.string_params.end() || it->second.empty()) continue;

            std::filesystem::path path;
            std::string asset_id;
            if (const AssetEntry* entry = al->find(it->second)) {
                path     = entry->canonical_path;
                asset_id = entry->asset_id;
            } else {
                std::filesystem::path cand = it->second;
                if (cand.is_relative() && !graph_dir.empty()) cand = graph_dir / cand;
                path = cand.lexically_normal();
            }
            if (path.empty()) continue;
            if (!seen_paths.insert(path.string()).second) continue;

            LockfileAsset la;
            la.asset_id = asset_id;
            la.kind     = p.asset_kind;
            la.path     = path.string();
            std::error_code ec;
            if (std::filesystem::exists(path, ec) && !ec) {
                const std::string hex = sha256_file(path);
                if (!hex.empty()) la.content_hash = "sha256:" + hex;
            }
            lf.assets.push_back(std::move(la));
        }
    }
    std::sort(lf.assets.begin(), lf.assets.end(),
              [](const LockfileAsset& a, const LockfileAsset& b) {
                  return a.path < b.path;
              });
}
```

Resolution order: AssetLibrary first (value might be an asset_id), falling back to filesystem path relative to the graph's directory. Empty content_hash is a graceful-degrade signal: the asset is named in the lockfile but we couldn't hash it.

## Verify algorithm

```cpp
AssetLibrary* al = package_manager.asset_library();
for (const auto& a : lockfile.assets) {
    std::filesystem::path path = a.path;
    if (!a.asset_id.empty() && al) {
        if (const AssetEntry* entry = al->find(a.asset_id)) {
            path = entry->canonical_path;
        }
    }
    if (!std::filesystem::exists(path)) {
        add(kAssetMissing, LockfileSeverity::Critical, a.path,
            "asset file is no longer reachable",
            "restore " + a.path + " or re-lock");
        continue;
    }
    if (a.content_hash.empty()) continue;  // pre-Phase-8 skip
    const std::string current = "sha256:" + sha256_file(path);
    if (current != a.content_hash) {
        add(kAssetChanged, LockfileSeverity::Warning, a.path,
            "asset content changed since lockfile was written",
            "re-lock or restore the original " + a.path);
    }
}
```

The asset_id → canonical_path re-resolution step is defensive: if a user moves a wavetable registration (without changing its identity), the lockfile's asset_id still points at the right file. Phase 2 generation stores asset_id when AssetLibrary resolves; older lockfiles without asset_id fall back to the raw path.

## Severity rationale

- `asset_missing` = **Critical**: a file the runtime can't find. Same tier as `missing_operator` / `missing_package`. Strict export blocks.
- `asset_changed` = **Warning**: file exists but differs. Overall goes to `CompatibleDrift`. Strict export still passes. Rationale: intentional asset edits (a user re-exporting a wavetable) shouldn't brick CI. If the user wants hard-fail-on-any-drift, they can use the verify-lock CLI separately.

## `sha256_file` helper

```cpp
std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::string bytes;
    bytes.reserve(64 * 1024);
    char buf[8192];
    while (ifs) {
        ifs.read(buf, sizeof(buf));
        auto n = ifs.gcount();
        if (n > 0) bytes.append(buf, static_cast<size_t>(n));
    }
    if (ifs.bad()) return {};
    return sha256_hex(bytes);
}
```

Reads in 8 KB chunks, accumulates to memory, defers to Phase 2's `sha256_hex`. No streaming sha256 state machine yet — if media files > available RAM show up, a follow-up can wire a chunked transform.

## Tests

### sha256_file (2)
1. **matches sha256_hex** — write known bytes; hash-of-file == hash-of-buffer.
2. **missing file returns empty** — non-existent path returns `""`, doesn't throw.

### Generation (5)
3. **no AssetLibrary → empty assets** — `pm.asset_library() == nullptr`; enumeration is a no-op. Backward compat for pre-Phase-8 callers.
4. **asset_kind param populates content_hash** — AssetTestOp has `"wavetable"` asset_kind; real file at the pointed path gets `sha256:...` hash.
5. **missing file → entry with empty content_hash** — same fixture but the path is `/does/not/exist.wav`. Entry still written (so verify can later emit `asset_missing`), but the hash is empty.
6. **dedup across nodes** — three nodes, same wavetable path → one asset entry.
7. **sort stability** — two graphs with same 3 assets in different insertion orders → identical assets[] sequence.

### Verify (4)
8. **asset_missing** — lockfile points at a non-existent path → `kAssetMissing` Critical → overall `Mismatch`.
9. **asset_changed** — file exists but hashes to a different value → `kAssetChanged` Warning → overall `CompatibleDrift`.
10. **asset untouched** — file's sha256 matches the lockfile → no findings.
11. **empty content_hash skips check** — pre-Phase-8 lockfile entry (`content_hash == ""`) with a real file that hashes to anything → no `asset_changed` finding.

All tests use a file-local `asset_fixture::register_asset_test_op` that calls `OperatorRegistry::register_builtin` with a hand-built `VividOperatorDescriptor` — same technique used by `register_builtin_operators` for `audio_out`/`video_out`, so `probe_descriptor("AssetTestOp")` returns the descriptor the enumeration relies on.

## Verification

```bash
cmake --build build --target test_project_lockfile
ctest --test-dir build --output-on-failure -R project_lockfile
```

Manual smoke (requires a graph that actually uses a wavetable operator):

```bash
./build/vivid lock --graph graphs/intro/audio_demo.json
# Inspect <graph_dir>/vivid.lock: .assets array has entries with content_hash

# Touch a wavetable file.
./build/vivid verify-lock --graph graphs/intro/audio_demo.json --pretty
# Expect WARN asset_changed; overall compatible_drift.

# Strict export still succeeds (Warning is below the Mismatch threshold).
./build/vivid export --strict --graph graphs/intro/audio_demo.json --output demo
# exit 0
```

## Acceptance Criteria

- `build_lockfile_for_graph` populates `lf.assets[]` for every asset-bearing param (`asset_kind != nullptr`) in the graph; files with no sha256 (missing or unreadable) leave `content_hash` empty.
- Assets are sorted by path and deduplicated across nodes.
- `verify_lockfile` emits `kAssetMissing` (Critical) when a locked asset path no longer exists, and `kAssetChanged` (Warning) when the content_hash drifts.
- Backward compat: pre-Phase-8 lockfiles (empty content_hash) and PMs without an AssetLibrary produce the same behavior as before this phase.
- Strict export (Phase 7) still passes when only `asset_changed` findings are present.
- `ctest -R project_lockfile` stays green.
- Three commits on `worktree-project-lockfile` beyond the Phase 7 commits.

## Out of Scope (Phase 8)

- Per-`AssetKindHandler` `content_fingerprint` hook — uniform file-bytes sha256 in v1.
- Streaming sha256 for files > available RAM.
- Hashing assets not referenced by the graph (e.g. full workspace asset library).
- Tracking file params without `asset_kind` (e.g. config files, user media dropped via generic file pickers).
- GUI "re-lock" affordance after `asset_changed` findings — the findings modal already surfaces the text via Phase 6b.
- CLI asset-library bootstrap for `vivid lock` / `vivid verify-lock` / `vivid export --strict` — the plan mentioned this, but since PackageManager::set_asset_library is already what callers use to wire AssetLibrary, the existing main.cpp path works once the GUI / runtime wires one up. CLI handlers construct their own PM today; they'd need an AssetLibrary local too for full symmetry. Deferred as a small follow-up if/when a CLI user reports that `vivid lock --graph …` didn't capture assets.

## Wrap-up

Phase 8 is the final master-plan phase. The `vivid.lock` format now captures:

- Core version and operator ABI (Phase 2).
- The graph's canonical content hash (Phase 2).
- Installed packages + their git commits (Phase 0).
- Operator descriptor fingerprints (Phase 0).
- Assets referenced by the graph, with sha256 content hashes (Phase 8).

Verification classifications (`match` / `missing_package` / `missing_operator` / `compatible_update` / `incompatible_update` / `abi_mismatch` / `vivid_core_version_mismatch` / `graph_content_drift` / `linked_unpinned` / `descriptor_hash_mismatch` / `asset_missing` / `asset_changed`) are all emitted. Reproducibility, as defined by the master plan, is feature-complete.
