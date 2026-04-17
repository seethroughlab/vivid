# Project Lockfile — Phase 6a Execution Plan

Scope of this doc: Phase 6a only ("Load Modes + Strict-Mode Enforcement") from [project-lockfile-reproducibility-plan.md](./project-lockfile-reproducibility-plan.md). Phase 6b (UI indicator + findings modal) is deferred. See the other `project-lockfile-phase-*.md` docs for per-phase execution records.

## Context

Phases 0–5 made the lockfile feature reachable and testable but never changed behavior on graph load. The master plan bundles load-mode work (backend) with an in-app UI indicator and findings modal (frontend). Per user answers we split that:

- **Phase 6a (this plan):** backend load modes, `locked_unavailable` per-node disabling, snapshot plumbing.
- **Phase 6b (future):** UI indicator + findings modal.

After Phase 6a: every graph load automatically verifies a sibling `vivid.lock` (when both the lockfile and a PackageManager are available); Strict mode disables affected nodes; every load produces a `LockfileStatus` reachable from `RuntimeCore::lockfile_status()` and present in `GraphSnapshot.lockfile_status` for Phase 6b to render.

## Scope decisions (per user answers)

- **Backend only** — no UI work.
- **Strict semantics:** per-node. Critical findings mark affected `CompiledNode`s with `missing_operator = true`, reason `"locked_unavailable"`. Unaffected nodes continue to execute.
- **Mode selection:** `Settings.lockfile_load_mode` (persisted, default `"studio"`). Callers can override per-load via a trailing `lockfile_mode` parameter on `RuntimeAPI::load_graph` (and the HTTP dispatch that fronts it).

## Deliverable

- `LockfileLoadMode { Studio, Strict, Recovery }` enum + `parse_lockfile_load_mode(str)` / `to_string(mode)` helpers.
- `Settings.lockfile_load_mode` persisted string with validation.
- `RuntimeCore::set_package_manager(PackageManager*)` + `RuntimeCore::lockfile_status()` + `set_lockfile_status()`. `build()` resets status at the top of each build.
- `RuntimeAPI::load_graph(path, .., .., lockfile_mode = "")` accepts an optional mode override.
- Load-path integration inside `RuntimeAPI::load_graph`: after a successful `core_.build()`, discover sibling `vivid.lock`, run `verify_lockfile`, store on `RuntimeCore`. In Strict mode, walk Critical findings and disable matching nodes.
- `apply_strict_mode_to_compiled_graph(status, compiled, registry)` helper that maps findings → node disabling.
- `GraphSnapshot.lockfile_status` populated each frame from `RuntimeCore::lockfile_status()`.
- HTTP `POST /load_graph` accepts optional `lockfile_mode` in the request body.
- `main.cpp` wires `runtime.set_package_manager(&pkg_manager)` once at startup.
- Tests at each layer.
- Four commits on the existing `worktree-project-lockfile` branch.

## Worktree workflow

Continue on `worktree-project-lockfile`. Four commits:

1. **`Add LockfileLoadMode + Settings plumbing for project lockfile`** — enum, parser, `to_string`, Settings field + validation, RuntimeCore setter/getter + reset-on-build. Plus parser / round-trip / RuntimeCore default / set_lockfile_status tests. No behavior change yet.
2. **`Run verify_lockfile on graph load; apply Strict-mode per-node disabling`** — `apply_strict_mode_to_compiled_graph` helper; `load_graph` signature grows the trailing `lockfile_mode` parameter; verify wiring + Strict-mode invocation inserted after `core_.build()`. Tests for Studio / Strict / Strict-non-critical / no-sibling / no-PackageManager.
3. **`Surface LockfileStatus in GraphSnapshot; wire load_graph dispatch override`** — `GraphSnapshot.lockfile_status`, populated by the snapshot builder from `RuntimeCore::lockfile_status()`; control-server dispatch reads optional `lockfile_mode`; `main.cpp` wires `set_package_manager(&pm)`. Test asserts the snapshot propagates the status.
4. **`Add Phase 6a execution plan doc for project lockfile`** — this doc.

## Files

### Modified
- `src/runtime/packages/project_lockfile.h` / `.cpp` — `LockfileLoadMode`, parser, `to_string`, `apply_strict_mode_to_compiled_graph`.
- `src/runtime/core/settings.h` / `.cpp` — `lockfile_load_mode` field + load/save + validation.
- `src/runtime/core/runtime_core.h` / `.cpp` — `PackageManager*` setter, `LockfileStatus` member + getter/setter, reset-on-build.
- `src/runtime/control/runtime_api.h` / `runtime_api_persistence.cpp` — `load_graph` optional trailing parameter; verify wiring + sibling discovery + Strict-mode call.
- `src/runtime/control/control_server_dispatch.cpp` — `load_graph` reads optional `lockfile_mode`.
- `src/runtime/core/main.cpp` — `runtime.set_package_manager(&pkg_manager)`.
- `src/runtime/graph/graph_snapshot_builder.cpp` — populate `snap.lockfile_status`.
- `src/ui/graph/graph_snapshot.h` — `LockfileStatus lockfile_status` top-level field.
- `tests/packages/test_project_lockfile.cpp` — parser, Settings, load-mode, snapshot tests.

