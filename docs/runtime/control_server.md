# ControlServer — HTTP JSON-RPC Interface

## Overview

`ControlServer` (control_server.h/cpp) exposes the Vivid runtime over a local HTTP server
(default port 9876, bound to 127.0.0.1). It uses the IXWebSocket `ix::HttpServer`.

Design: **pimpl with a threadsafe queue**. The HTTP server thread enqueues requests as
`PendingRequest { method, body, promise<string> }`. Each frame, `process_requests()` drains
the queue on the main thread, dispatches commands, and fulfills promises.

This ensures all runtime mutations happen on the main thread — no locking on `RuntimeCore` or `Graph`.

## Ownership

The control server is embedded in the running Vivid app process.

- the running Vivid instance owns the live graph and runtime state
- that same process binds the local HTTP server, default `127.0.0.1:9876`
- one live runtime instance owns that port for a given session
- external tools should connect to that instance rather than launching additional runtimes for the same interactive session

## Relationship To MCP

The control server is **not** itself an MCP server.

- `mcp/vivid_mcp.py` is a separate Python MCP bridge process
- the bridge can launch or reuse a Vivid runtime, then translates MCP stdio tool calls into HTTP requests against this control server
- MCP clients therefore operate on the current live runtime instance through the bridge layered on top of the HTTP control server
- for static lookup-only tasks, the MCP bridges may bypass the control server entirely and invoke one-shot `vivid` CLI JSON queries instead

Interface capture, graph inspection, and live analysis are conceptually operations on the running runtime, even if some isolated debug/repro workflows still describe direct CLI screenshot runs.

`capture_interface` is the live-session whole-window capture path. Unlike `capture_frame`, it runs after the graph UI, thumbnails, and overlays have been composed, so the returned PNG reflects the actual inspector/window state of the running instance.

Read-only source browsing belongs to the separate opdev MCP bridge, not to the main `vivid` MCP bridge. The control server still exposes a compact source/build query surface so `mcp/vivid_opdev_mcp.py` can reuse the running runtime's source-root discovery and build-console state, but those methods are intended for opdev workflows rather than general live-graph authoring.

The practical boundary is:

- use the control server for commands that need a live graph, live runtime state, or the visible session
- use one-shot CLI JSON queries for operator/package/docs/catalog lookup that does not need a running session

## HTTP Protocol

- **Method**: `POST /<method_name>`
- **Body**: JSON object with method-specific fields
- **Response**: JSON object with `"ok": true/false` and method-specific fields

All requests are POSTs. The URL path is the method name (e.g. `POST /add_node`).

## Endpoint Catalog

### Graph Inspection
| Method | Key params | Description |
|--------|-----------|-------------|
| `inspect_graph` | — | All nodes, params, connections as JSON |
| `introspect_nodes` | — | Compact per-node state summary |
| `run_diagnostics` | — | Graph-level diagnostics (port mismatches, etc.) |
| `capture_interface` | `node_id` (optional), `save_path` (optional), `ensure_ui_visible` (default `true`) | Capture the full running interface after UI overlays are drawn |
| `analyze_output` | `mode`, `window_seconds`, `include_payload`, `node_id` (optional) | Capture and analyze the current frame/audio/AV output |
| `compare_outputs` | `mode`, `a.window_seconds`, `b.window_seconds`, `include_payload`, `node_id` (optional) | Capture two output windows and return structured comparison |
| `get_graph_load_diagnostics` | — | Package version mismatch info from last load |
| `get_registry_diagnostics` | — | Registered custom port types + loader ABI mismatch diagnostics |
| `get_graph_errors` | — | Per-node error state |
| `list_types` | — | All registered operator types with enriched params/ports plus summary docs fields when available |
| `operator_docs` | `name`, `package` (optional) | Full documentation + descriptor metadata for one operator |
| `list_nodes` | — | All nodes (id, type) |
| `inspect` | `node_id` | Single node params + port values |
| `validate_checks` | `checks` | Validate check definitions (no graph needed) |
| `run_checks` | `checks` | Run checks against live graph |
| `sample_node_outputs` | `node_id`, `duration_seconds`, `interval_ms`, `include_lanes` | Time-series sampling of a node's output port values |

