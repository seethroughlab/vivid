# Production Crash Recovery Plan

Status: approved for implementation. This does not replace the existing `CrashGuard`; it turns crash attribution into a recoverable production workflow.

## Goal

Make Vivid resilient enough for unattended installations and live performance by ensuring a bad operator, package, graph mutation, GPU path, or device failure does not leave the user with only "the app crashed."

Vivid already records which operator was active during fatal signals through `src/runtime/core/crash_guard.h`. That is the right first step, but production use needs a complete recovery loop:

- record what failed
- reopen safely
- isolate or disable the likely culprit
- preserve enough context for the developer or LLM to fix it

The target user experience is: after a crash, Vivid relaunches or reopens with a clear explanation, the last graph is available, and the suspected node can be bypassed without hand-editing JSON.

## Proposal

Add a production crash recovery layer with four pieces:

- A structured crash record written on fatal process termination or startup recovery.
- A safe-mode graph load path that can disable suspect nodes and skip risky startup behavior.
- A quarantine mechanism for repeatedly crashing operators.
- An optional watchdog process mode for unattended installs that restarts Vivid after crashes.

The first version should focus on operator and graph recovery, because those are Vivid's highest-risk production boundary: LLM-generated and package-supplied C++ runs inside the app process.

## Phase Sequencing

| Phase | Ships independently | Core value |
|-------|-------------------|------------|
| Phase 1 | Yes | Crash records exist — debuggable post-crash state |
| Phase 2 | Yes (with Phase 1) | Safe relaunch — suspect node disabled |
| Phase 3 | Yes (with 1+2) | User-facing recovery flow — dialog + visual feedback |
| Phase 4 | Yes (with 1+2) | Repeat offenders auto-isolated |
| Phase 5 | Yes (with 1+2) | MCP/LLM can explain and fix crashes |
| Phase 6 | Yes (with 1-5) | Unattended restart |

Phases 1-3 deliver the core user experience. Phases 4-6 add production hardening.

---

## Phase 1: Crash Records (Foundation)

**Goal:** Write structured crash state on fatal termination; read and expand it on next startup.

### New Files

