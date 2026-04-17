# Project Lockfile — Phase 0 Execution Plan

Scope of this doc: Phase 0 only ("Provenance Capture") from [project-lockfile-reproducibility-plan.md](./project-lockfile-reproducibility-plan.md). Builds on Phases 1–4 (see the other `project-lockfile-phase-*.md` docs). This phase lands after Phase 4, despite its "0" numbering, because it fills in the data that earlier phases left as empty strings.

## Context

Phases 1–4 built the full lockfile pipeline (generate, verify, RuntimeAPI, HTTP/MCP dispatch) but deferred the actual provenance data. A generated `vivid.lock` emitted:

```json
"source": {"kind": "git", "url": "", "commit": ""},
"operators": [{"descriptor_hash": "", "type": "...", ...}]
```

So the lockfile was a skeleton — it named the packages and operators in use but couldn't pin a specific commit or catch silent operator drift. Phase 0 closes that gap:

- Capture git commit + remote URL + dirty flag for each installed/linked package.
- Fingerprint each operator's public descriptor surface (name, params, ports, flags).
- Wire both into `build_lockfile_for_graph`.
- Flip on Phase 3's two deferred verify findings: `linked_unpinned` and `descriptor_hash_mismatch`.

After this phase, `vivid.lock` is a meaningful artifact: a clean environment with the same packages at the same commits reproduces the same graph. `verify_lockfile` catches the drift cases that matter most in practice.

## Scope decisions (per user answers)

- **All three sub-tasks in one phase:** PackageInfo fields + git capture + descriptor hash.
- **Wire up deferred findings:** `linked_unpinned` and `descriptor_hash_mismatch` start emitting as part of this phase.
- **Descriptor hash scope:** descriptor struct only (name, params, ports, flags). WGSL shader source deferred.
- **`vivid_core.commit` deferred:** no compile-time macro exists yet; the field stays empty in Phase 0.

## Deliverable