### Capture
| Method | Key params | Description |
|--------|-----------|-------------|
| `capture_frame` | — | Capture single GPU output frame |
| `capture_audio` | `window_seconds` | Capture audio buffer |
| `capture_av` | `window_seconds` | Capture synchronized audio + video |
| `start_recording` | `path`, `fps` | Begin continuous recording to file |
| `stop_recording` | — | End recording session |

### Graph Topology
| Method | Key params | Description |
|--------|-----------|-------------|
| `new_graph` | — | Create empty graph |
| `add_node` | `type`, `id` | Add node, queues topology rebuild |
| `remove_node` | `node_id` | Remove node + connections |
| `connect` | `from_addr`, `to_addr`, `semantic_defaults` | Connect ports (`node_id/port_name`) |
| `disconnect` | `from_addr`, `to_addr` | Disconnect ports |
| `set_connection_remap` | `from_addr`, `to_addr`, `from_min/max`, `to_min/max`, `clamp` | Set wire remap |

### Module Modulation
| Method | Key params | Description |
|--------|-----------|-------------|
| `add_mod_assignment` | `node_id`, `source`, `destination`, `amount`, `polarity`, `curve` | Add a named modulation assignment on a module instance |
| `remove_mod_assignment` | `node_id`, `source`, `destination` | Remove a modulation assignment from a module instance |
| `update_mod_assignment` | `node_id`, `source`, `destination`, `amount`, `polarity`, `curve` | Update an existing modulation assignment |
| `list_mod_sources` | `node_id` | List the module's declared modulation sources |
| `list_mod_destinations` | `node_id` | List the module's declared modulation destinations |
| `list_mod_assignments` | `node_id` | List active authored modulation assignments on the module instance |

### Parameters
| Method | Key params | Description |
|--------|-----------|-------------|
| `set_param` | `node_id`, `param`, `value` | Set float param (immediate) |
| `get_param` | `node_id`, `param` | Get float param value |
| `set_string_param` | `node_id`, `param`, `value` | Set string/file param |
| `set_resolution` | `node_id`, `width`, `height` | Set per-node GPU texture resolution |
| `set_node_layout` | `node_id`, `x`, `y` | Save UI layout position |
| `set_param_lock` | `node_id`, `param`, `flags` | Set PARAM_LOCK_* flags (0-3) |
| `get_param_lock` | `node_id`, `param` | Get lock flags |

### Persistence
| Method | Key params | Description |
|--------|-----------|-------------|
| `save_graph` | `path` (optional) | Save to file |
| `load_graph` | `path` | Load the requested graph file into the running runtime and make it the active graph |

### Undo / Redo
| Method | Key params | Description |
|--------|-----------|-------------|
| `undo` | — | Undo last graph mutation |
| `redo` | — | Redo undone mutation |

### Variations
| Method | Key params | Description |
|--------|-----------|-------------|
| `save_variation` | `name` | Snapshot all params as named variation |
| `recall_variation` | `name` | Apply variation params to live nodes |
| `remove_variation` | `name` | Delete variation |
| `rename_variation` | `old_name`, `new_name` | Rename |
| `update_variation` | `name` | Overwrite with current params |
| `list_variations` | — | All variation names |
| `queue_variation` | `name`, `quantize` (`"instant"/"beat"/"bar"/"4bar"`, legacy `"four_bar"` also accepted) | Schedule switch |
| `duplicate_variation` | `name` | Clone an existing variation |
| `move_variation` | `name`, `position` | Reorder variation in list |
| `set_graph_metronome` | `enabled`, `bpm`, `beats_per_bar` | Update optional graph-wide metronome state and retime the live runtime immediately |
| `set_quantize_clock` | `node_id` | Deprecated compatibility shim for older graphs/tools |

