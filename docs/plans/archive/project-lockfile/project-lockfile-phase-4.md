# Project Lockfile — Phase 4 Execution Plan

Scope of this doc: Phase 4 only ("Control-Server Dispatch + MCP") from [project-lockfile-reproducibility-plan.md](./project-lockfile-reproducibility-plan.md). Builds on Phase 3 ([project-lockfile-phase-3.md](./project-lockfile-phase-3.md)). CLI subcommands, UI surfaces, export gate, and asset hashing are out of scope.

## Context

Phases 1–3 delivered the lockfile module and three RuntimeAPI methods (`write_project_lockfile`, `verify_project_lockfile`, `get_project_dependency_status`). None of them are reachable from outside the runtime yet — the HTTP control server doesn't route them, and the MCP bridge has no tool wrappers. Phase 4 closes that gap.

By the end of this phase a user can:
- `POST /write_project_lockfile` → `{"ok": true, "message": <absolute path>}` to produce a `vivid.lock`
- `POST /verify_project_lockfile` or `/get_project_dependency_status` → `{"ok": true, "status": {...}}` with an inline, single-parse JSON payload
- Call the same three methods as MCP tools from `vivid_mcp.py`

`vivid_opdev_mcp.py` is intentionally skipped — lockfile is user-facing per the MCP split (`mcp/CLAUDE.md`).

## Deliverable