### New
- None.

## Data model

```cpp
// src/runtime/packages/project_lockfile.h
enum class LockfileLoadMode { Studio, Strict, Recovery };
LockfileLoadMode parse_lockfile_load_mode(const std::string& s);  // unknown -> Studio
const char*      to_string(LockfileLoadMode mode);

void apply_strict_mode_to_compiled_graph(const LockfileStatus& status,
                                         CompiledGraph& compiled,
                                         const OperatorRegistry& registry);

// src/runtime/core/settings.h
std::string lockfile_load_mode = "studio";  // "studio" | "strict" | "recovery"

// src/runtime/core/runtime_core.h (RuntimeCore additions)
void set_package_manager(PackageManager* pm);
PackageManager* package_manager() const;
const LockfileStatus& lockfile_status() const;
void set_lockfile_status(LockfileStatus s);
```

Studio and Recovery currently behave identically — verify runs, status is stored, nothing is disabled. Recovery exists in the enum now so later phases can layer a "load everything possible" behavior onto it without touching the enum.

## Load-path integration

Inside `RuntimeAPI::load_graph`, after the existing `core_.build(graph_, registry_)` succeeds:

```cpp
LockfileStatus lf_status;
const auto sibling = std::filesystem::path(path).parent_path() / "vivid.lock";
std::error_code ec;
if (std::filesystem::exists(sibling, ec) && !ec && core_.package_manager()) {
    auto load_result = load_lockfile(sibling);
    if (load_result.ok()) {
        lf_status = verify_lockfile(
            load_result.lockfile, graph_, *core_.package_manager(), registry_);
    }
}
core_.set_lockfile_status(lf_status);

const LockfileLoadMode mode = parse_lockfile_load_mode(lockfile_mode);
if (mode == LockfileLoadMode::Strict && core_.compiled_graph()) {
    apply_strict_mode_to_compiled_graph(lf_status, *core_.compiled_graph(), registry_);
}
```

- Any failure in the chain (no PackageManager, no sibling lockfile, corrupt lockfile) leaves `lf_status` at its default (`overall = Match`, empty findings). That's also what `RuntimeCore::build()` resets to at the top of every build, so a failed subsequent load doesn't inherit stale status from a previous load.
- `lockfile_mode == ""` maps to Studio.

## Strict-mode finding mapping

`apply_strict_mode_to_compiled_graph` walks `status.findings` and, for each `severity == Critical`:

| Finding ID                 | Subject meaning    | Effect                                  |
|----------------------------|--------------------|-----------------------------------------|
| `missing_package`          | package name       | Disable every node whose type resolves to that package |
| `incompatible_update`      | package name       | Same                                     |
| `abi_mismatch`             | operator type or `"vivid_core"` | Disable that operator's nodes; `vivid_core` subject is left alone |
| `descriptor_hash_mismatch` | operator type      | Disable that operator's nodes            |
| `missing_operator`         | operator type      | Skipped (compiler already emits `"not_found"`) |

Non-Critical findings (`Info` / `Warning` — e.g. `graph_content_drift`, `linked_unpinned`, `vivid_core_version_mismatch`) are purely informational. A single `linked_unpinned` doesn't freeze the whole graph, matching the product intent ("warn, don't break").

Disabling a node sets `cn.missing_operator = true`, `cn.missing_operator_reason = "locked_unavailable"`, `cn.missing_operator_detail = finding.message`. The existing frame/audio executors already skip nodes with `missing_operator = true`, so no executor changes are needed. `missing_operator_reason` is a free-form string (confirmed in `compiled_graph.h:455`), so `"locked_unavailable"` is additive — no enum update.

## Snapshot integration

`GraphSnapshot.lockfile_status` at the top level. `graph_snapshot_builder.cpp` populates it in a single line just before the `return snap;` at function end:

```cpp
snap.lockfile_status = runtime.lockfile_status();
```

Per-node `missing_operator_reason == "locked_unavailable"` already flows through the existing `cn.missing_operator_reason → snap_node.error_message` pipeline. Phase 6b can special-case the reason string for coloring.

## HTTP dispatch override

The existing `load_graph` branch in `control_server_dispatch.cpp` now reads an optional `lockfile_mode`:

```cpp
const std::string lockfile_mode = root.value("lockfile_mode", std::string());
result = command_result_to_json(
    api.load_graph(root["path"].get<std::string>(),
                   has_gpu_ops, has_audio, lockfile_mode));
```