Quantized variation switching now uses the graph metronome. If the metronome is disabled,
`queue_variation` rejects `beat`/`bar`/`4bar` requests instead of silently falling back to an
immediate switch. Live tempo updates are phase-continuous for BPM changes; meter changes restart
the bar immediately and clear any queued quantized switch.

### Per-Operator Presets
| Method | Key params | Description |
|--------|-----------|-------------|
| `save_preset` | `node_id`, `name` | Save preset for one operator |
| `recall_preset` | `node_id`, `name` | Apply preset |
| `update_preset` | `node_id`, `name` | Overwrite preset |
| `remove_preset` | `node_id`, `name` | Delete preset |
| `rename_preset` | `node_id`, `old_name`, `new_name` | Rename |
| `list_presets` | `node_id` | All preset names for node |
| `list_factory_presets` | `node_id` | Built-in factory presets |

### State-Preset Mapping
| Method | Key params | Description |
|--------|-----------|-------------|
| `set_state_preset` | `sm_node`, `state_idx`, `target_node`, `preset_name` | Map state → preset |
| `remove_state_preset` | `sm_node`, `state_idx`, `target_node` | Remove mapping |
| `clear_state_presets` | `sm_node` | Clear all mappings for state machine |
| `inspect_state_presets` | `sm_node` | Get all mappings |

### MIDI
| Method | Key params | Description |
|--------|-----------|-------------|
| `add_midi_mapping` | `node_id`, `param`, `cc`, `channel`, `range_min/max` | Map CC to param |
| `remove_midi_mapping` | `node_id`, `param` | Remove CC mapping |
| `update_midi_mapping` | `node_id`, `param`, `range_min/max` | Update range |

### Solo
| Method | Key params | Description |
|--------|-----------|-------------|
| `set_solo` | `node_id` (empty = clear) | Solo a GPU node |
| `get_solo` | — | Current solo node |

### Sticky Notes
| Method | Key params | Description |
|--------|-----------|-------------|
| `add_sticky_note` | `text`, `x`, `y`, `width`, `height`, `color`, `id` | Add annotation to graph canvas |
| `list_sticky_notes` | — | All sticky notes in current graph |
| `update_sticky_note` | `id`, `text`, `x`, `y`, `width`, `height`, `color` | Update note (all fields optional except `id`) |
| `remove_sticky_note` | `id` | Delete note |

### Assets
| Method | Key params | Description |
|--------|-----------|-------------|
| `list_assets` | `kind` (optional), `scope` (optional) | List merged package and workspace asset-library entries |
| `inspect_asset` | `asset_id` | Inspect one asset entry, including generic `kind_meta` payload |
| `import_asset` | `source_path`, `kind` (optional) | Import a file into the workspace asset library using the built-in handler for that kind |
| `refresh_assets` | — | Rebuild the merged asset-library view from workspace sidecars and remembered package asset sources |

### Utility
| Method | Key params | Description |
|--------|-----------|-------------|
| `mcp_ping` | `server` | Heartbeat / liveness check for MCP clients |
| `package_catalog` | — | Fetch remote package catalog metadata |
| `check_package_updates` | `core_version`, `include_all_installed` | Check installed package update status |
| `check_core_updates` | `force_refresh` | Check core app update availability |