| File | Purpose |
|------|---------|
| `src/runtime/core/crash_recovery.h` | `CrashRecord` struct, `CrashRecoveryManager` class |
| `src/runtime/core/crash_recovery.cpp` | Snapshot maintenance, marker expansion, history rotation |
| `tests/core/test_crash_recovery.cpp` | Unit tests for serialization, marker parsing, history pruning |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/core/crash_guard.h` | Add file-scope `static char g_marker_path[1024]` and `static char g_snapshot_path[1024]`. Extend `crash_signal_handler()` to write a minimal marker file (signal number + snapshot path as ASCII) using only `open()`, `write()`, `close()` — all async-signal-safe. Add `set_crash_marker_paths(marker, snapshot)` init function. |
| `src/runtime/core/main.cpp` | After `install_crash_handlers()`: init `CrashRecoveryManager`, set marker/snapshot paths, check for previous crash marker. Periodically call `update_snapshot()` (every 60 frames or on topology change). |
| `src/runtime/platform/platform.h/.cpp` | Add `get_crash_dir()` returning `get_config_dir() + "/crashes"`, ensuring directory exists. |
| `cmake/tests/10-runtime-control-graph.cmake` | Add `test_crash_recovery` target. |

### CrashRecord Fields

The crash record is JSON and stored under the user's Vivid state directory (`get_config_dir()`), not next to the graph. Use a stable filename for the latest crash plus timestamped history:

- `latest-crash.json`
- `crashes/<timestamp>.json`

Record fields (JSON-serializable via nlohmann/json):

- `timestamp` — ISO 8601
- `signal`, `signal_name` — e.g. 11, "SIGSEGV"
- `pid` — process ID
- `vivid_version`, `platform` — core version and OS
- `graph_path`, `graph_dirty` — active graph and whether it had unsaved changes
- `operator_name` — from `g_current_operator` (CrashGuard)
- `node_id`, `node_type` — when available from the snapshot
- `pkg_name`, `pkg_version` — package provenance for package operators
- `last_mutation` — last graph mutation summary from RuntimeAPI
- `audio_device`, `audio_buffer_size` — audio device summary
- `gpu_adapter` — GPU adapter/device summary
- `mcp_attached`, `control_server_port` — control server state

### Signal Handler Design

The signal handler must remain async-signal-safe. Do not serialize JSON from the signal handler.

1. **Normal runtime:** `CrashRecoveryManager::update_snapshot()` serializes current state to `<crash_dir>/latest-snapshot.json` periodically (every 60 frames or on topology change). This pre-formats the recovery data while it is safe to allocate and serialize.

2. **Signal handler:** Opens `g_marker_path` with `open(O_WRONLY|O_CREAT|O_TRUNC)`, writes signal number as decimal ASCII + newline + `g_snapshot_path` + newline. Closes. Then re-raises with `SIG_DFL` as before. Only uses `open()`, `write()`, `close()`, `signal()`, `raise()` — all async-signal-safe.

3. **Next startup:** `CrashRecoveryManager::init()` checks for the marker file. If found, reads the marker + snapshot, combines them into a full `CrashRecord`, writes `latest-crash.json` and `crashes/<timestamp>.json`. Cleans up the marker.

4. **History:** Keep last 20 crash records, prune older.

Existing `CrashGuard` stderr behavior is preserved — the signal handler addition is purely additive.

### Tests

- Round-trip: serialize CrashRecord to JSON, deserialize, verify all fields.
- Fake marker + snapshot on disk, call `expand_marker()`, verify `latest-crash.json` is correct.
- Write >20 crash histories, verify oldest pruned.
- `update_snapshot()` produces valid JSON with expected fields.

---

## Phase 2: Safe-Mode Launch

**Goal:** `--safe-mode` CLI flag loads the graph with suspect nodes disabled, audio deferred, hot-reload off.

**Depends on:** Phase 1 (crash record identifies the suspect operator).

### New Files

| File | Purpose |
|------|---------|
| `src/runtime/core/safe_mode.h` | `SafeModeConfig` struct, disabled-node-set computation |
| `src/runtime/core/safe_mode.cpp` | Compute disabled set from crash record |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/core/main.cpp` | Add `--safe-mode` CLI flag via CLI11. When active: skip `audio_engine.start()`, skip `file_watcher/hot_reloader` init, pass disabled-node set into graph build. |
| `src/runtime/graph/graph_compiler.h` | Add `std::unordered_set<std::string> disabled_nodes` to `GraphCompiler::Options`. |
| `src/runtime/graph/graph_compiler.cpp` | In Pass 1: if node ID is in `disabled_nodes`, treat as missing operator with `missing_operator_reason = "disabled"`, `missing_operator_detail = "Disabled by safe mode (crash recovery)"`. Skip instance creation. |
| `src/runtime/core/runtime_core.h/.cpp` | Thread `disabled_nodes` through `prepare_build()` to compiler options. (Currently at `runtime_core.cpp:81`: `GraphCompiler::Options opts;` — add `opts.disabled_nodes = disabled_nodes_;`) |

### Disabled-Node Design

**No graph schema change.** The disabled-node set is in-memory only, stored in `RuntimeCore`. This reuses the existing `missing_operator` infrastructure — disabled nodes get placeholder treatment, connections are dropped, execution is skipped. The graph compiler already handles all of this gracefully in Pass 1 (placeholder nodes) and Pass 2 (dropped connections).

**SafeModeConfig:**
- `bool active` — whether safe mode is engaged
- `std::unordered_set<std::string> disabled_node_ids` — node IDs whose type matches the crash record's `node_type`
- `std::string crash_operator`, `crash_node_id`, `crash_reason` — context from the crash record

**Disabled-node computation:** On safe-mode startup with a crash record, find all nodes in the graph whose `type` matches `crash_record.node_type`. Add their IDs to the disabled set. If `crash_record.node_id` is available, also add it explicitly.

