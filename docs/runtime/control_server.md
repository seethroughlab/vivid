# ControlServer — HTTP JSON-RPC Interface

## Overview

`ControlServer` (control_server.h/cpp) exposes the Vivid runtime over a local HTTP server
(default port 9876, bound to 127.0.0.1). It uses the IXWebSocket `ix::HttpServer`.

Design: **pimpl with a threadsafe queue**. The HTTP server thread enqueues requests as
`PendingRequest { method, body, promise<string> }`. Each frame, `process_requests()` drains
the queue on the main thread, dispatches commands, and fulfills promises.

This ensures all runtime mutations happen on the main thread — no locking on `Scheduler` or `Graph`.

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
| `get_graph_load_diagnostics` | — | Package version mismatch info from last load |
| `get_graph_errors` | — | Per-node error state |
| `list_types` | — | All registered operator types with params/ports |
| `list_nodes` | — | All nodes (id, type) |
| `inspect` | `node_id` | Single node params + port values |
| `validate_checks` | `checks` | Validate check definitions (no graph needed) |
| `run_checks` | `checks` | Run checks against live graph |

### Graph Topology
| Method | Key params | Description |
|--------|-----------|-------------|
| `add_node` | `type`, `id` | Add node, queues topology rebuild |
| `remove_node` | `node_id` | Remove node + connections |
| `connect` | `from_addr`, `to_addr`, `semantic_defaults` | Connect ports (`node_id/port_name`) |
| `disconnect` | `from_addr`, `to_addr` | Disconnect ports |
| `set_connection_remap` | `from_addr`, `to_addr`, `from_min/max`, `to_min/max`, `clamp` | Set wire remap |

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
| `load_graph` | `path` | Load graph from file |

### Variations
| Method | Key params | Description |
|--------|-----------|-------------|
| `save_variation` | `name` | Snapshot all params as named variation |
| `recall_variation` | `name` | Apply variation params to live nodes |
| `remove_variation` | `name` | Delete variation |
| `rename_variation` | `old_name`, `new_name` | Rename |
| `update_variation` | `name` | Overwrite with current params |
| `list_variations` | — | All variation names |
| `queue_variation` | `name`, `quantize` (`"instant"/"beat"/"bar"/"four_bar"`) | Schedule switch |
| `set_quantize_clock` | `node_id` | Set clock node for quantized switching |

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
| `package_operator_docs` | `name` | Per-operator documentation |
| `test_package` | `name` | Run package tests |

### Scaffolding
| Method | Key params | Description |
|--------|-----------|-------------|
| `scaffold_operator` | `name`, `domain`, `variant` | Generate operator from template |

## Buffered vs Immediate Commands

`is_topology_command()` returns true for commands that must be applied via `apply_pending()`:
`add_node`, `remove_node`, `connect`, `disconnect`, `set_connection_remap`, `set_param`,
`set_string_param`, `set_resolution`, `set_node_layout`, MIDI/variation/preset mutations, `load_graph`.

Non-topology commands (`inspect_graph`, `list_types`, etc.) can execute immediately on the main thread.

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