### Opdev Query Support
| Method | Key params | Description |
|--------|-----------|-------------|
| `list_source_roots` | — | Report allowlisted source roots and whether checkout or bundled source is active |
| `search_source` | `query`, `roots`, `limit`, `file_types`, `path_globs` | Full-text search across allowlisted read-only roots |
| `read_source_file` | `path`, `max_bytes` | Read one allowlisted repo-relative file with truncation metadata |
| `read_source_span` | `path`, `start_line`, `end_line` | Read an exact line range from one allowlisted file |
| `find_symbol` | `name`, `roots`, `limit` | Lightweight symbol-definition lookup across allowlisted roots |
| `find_references` | `name`, `roots`, `limit` | Lightweight token/reference lookup across allowlisted roots |
| `get_build_activity` | `scope`, `limit` | Summarize recent or active build/test tasks from the build console |
| `explain_build_failure` | `task_id` or `latest`, `max_lines` | Return the latest failed build/test task with top error lines and bounded raw output |

### Packages
| Method | Key params | Description |
|--------|-----------|-------------|
| `install_package` | `url` | Install from git URL or local path |
| `uninstall_package` | `name` | Remove package |
| `link_package` | `path` | Symlink local package for development |
| `unlink_package` | `name` | Remove symlink |
| `rebuild_package` | `name` | Recompile operators |
| `list_packages` | — | All installed/linked packages |
| `read_package_docs` | `name` | Read package README |
| `list_package_examples` | `name` | List example graph files |
| `read_package_example` | `name`, `example` | Read example file |
| `package_operator_docs` | `name` | Per-operator documentation merged with descriptor metadata |
| `test_package` | `name` | Run package tests |

### Scaffolding
| Method | Key params | Description |
|--------|-----------|-------------|
| `scaffold_operator` | `name`, `domain`, `variant`, `inputs`, `outputs`, `params` | Generate operator from template. `name`/`domain`/`variant` create a starter template (UI/CLI path). `inputs`/`outputs`/`params` are optional and used by MCP tools for programmatic advanced scaffolding. |

## Buffered vs Immediate Commands

`is_topology_command()` returns true for commands that must be applied via `apply_pending()`:
`add_node`, `remove_node`, `connect`, `disconnect`,
`set_connection_remap`, `set_param`, `set_string_param`, `set_resolution`, `set_node_layout`,
MIDI/variation/preset mutations, `load_graph`.

Non-topology commands (`inspect_graph`, `list_types`, etc.) can execute immediately on the main thread.

`inspect_graph` now also returns `result.meta` when the loaded graph carries graph-owned content
metadata. This mirrors the persisted `GraphContentMeta` contract from `Graph::load()` / `save()`
and includes Step 6 fields such as `domains`, `content_kind`, `category`, `family`, `role`,
`playability`, and raw `preview_controls`.

Param metadata returned through `inspect_graph`, `list_types`, and `operator_docs` may also include
an optional `asset_kind` string. When present, it marks a file/string-backed param as asset-bound
and declares which asset-library kind the UI should browse for that param.

The asset endpoints are also immediate. `refresh_assets` rebuilds both workspace and package entries;
it is not limited to workspace sidecars only.

Asset entries are content-agnostic at the control-server layer. The shared fields are:

- `asset_id`, `kind`, `display_name`, `scope`, `package_name`
- `canonical_path`, `relative_path`, `source_hash`
- `imported_at`, `discovered_at`, `file_size`, `file_format`
- `kind_meta`

`kind_meta` carries the kind-specific extracted metadata owned by the built-in handler for that asset
kind. In v1, the only built-in kind is `wavetable`, so `kind_meta` contains wavetable metadata such
as sample rate, channel count, frame count, samples per frame, total samples, and peak amplitude.

## Pimpl / Impl

```cpp
struct ControlServer::Impl {
    ix::HttpServer server;
    std::mutex queue_mutex;
    std::deque<PendingRequest> queue;
    std::atomic<bool> running;
    vivid::UndoManager undo_history{200};  // 200-step undo history
};
```

The HTTP callback enqueues `PendingRequest` with a `std::promise<string>`.
`process_requests()` pops and dispatches on the main thread, fulfilling each promise.
The HTTP thread blocks on `future.get()` until the main thread processes the request.

## Undo System

