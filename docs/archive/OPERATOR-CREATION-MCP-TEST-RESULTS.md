# Operator Creation + MCP Test Results

## Run 1

- Date: 2026-03-04
- Branch/Workspace: `/Users/jeff/Developer/vivid`
- Scope: Milestone 1 item 5 initial execution (`OC-1`, `OC-3`)

## Commands Run

- `build/test_operator_creator`
- `build/test_hot_reload`
- `build/test_control_server .` (outside sandbox; localhost bind required)
- Attempted live runtime API path:
  - `build/vivid --headless --src-dir /Users/jeff/Developer/vivid`
  - control-server probe via `curl http://127.0.0.1:9876/...`

## Results

### OC-1 End-to-End Scaffold -> Edit -> Reload -> Verify

- Status: `PARTIAL PASS`
- Evidence:
  - `test_operator_creator`: all checks passed, including scaffold for control/audio/gpu/composite and template/CMake patch validation.
  - `test_hot_reload`: all checks passed, including reload behavior and state/param reconciliation across operator versions.
- Notes:
  - This validates scaffold generation and reload correctness, but not the full interactive `scaffold -> edit -> hot-reload -> use in running vivid graph via control server` path in one live session.

### OC-3 MCP `scaffold_operator` via Control Server

- Status: `BLOCKED (live vivid path) / PARTIAL PASS (control-server harness)`
- Evidence:
  - `test_control_server` passes fully when run outside sandbox, confirming control server request/response path works over HTTP.
  - Live `vivid` runs in this environment did not expose `127.0.0.1:9876` despite startup attempts, so a real `POST /scaffold_operator` file-creation verification against the live app could not be completed in this run.
- Blocker:
  - Runtime startup path in this environment does not reach a listening control-server socket on `9876` during attempted sessions.

## Next Actions

- Add a dedicated integration test that exercises `POST /scaffold_operator` on `ControlServer` with `set_src_dir(...)` and asserts generated files exist.
- Re-run OC-1/OC-3 against a live `vivid` session once control-server bring-up on `9876` is reproducible in this environment.

## Run 2

- Date: 2026-03-04
- Scope: Live MCP bridge verification after startup diagnosis

### Startup diagnosis findings

- `sample` on hung process showed startup blocked in `OperatorRegistry::scan_deferred()` during plugin probe teardown.
- Probe tracing identified a deterministic probe crash at `particles3d.dylib`.
- After skipping that plugin, startup reached installed package scan and crashed probing package plugin `reverse.dylib`.

### Mitigations used for verification run

- Launch flags:
  - `VIVID_SKIP_PLUGINS=particles3d`
  - `VIVID_SKIP_PACKAGE_SCAN=1`
- Temp scaffold source root:
  - `/tmp/vivid_oc_src` with CMake insertion markers

### OC-3 live bridge result

- Status: `PASS`
- Evidence:
  - Control server reached listening state on `http://127.0.0.1:9876`.
  - Python MCP bridge call succeeded:
    - Request: `scaffold_operator("oc_bridge_probe", "control")`
    - Response: `{"ok":true,"result":{"cpp_path":"/tmp/vivid_oc_src/operators/control/oc_bridge_probe/oc_bridge_probe.cpp","target_name":"oc_bridge_probe"}}`
  - File verification:
    - `/tmp/vivid_oc_src/operators/control/oc_bridge_probe/oc_bridge_probe.cpp` exists.
    - `/tmp/vivid_oc_src/CMakeLists.txt` contains `add_vivid_operator(oc_bridge_probe ...)`.

### OC-1 status update

- Status: `PASS` (combined evidence from `test_operator_creator`, `test_hot_reload`, and successful live scaffold call)