**Audio deferral:** Skip `audio_engine.start()` in safe mode. Add a `resume_audio()` path callable from UI or MCP (Phase 5).

**Hot-reload suppression:** Skip `file_watcher` and `hot_reloader` init when safe mode is active.

### Tests

- Compile a graph with a disabled node set, verify `missing_operator == true` with reason `"disabled"`.
- Verify connections to/from disabled nodes are dropped.

---

## Phase 3: Recovery UI

**Goal:** Show a recovery dialog on startup after crash. Disabled nodes get distinct visual treatment.

**Depends on:** Phase 1 + Phase 2.

### Modified Files

| File | Change |
|------|--------|
| `src/ui/dialogs/dialog_manager.h` | Add `CrashRecoveryState` struct (following existing dialog patterns: `AboutState`, `SaveConfirmState`, etc.). |
| `src/ui/dialogs/dialog_manager_draw.cpp` | Add `draw_crash_recovery()` — modal with crash summary + 3 buttons: "Open Normally", "Open Safe Mode", "Reveal Crash Report". |
| `src/ui/dialogs/dialog_manager_input.cpp` | Add `update_crash_recovery()` for button click handling. |
| `src/runtime/core/main.cpp` | After crash detection on startup, open recovery dialog. Wire button actions: normal load, safe-mode load with disabled nodes, reveal crash JSON via `open_url()`. |
| `src/ui/graph/node_graph_draw.cpp` | Extend badge rendering: add amber "DISABLED" badge for safe-mode disabled nodes (distinct from red "MISSING" badge). |
| `src/ui/graph/graph_snapshot.h` | Add `bool disabled_by_safe_mode = false` to `NodeSnapshot`. |
| `src/runtime/graph/graph_snapshot_builder.cpp` | Populate `disabled_by_safe_mode` from `CompiledNode::missing_operator_reason == "disabled"`. |

### Recovery Dialog

On startup after a crash, show a recovery modal:

- **Title:** "Vivid crashed during previous session"
- **Body:** "Fatal signal {signal_name} in operator: {operator_name} (node: {node_id})"
- **Buttons:**
  - "Open Normally" — proceed with standard load
  - "Open Safe Mode" — set `safe_mode_config.active = true`, rebuild with disabled nodes
  - "Reveal Crash Report" — call `open_url()` on the crash JSON file path

If safe mode is chosen, select the suspected node in the graph editor and show its error details in the inspector.

### Node Visual States

**Node badge:** Amber "DISABLED" badge (not red — red means missing/broken, amber means intentionally disabled for safety). Inspector shows crash context when a disabled node is selected.

---

## Phase 4: Quarantine

**Goal:** Auto-disable operators that crash repeatedly when in safe mode.

**Depends on:** Phase 1 (crash history) + Phase 2 (disabled-node set).

### New Files