Backward compatible: omitted field → empty string → Studio.

## main.cpp wiring

One additional line during startup, after both `runtime` (RuntimeCore) and `pkg_manager` (PackageManager) are constructed:

```cpp
runtime.set_package_manager(&pkg_manager);
```

Without this call, `RuntimeCore::package_manager()` returns null and `load_graph` skips verification silently — same behavior as pre-Phase-6a. The call is additive; omitting it is a soft-fail (no crash, no verify).

## Tests

All run under the existing `test_project_lockfile` target.

**Commit 1 (plumbing):**
- `parse_lockfile_load_mode` known values + unknown fallback + case-sensitivity.
- `to_string`/parse round-trip.
- `RuntimeCore::lockfile_status()` default is Match / empty / no PackageManager.
- `RuntimeCore::set_lockfile_status` updates overall + findings.

**Commit 2 (load-path):**
- Studio mode: verify runs, status is Mismatch (via hand-authored `descriptor_hash_mismatch`), NO node disabled.
- Strict mode: same lockfile, assert affected `audio_out` node has `missing_operator = true` and `missing_operator_reason == "locked_unavailable"`.
- Strict ignores non-Critical: a lockfile whose only finding is `graph_content_drift` (Info) leaves the node alone; overall is `CompatibleDrift`.
- No sibling lockfile: `overall == Match` (default), no findings.
- No PackageManager: `overall == Match`, even in Strict mode (verify is skipped silently).

**Commit 3 (snapshot):**
- Snapshot carries the status: build a snapshot after a Strict load against a descriptor-mismatch lockfile, assert `snap.lockfile_status.overall` and `findings.size()` match `RuntimeCore::lockfile_status()`.

### Why descriptor_hash_mismatch instead of abi_mismatch in tests

Built-in operators (registered via `register_builtin_operators`) have `OperatorMapEntry.abi_version == 0` — the ABI is only populated for probed dylibs. The existing `verify_lockfile` ABI check gates on both sides being non-zero, so it can't emit `abi_mismatch` for builtins. Descriptor hashing landed in Phase 0 and works for builtins from the in-memory descriptor, so it's the natural hook for unit tests that want to trigger a Critical operator-type finding without installing a dylib fixture.

## Verification

```bash
cmake --build build --target test_project_lockfile
ctest --test-dir build --output-on-failure -R project_lockfile
```

Manual smoke:
```bash
# 1. Generate a lockfile.
./build/vivid lock --graph graphs/intro/audio_demo.json

# 2. Edit the lockfile to introduce a stale descriptor_hash for one operator.

# 3. Launch the runtime with Settings.lockfile_load_mode = "strict" (or
#    hit POST /load_graph with {"path": "...", "lockfile_mode": "strict"}
#    via the MCP bridge). Affected nodes should show
#    missing_operator = true, reason = "locked_unavailable" in any
#    snapshot dump. The UI indicator itself is Phase 6b.
```

## Acceptance Criteria

- `Settings.lockfile_load_mode` persists and round-trips through JSON with validation; unknown values coerce to `"studio"`.
- `RuntimeCore::set_package_manager(&pm)` is an optional; unset means verify is skipped silently (pre-Phase-6a behavior preserved).
- A graph load with a sibling `vivid.lock` populates `RuntimeCore::lockfile_status()` with `verify_lockfile`'s result.
- In Strict mode, `CompiledNode`s matching Critical findings receive `missing_operator = true`, `missing_operator_reason = "locked_unavailable"`. Non-Critical findings never disable nodes.
- `GraphSnapshot.lockfile_status` is populated every frame.
- `RuntimeAPI::load_graph(path, .., .., "strict")` overrides the Settings field for one load.
- HTTP `POST /load_graph` accepts optional `lockfile_mode`.
- `ctest -R project_lockfile` stays green.
- No new files (Phase 6a is entirely additive to existing modules).
- Four commits on `worktree-project-lockfile` beyond the Phase 5 commits.

## Out of Scope (Phase 6a)

- UI indicator + findings modal — Phase 6b.
- Panel action buttons (install, rebuild, open lockfile) — later.
- Recovery mode gains beyond "treat as Studio" — deferred until a concrete recovery behavior is identified.
- Core `abi_mismatch` subject handling that blocks the whole graph — Phase 6a emits the finding but doesn't globally stop execution; that's a runtime-wide policy decision for later.
- Phase 7 (export strict mode), Phase 8 (asset hashing).
- `vivid_core.commit` build-time macro (deferred from Phase 0).

Phase 6a turns the lockfile from an observer (CLI `verify-lock`) into an enforcer (runtime Strict mode) without adding any UI. Phase 6b will make the enforcement visible in the graph editor.