- `PackageInfo` extended with three provenance fields: `std::string source_url`, `std::string git_commit`, `bool dirty`.
- `capture_git_metadata(repo_dir, info)` helper (in `namespace vivid::package_manager_internal`) that reads commit/url/dirty via ProcessRunner. Defensive: any failure leaves fields empty and returns silently.
- Call sites: `PackageManager::install` (after clone/copy), `PackageManager::link` (on the linked tree), and `PackageManager::scan_installed` (on each discovered package dir so existing installations retroactively acquire provenance on app startup).
- `operator_descriptor_hash(const VividOperatorDescriptor*)` in a new standalone TU + `OperatorRegistry::descriptor_hash(type_name)` wrapper.
- `build_lockfile_for_graph` populates `source.url`/`source.commit`/`source.kind` from captured data and `operator.descriptor_hash` from the registry.
- `verify_lockfile` emits `kLinkedUnpinned` (linked package with empty commit or `dirty = true`) and `kDescriptorHashMismatch` (non-empty lockfile hash differs from the registry's current hash). Skip path for empty lockfile hash preserves backward compatibility with pre-Phase-0 lockfiles.
- Tests across three layers.
- Four commits on the existing `worktree-project-lockfile` branch.

## Worktree workflow

Continue on `worktree-project-lockfile`. Four commits:

1. **`Add provenance fields to PackageInfo + git metadata capture`** — struct additions + `capture_git_metadata` helper + wiring into install/link/scan. Tests via `init_git_repo_at` fixture that drives real git commands.
2. **`Add operator descriptor hashing`** — new `operator_descriptor_hash.{h,cpp}` + registry accessor. Standalone tests for stability, null-safety, and sensitivity to each descriptor field.
3. **`Fill provenance and descriptor_hash in lockfile generation; emit new verify findings`** — flip the TODO comments in `build_lockfile_for_graph` and `verify_lockfile` from stub behavior to real wire-up. Tests for each new finding.
4. **`Add Phase 0 execution plan doc for project lockfile`** — this doc.

If (2) needs to land independently of (1), it can — the descriptor hashing is entirely operator-side. (3) depends on both.

## Files

### Modified
- `src/runtime/packages/package_manager.h` — `PackageInfo` fields.
- `src/runtime/packages/package_manager_internal.h` — `capture_git_metadata` declaration.
- `src/runtime/packages/package_manager_install.cpp` — helper implementation + install/link capture.
- `src/runtime/packages/package_manager_discovery.cpp` — capture during scan.
- `src/runtime/operators/operator_registry.h` — `descriptor_hash` accessor declaration.
- `src/runtime/operators/operator_registry.cpp` — accessor implementation.
- `src/runtime/packages/project_lockfile.cpp` — wire real provenance into generation; flip on new verify findings.
- `tests/packages/test_project_lockfile.cpp` — new tests for git capture, descriptor-hash wire-up, and descriptor-hash-mismatch finding.
- `cmake/app.cmake`, `cmake/tests.cmake` — register `operator_descriptor_hash.cpp`.
- `cmake/tests/40-packages-media-misc.cmake` — register `test_operator_descriptor_hash`.

### New
- `src/runtime/operators/operator_descriptor_hash.h` / `.cpp` — standalone descriptor canonicalizer + `sha256_hex`.
- `tests/operators/test_operator_descriptor_hash.cpp` — focused tests for the canonicalizer.

## PackageInfo extension

```cpp
struct PackageInfo {
    // ... existing fields ...

    // Provenance (populated at install/link/scan time; not read from the
    // manifest). Empty when the package isn't a Git worktree or git is
    // unavailable. `dirty` meaningful only when git_commit is set.
    std::string source_url;
    std::string git_commit;
    bool dirty = false;
};
```

`source.kind` inference in `build_lockfile_for_graph`:
- `linked` → `"local"` (regardless of whether the tree is a git repo)
- `!linked && !source_url.empty()` → `"git"` (installed from a git URL)
- otherwise → `"local"` (installed from a bare local copy)

## Git metadata capture

Helper in `namespace vivid::package_manager_internal`:

```cpp
void capture_git_metadata(const std::string& repo_dir, PackageInfo& info);
```

Implementation uses ProcessRunner (argv-based, no shell) with a 5-second timeout per call:

- `git -C <dir> rev-parse HEAD` → 40-char sha written to `info.git_commit`.
- `git -C <dir> config --get remote.origin.url` → written to `info.source_url` (optional).
- `git -C <dir> status --porcelain` → non-empty output sets `info.dirty = true`.

Any failure (non-git dir, missing git binary, permission issue) leaves fields at defaults. Never errors.

Call sites:
- `PackageManager::install` after rename-to-final-path — fresh clones always have a clean worktree.
- `PackageManager::link` after canonicalizing the linked path — captures the linked tree's state.
- `PackageManager::scan_installed` via `package_manager_discovery.cpp` — each discovered package directory gets captured, so existing installs populate provenance on startup without needing re-install.

## Descriptor hashing

Canonical text form (not persisted, not user-facing — only the sha matters):

```
name=<operator_name>
flags=time:<0|1> audio:<0|1> gpu:<0|1> frame:<0|1> lane_behavior:<n> strategy_independent:<0|1>
params=<count>
param[0]: name=<n> type=<t> default=<d> min=<mn> max=<mx> display_hint=<h> tag=<tag> shape=<shape> unit=<u> intent=<i> asset_kind=<ak> widget_id=<w> default_string=<ds>
param[0].choices: <count>,<c0>,<c1>,...
...
ports=<count>
port[0]: name=<n> type=<t> direction=<d> transport=<t> channels=<c> default=<d> payload_size=<p> type_name=<tn> stable_type_id=<id> tag=<tag> shape=<shape> intent=<i>
...
```

SHA-256 of that string, prefixed with `"sha256:"`. Same format as `canonicalize_graph_hash`.

What it catches: renamed/added/removed params, type changes, default-value changes, semantic-metadata edits, flag flips, port direction or transport changes.

What it ignores by design: dylib path, ABI version (has its own `operator_abi` field), source-file whitespace, compiler version, shader source (Phase 0 scope).

The canonicalizer lives in its own TU (`src/runtime/operators/operator_descriptor_hash.cpp`) so it can be unit-tested with hand-built `VividOperatorDescriptor` structs without instantiating a full registry.

`OperatorRegistry::descriptor_hash(type_name)` is a thin wrapper: calls `probe_descriptor(type_name)`, returns empty for unknown types.

## Lockfile generation wire-up

In `build_lockfile_for_graph`:

```cpp
p.source.kind = info.linked
    ? "local"
    : (!info.source_url.empty() ? "git" : "local");
p.source.url    = info.source_url;
p.source.commit = info.git_commit;

o.descriptor_hash = operator_registry.descriptor_hash(type_name);
```

`vivid_core.commit` stays empty — no compile-time macro yet. Deferred to a later phase.

## Verify wire-up

In `verify_lockfile`:

```cpp
// After the package version-compatibility block:
if (info.linked && (info.git_commit.empty() || info.dirty)) {
    add(kLinkedUnpinned, LockfileSeverity::Warning, p.name,
        info.git_commit.empty()
            ? "linked from a non-git path (no commit to pin)"
            : "linked worktree has uncommitted changes",
        "commit changes or install from a stable source");
}

// After the operator ABI block:
if (!o.descriptor_hash.empty()) {
    const std::string current = operator_registry.descriptor_hash(o.type);
    if (!current.empty() && current != o.descriptor_hash) {
        add(kDescriptorHashMismatch, LockfileSeverity::Critical, o.type,
            "operator descriptor changed since lockfile was written",
            entry.package_name.empty()
                ? "rebuild core"
                : "rebuild " + entry.package_name);
    }
}
```

Empty-hash skip: if the lockfile side has `descriptor_hash == ""` (pre-Phase-0 lockfile), skip the check. Same pattern used for `operator_abi == 0` in existing verify logic. Prevents false positives when old lockfiles are loaded by new runtimes.

## Tests

New tests in `tests/packages/test_project_lockfile.cpp`:

- **capture_git_metadata** (three cases) — clean repo: commit + url populated, not dirty. Dirty repo: `dirty = true`. Non-git dir: all fields empty. Uses `init_git_repo_at` helper (driven by real `git` binary via ProcessRunner). Each test SKIPs cleanly if git isn't on PATH.
- **build_lockfile populates descriptor_hash** — register built-in operators, add `audio_out` to the graph, assert `operators[0].descriptor_hash` starts with `"sha256:"` and is 71 chars long.
- **verify emits descriptor_hash_mismatch** — hand-build a lockfile with `audio_out` and a stale descriptor_hash, verify → `kDescriptorHashMismatch` Critical → `overall = Mismatch`.
- **verify skips descriptor_hash_mismatch when empty** — lockfile's hash is "" → no finding, proving the backward-compat skip path.

New file `tests/operators/test_operator_descriptor_hash.cpp`:

- **deterministic** — same descriptor → same hash across calls.
- **null-safe** — `operator_descriptor_hash(nullptr)` returns empty.
- **sensitivity** — changes with operator name, param rename, param default, port type, capability flag, added param. Seven variants, each asserting a different dimension of the canonical form.

All registered under the existing `PACKAGE` CMake label.

`linked_unpinned` has no dedicated integration test — would require a real `pm.link()` fixture with a non-git dir. The logic is trivial (two equality checks) and is exercised whenever a dev links a local package and runs verify. A follow-up integration test could be added later if false positives/negatives show up.

## Verification

```bash
cmake --build build --target test_project_lockfile test_operator_descriptor_hash
ctest --test-dir build --output-on-failure -R "project_lockfile|operator_descriptor_hash"
```

Background per user preference. No end-to-end MCP smoke is needed — Phase 0 is strictly data-capture; its surface is exercised through the same HTTP/MCP endpoints added in Phase 4.

## Acceptance Criteria

- `PackageInfo` carries `source_url`, `git_commit`, `dirty` and they're populated by install/link/scan.
- Installing a package that lives in a git repo produces a `PackageInfo` with both `source_url` and `git_commit` set.
- Linking a non-git dir leaves fields empty; linking a dirty git worktree sets `dirty = true`.
- `OperatorRegistry::descriptor_hash(type)` returns `"sha256:<hex>"` for a registered type and empty otherwise.
- `build_lockfile_for_graph` produces lockfiles whose `packages[*].source` fields and `operators[*].descriptor_hash` are populated whenever the underlying data exists.
- `verify_lockfile` emits `kLinkedUnpinned` for dirty/unpinned linked packages and `kDescriptorHashMismatch` for operator drift; both branches are skipped when the corresponding data is empty.
- `ctest -R "project_lockfile|operator_descriptor_hash"` stays green.
- Four commits on `worktree-project-lockfile` beyond the Phase 4 commits.
- No changes outside the "Modified" and "New" lists.

## Out of Scope (Phase 0)

- `vivid_core.commit` — needs a build-time git macro (`cmake/git_version.cmake`); deferred.
- WGSL shader source hashing — descriptor-only for v1.
- C++ header fingerprinting / dylib-content hashing — over-aggressive; not useful signal.
- Retroactive population of already-generated lockfiles — users re-run `write_project_lockfile` to refresh.
- Phase 5 (CLI), Phase 6 (UI + load modes), Phase 7 (export strict mode), Phase 8 (asset hashing).

## Rationale for deferring `vivid_core.commit`

The lockfile already carries `vivid_core.version` (from `VIVID_CORE_VERSION` macro) and `operator_abi` (from `VIVID_OPERATOR_ABI_VERSION`). Together those identify the core well enough for Phase 0's "reproducibility of the graph" promise. A separate commit hash would help when diagnosing dev-build divergence but adds build-system complexity (cmake script running `git rev-parse HEAD` at configure time + rebuild-on-commit considerations). Left for a small follow-up change if/when the need shows up.