| File | Purpose |
|------|---------|
| `src/runtime/core/quarantine.h` | `QuarantineManager`, operator identity key, threshold logic |
| `src/runtime/core/quarantine.cpp` | Scan crash history, compute quarantine set |
| `tests/core/test_quarantine.cpp` | Unit tests for threshold and window logic |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/core/safe_mode.cpp` | Merge quarantined operators into disabled set on safe-mode startup. |
| `src/runtime/core/main.cpp` | Run quarantine check on safe-mode startup. In normal mode, log a warning for quarantined operators but don't auto-disable. |
| `src/ui/graph/node_graph_draw.cpp` | "QUARANTINED" badge variant for quarantined nodes. |
| `src/ui/graph/graph_snapshot.h` | Add `bool quarantined = false` to `NodeSnapshot`. |

### Quarantine Design

**Operator identity:** `(type_name, pkg_name)` — version-insensitive grouping so a rebuilt package with the same bug still counts.

- core operators: type name
- package operators: package name + operator type
- project operators: type name

**Threshold:** 3 crashes within 24 hours. Recomputed from on-disk crash history at startup — no separate quarantine state file needed.

**Behavior:**
- In safe mode: quarantined operators are automatically disabled with reason `"quarantined"`.
- In normal mode: log a warning for quarantined operators but do not silently disable.

### Tests

- Given N crash records with various operator types, verify threshold correctly identifies repeat offenders.
- Verify window filtering (old crashes outside 24h don't count).
- Verify identity grouping (same type+package, different version = same identity).

---

## Phase 5: MCP / Control Server Endpoints

**Goal:** Expose crash recovery state through the control server so MCP workflows and external tools can query, explain, and act on crashes.

**Depends on:** Phase 1 + Phase 2.

### Files

| File | Change |
|------|--------|
| `src/runtime/control/control_server_dispatch.cpp` | Add routes: `get_last_crash`, `clear_last_crash`, `load_graph_safe_mode`. |
| `src/runtime/control/control_server_internal.h` | Declare handler functions. |
| `src/runtime/control/control_server_crash.cpp` (new) | Implement handlers — read/clear crash record, load graph in safe mode. |
| `src/runtime/core/main.cpp` | Pass `CrashRecoveryManager*` to control server dispatch. |

### Endpoints

- **`get_last_crash`** — returns latest CrashRecord as JSON, or `{"crash": null}` if none. Include quarantine status and 24h crash count.
- **`clear_last_crash`** — removes `latest-crash.json`, returns `{"ok": true}`.
- **`load_graph_safe_mode`** — accepts `{"path": "..."}`, loads graph with disabled-node set from crash record + quarantine.

These return structured JSON so MCP workflows can explain the failure and suggest a fix.

### Tests

- Call `get_last_crash` with no crash record, verify null response.
- Write fake crash record, call `get_last_crash`, verify fields.
- Call `clear_last_crash`, verify subsequent `get_last_crash` returns null.

---

## Phase 6: Watchdog (Deferred)

**Goal:** Optional parent process that restarts Vivid after unexpected termination.

**Depends on:** All prior phases.

Add a later, optional production launcher:

```bash
vivid-watchdog --graph path/to/show.json --restart-on-crash
```

The watchdog is a small parent process that fork/exec's Vivid, watches exit status, and restarts with `--safe-mode` after signal-caused termination (`WIFSIGNALED` / exit status > 128). No graph logic in the watchdog — all recovery intelligence lives in the main process through crash records and safe mode.

- Configurable restart delay (default 2 seconds)
- Max restart count before giving up (default 5)
- For v1, this can be a `--watchdog` mode in the existing binary if a separate helper is too much, but a separate helper is cleaner for crash supervision

This phase is lower priority and can ship independently whenever production deployments need it.

---

## Testing Strategy

### By Level

| Level | What | Where | Runs in CI |
|-------|------|-------|------------|
| Unit | CrashRecord serialization, quarantine thresholds, safe-mode disabled-set computation | `tests/core/test_crash_recovery.cpp`, `tests/core/test_quarantine.cpp` | Yes (partition 10) |
| Runtime | Graph compiler with disabled nodes, snapshot builder populating disabled flags | `tests/graph/test_graph_compiler_safe_mode.cpp` | Yes (partition 10) |
| Control | MCP endpoints for crash state | `tests/control/test_control_server.cpp` (extended) | Yes (partition 10) |
| Integration | Subprocess crash + recovery round-trip | `tests/integration/test_crash_subprocess.cpp` | Optional/nightly |
| Manual | Recovery dialog appearance, badge rendering, inspector crash context | Visual verification | No |

### Verification Commands

```bash
cmake --build build --target vivid && ctest --test-dir build --output-on-failure -R "crash_recovery|quarantine"
```

Adjust target names to match the final test layout.

## Acceptance Criteria

- A crash produces a recoverable, structured record with graph and operator context.
- Relaunch after a crash offers safe mode and identifies the suspected node/operator.
- Safe mode can load the previous graph without executing the suspected operator.
- Repeatedly crashing operators are clearly quarantined in safe mode.
- MCP clients can query crash state and trigger safe-mode loads.
- Existing `CrashGuard` stderr behavior remains intact for developer debugging.