`UndoManager` (undo_manager.h) is embedded in the pimpl with a 200-step history.
Topology-changing commands that modify the graph capture a before/after JSON snapshot for undo/redo.

## `test_package` Response Shape

`test_package` returns package-test results with both human-readable and machine-readable fields.

## Operator Introspection Payloads

The operator introspection endpoints are intended to serve MCP planning, authoring, and docs lookup.

### `list_types`

`list_types` returns the registered operator catalog with descriptor metadata for:

- params: type/default/range plus semantic metadata, optional `asset_kind`, and descriptions when present
- ports: type/transport plus semantic metadata, defaults/channels, and custom-type registry info when present

When the runtime can resolve operator docs from source comments in the core source tree or an installed
package source tree, the response may also include summary-level doc fields:

- `brief`
- `has_docs`
- `operator_family`
- `lane_behavior_help`

Missing docs are not an error. Operators without matching docs still appear with descriptor-only metadata and `has_docs: false`.

### `operator_docs`

`operator_docs` returns a single enriched operator object for `name`.

Request body:

```json
{ "name": "Envelope" }
```

Optional:

- `package`: force package doc lookup for an installed package operator

Response shape:

- descriptor metadata: `name`, `kind`, `time_dependent`, `lane_behavior`, `lane_behavior_help`, `params`, `inputs`, `outputs`
- doc-derived fields when available: `brief`, `body`, `source_path`, `tips`, `related`, `recipes`, `pitfalls`, `best_used_with`, `common_companions`, `operator_family`
- `has_docs`: `true` when a source doc block was found and merged, `false` otherwise

If no docs are found, the endpoint still succeeds and returns the descriptor-only payload.

### `package_operator_docs`

`package_operator_docs` returns the same enriched operator shape as `operator_docs`, but for every operator
owned by the installed package named by `name`.

Docs are resolved from package operator source comments when available. Missing package docs do not fail the
request; the endpoint still returns descriptor-only entries with `has_docs: false`.

Top-level response payload:

- `package`
- `summary`
  - `total`
  - `passed`
  - `failed`
  - `skipped`
- `notes` (optional)
- `tests`

Each entry in `tests` includes:

- `name`
- `type`
- `status`
- `code` (stable classifier)
- `reason` (optional)
- `output` (optional, usually only for C++ tests)

Representative `code` values:

- graph:
  - `graph_passed`
  - `graph_needs_gpu`
  - `graph_needs_audio`
  - `graph_load_failed`
  - `graph_build_failed`
  - `graph_node_error`
  - `unsupported_graph_test_shape`
- shared validation:
  - `missing_test_file`
  - `path_outside_package`
  - `duplicate_test_entry`
- cpp:
  - `cpp_passed`
  - `unsupported_test_extension`
  - `unsupported_cpp_test_shape`
  - `cpp_compile_failed`
  - `cpp_runtime_failed`
  - `cpp_runtime_launch_failed`
  - `cpp_runtime_abnormal`

`notes` is where the server surfaces contract guidance that applies to the whole package, for example:

- no manifest tests declared
- manifest `tests.cpp` entries that should stay in package-local CMake / CTest

## Custom Port Introspection

For custom ports, `list_types` now exposes registry-backed metadata in addition to
descriptor fields:

- `custom_type_registered`
- `audio_safe`
- `registry_package_name` (when known)
- `registry_description` (when known)

`get_registry_diagnostics` is the process-wide view for package/tooling debugging. It returns:

- `custom_port_types`
  - stable registered custom port metadata from the runtime registry
- `abi_mismatch_diagnostics`
  - plugins skipped during probing because plugin ABI and runtime ABI did not match
- `loader_failure_diagnostics`
  - plugins that failed full load after probing, including malformed descriptor and
    custom-type registration failures

Representative `loader_failure_diagnostics` fields:

- `plugin_path`
- `plugin_name`
- `package_name` (optional)
- `code`
- `message`