- Three `else if (method == "...")` branches in `src/runtime/control/control_server_dispatch.cpp` (next to `save_graph`).
- One inline helper `unwrap_status_to_json(CommandResult)` in `src/runtime/control/control_server_internal.h` alongside `command_result_to_json`. Parses `CommandResult.message` as JSON on success and inlines it as `status`; falls through to the standard `{"ok": false, "error": ...}` shape on failure.
- Three `@mcp.tool()` wrappers in `mcp/vivid_mcp.py` modeled on `save_graph` (`mcp/vivid_mcp.py:753-761`).
- Dispatch-level tests in the existing `test_project_lockfile` target — the new logic that warrants direct coverage is `unwrap_status_to_json`; the branch wiring itself is thin and delegates to already-tested RuntimeAPI methods.
- No unit tests for MCP wrappers (the bridge isn't unit-tested in this repo); manual smoke via `scripts/mcp_bridge_smoke.py` suffices.
- Three commits on `worktree-project-lockfile`.

## Worktree Workflow

Continue in `.claude/worktrees/project-lockfile` on branch `worktree-project-lockfile`. Three commits:

1. **`Add control-server dispatch for project lockfile`** — the three branches + `unwrap_status_to_json` helper + unit tests for the helper.
2. **`Add MCP tools for project lockfile`** — the three `@mcp.tool()` wrappers in `vivid_mcp.py`.
3. **`Add Phase 4 execution plan doc for project lockfile`** — this doc.

If (2) runs into Python tooling surprises, (1) stands alone.

## Files

### Modified
- `src/runtime/control/control_server_internal.h` — add the `unwrap_status_to_json` inline helper.
- `src/runtime/control/control_server_dispatch.cpp` — add three new method branches between `save_graph` and `load_graph`.
- `mcp/vivid_mcp.py` — add three `@mcp.tool()` wrappers near the persistence tools.
- `tests/packages/test_project_lockfile.cpp` — three unit tests covering `unwrap_status_to_json`.

### New
- None.

## Dispatch wiring

`dispatch()` already takes `PackageManager* package_manager` as a parameter (`control_server_dispatch.cpp:20`), so each branch just extracts fields from the request body and calls the corresponding RuntimeAPI method.

The helper (`control_server_internal.h`):

```cpp
inline std::string unwrap_status_to_json(const CommandResult& r) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    if (!r.ok) {
        out["ok"]    = false;
        out["error"] = r.message;
        return out.dump();
    }
    out["ok"] = true;
    try {
        out["status"] = nlohmann::json::parse(r.message);
    } catch (const nlohmann::json::exception&) {
        out["status"] = r.message;  // defensive fallback
    }
    return out.dump();
}
```

The three dispatch branches:

```cpp
} else if (method == "write_project_lockfile") {
    if (!root_valid)                 result = json_err("invalid JSON body");
    else if (!package_manager)       result = json_err("no package manager available");
    else {
        const std::string graph_path  = root.value("graph_path", std::string());
        const std::string output_path = root.value("output_path", std::string());
        result = command_result_to_json(
            api.write_project_lockfile(*package_manager, graph_path, output_path));
    }
} else if (method == "verify_project_lockfile") {
    if (!root_valid)                 result = json_err("invalid JSON body");
    else if (!package_manager)       result = json_err("no package manager available");
    else {
        const std::string graph_path    = root.value("graph_path", std::string());
        const std::string lockfile_path = root.value("lockfile_path", std::string());
        result = unwrap_status_to_json(
            api.verify_project_lockfile(*package_manager, graph_path, lockfile_path));
    }
} else if (method == "get_project_dependency_status") {
    if (!root_valid)                 result = json_err("invalid JSON body");
    else if (!package_manager)       result = json_err("no package manager available");
    else {
        const std::string graph_path = root.value("graph_path", std::string());
        result = unwrap_status_to_json(
            api.get_project_dependency_status(*package_manager, graph_path));
    }
}
```

## Response shapes

**`write_project_lockfile`** (standard):
```json
{"ok": true, "message": "/abs/path/to/vivid.lock"}
```

**`verify_project_lockfile` / `get_project_dependency_status`** (unwrapped, single-parse):
```json
{
  "ok": true,
  "status": {
    "overall": "match",
    "findings": []
  }
}
```

**Errors (all three):**
```json
{"ok": false, "error": "..."}
```

## MCP wrappers

Inserted next to `save_graph` / `load_graph` / `new_graph` in `mcp/vivid_mcp.py`:

```python
@mcp.tool()
async def write_project_lockfile(graph_path: str,
                                 output_path: str | None = None) -> str:
    resolved = _resolve_graph_path(graph_path)
    body: dict = {"graph_path": resolved}
    if output_path is not None:
        body["output_path"] = output_path
    return await _post("write_project_lockfile", body)


@mcp.tool()
async def verify_project_lockfile(graph_path: str,
                                  lockfile_path: str) -> str:
    return await _post("verify_project_lockfile", {
        "graph_path": _resolve_graph_path(graph_path),
        "lockfile_path": lockfile_path,
    })


@mcp.tool()
async def get_project_dependency_status(graph_path: str) -> str:
    return await _post("get_project_dependency_status",
                       {"graph_path": _resolve_graph_path(graph_path)})
```

Each tool's docstring describes the JSON response shape so LLM callers can plan a single `json.loads` on the text output.

## Tests

Testing `dispatch()` directly is heavy — it takes 17 parameters including `RuntimeCore&`, `OperatorSourceDocs&`, `SourceIndex&`. Instead we test the one piece of genuinely new logic — `unwrap_status_to_json` — and rely on the existing RuntimeAPI-level tests for the underlying methods plus manual MCP/HTTP smoke for end-to-end verification.

Three new unit tests in `tests/packages/test_project_lockfile.cpp`:

1. **`unwrap_status_to_json` inlines valid status** — build a `CommandResult{ok=true, message=lockfile_status_to_json(s)}`, assert response contains `status` as a real JSON object addressable without a second parse.
2. **`unwrap_status_to_json` preserves error** — `ok=false, message="boom"` → `{"ok": false, "error": "boom"}`, no `status` field.
3. **`unwrap_status_to_json` non-JSON fallback** — defensive: if `message` isn't valid JSON despite `ok=true`, `status` is preserved as a string. Shouldn't happen in practice but documents the behavior.

All run under the existing `test_project_lockfile` target.

## Manual MCP verification

Not unit-tested; verify against a running runtime:

```bash
./build/vivid graphs/intro/demo.json &      # runtime on :9876
./.venv-mcp/bin/python scripts/mcp_bridge_smoke.py \
    write_project_lockfile graph_path=/abs/path/to/demo.json
./.venv-mcp/bin/python scripts/mcp_bridge_smoke.py \
    get_project_dependency_status graph_path=/abs/path/to/demo.json
```

Eyeball the JSON output. The `status` field should be an object, not a stringified JSON.

## Verification

```bash
cmake --build build --target test_project_lockfile
ctest --test-dir build --output-on-failure -R project_lockfile
```

Background per user preference. Then the manual MCP smoke above for end-to-end coverage.

## Acceptance Criteria

- `POST /write_project_lockfile` writes a `vivid.lock` and returns `{"ok": true, "message": <path>}`.
- `POST /verify_project_lockfile` and `POST /get_project_dependency_status` return `{"ok": true, "status": {...}}` with an inline object (no nested JSON string).
- `get_project_dependency_status` returns `status.overall == "no_lockfile"` when the sibling is absent.
- MCP tools in `vivid_mcp.py` correspond 1:1 to the HTTP methods and return the response text verbatim.
- `ctest -R project_lockfile` stays green across all existing and new cases.
- No changes outside the files listed in "Modified".
- Three commits on `worktree-project-lockfile` beyond the Phase 3 commits.

## Out of Scope (Phase 4)

- CLI subcommands `vivid lock` / `vivid verify-lock` (Phase 5).
- UI indicator, load modes, `locked_unavailable` reason (Phase 6).
- Export strict mode (Phase 7).
- Asset hashing (Phase 8).
- Phase 0 provenance capture.
- `vivid_opdev_mcp.py` tool wrappers — lockfile is user-facing, not operator-dev.

Phase 4 makes the lockfile reachable from MCP clients and any HTTP consumer, which is the last step before the feature becomes useful in everyday workflows. End-user surfaces (UI indicator, CLI) arrive in Phase 5+.
