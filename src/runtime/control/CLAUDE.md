# Control Server & RuntimeAPI

## Purpose

This directory contains the HTTP control server that exposes the Vivid runtime to external tools, and the `RuntimeAPI` command layer that implements all runtime mutations. The control server is how MCP bridges, CLI tools, and any external automation interact with a running Vivid instance.

## Key Files

| File | Role |
|------|------|
| `control_server.h/cpp` | `ControlServer` — HTTP listener (port 9876), pimpl with thread-safe request queue |
| `control_server_dispatch.cpp` | `dispatch()` — routes method names to handlers, covers both read-only queries and mutations |
| `control_server_query.cpp` | Read-only query handlers: `inspect_graph`, `list_types`, `operator_docs`, `introspect_nodes`, etc. |
| `control_server_checks.h/cpp` | Graph validation check handlers (`run_checks`, `validate_checks`) |
| `control_server_assets.cpp` | Asset library endpoint handlers |
| `control_server_internal.h` | Shared types, forward declarations, and handler signatures across control server files |
| `runtime_api.h/cpp` | `RuntimeAPI` — high-level command layer wrapping `Graph`, `RuntimeCore`, `AudioEngine`, and `OperatorRegistry` |
| `runtime_api_live.cpp` | Live-state queries: inspect, introspect, sample outputs |
| `runtime_api_modulation.cpp` | Modulation assignment CRUD |
| `runtime_api_persistence.cpp` | Save, load, reload, new graph, snapshot application |
| `runtime_api_variations.cpp` | Legacy variation compatibility, preset management, session quantization, state-preset mapping |
| `runtime_command_sink.h/cpp` | Abstract command sink interface for decoupling UI from control server |
| `graph_file_io.h/cpp` | Graph file loading/saving helpers |

## How It's Organized

### Request Flow

The control server uses a **queue-and-drain** pattern to keep all runtime mutations on the main thread:

1. The HTTP server thread (IXWebSocket) receives a `POST /<method>` request
2. It enqueues a `PendingRequest` with a `std::promise<string>` into a thread-safe queue
3. Each frame, `process_requests()` drains the queue on the main thread
4. `dispatch()` routes the method name to the appropriate handler
5. The handler executes and fulfills the promise; the HTTP thread returns the response

This means runtime state is never accessed from the HTTP thread — no locking on `RuntimeCore` or `Graph`.

### Dispatch Organization

`dispatch()` in `control_server_dispatch.cpp` is a method-name router. It checks read-only queries first (these don't need to parse the JSON body), then parses the body and routes to mutation handlers. The function delegates to `RuntimeAPI` for all state-changing operations.

Mutations that affect graph topology (add_node, remove_node, connect, disconnect) set `pending_topology_change_` in `RuntimeAPI`. These are applied between frames via `apply_pending()`, which triggers a graph recompile.

### RuntimeAPI Split

`RuntimeAPI` is the central command layer — the control server never mutates `Graph` or `RuntimeCore` directly. The implementation is split across files by domain:

- **`runtime_api.cpp`** — core topology mutations (add/remove/connect), parameter access, undo/redo
- **`runtime_api_live.cpp`** — live-state inspection and node output sampling
- **`runtime_api_modulation.cpp`** — modulation source/destination/assignment CRUD
- **`runtime_api_persistence.cpp`** — save/load/reload, snapshot application, graph identity management
- **`runtime_api_variations.cpp`** — legacy variation compatibility, session quantization, per-operator presets, state-preset mapping

`RuntimeAPI` also owns cross-cutting state: undo/redo history, reload serial (bumped on every topology change), graph dirty tracking, and reserved crossfade state for future session transitions.

## Relationships

- **Upstream:** `Graph`, `RuntimeCore`, `AudioEngine`, `OperatorRegistry` — all passed by reference, not owned
- **Downstream:** MCP bridge (`mcp/vivid_mcp.py`) connects via HTTP; UI uses `RuntimeCommandSink` for the same operations
- **Cross-cutting:** `BuildConsole`, `SourceIndex`, `PackageManager` are passed to dispatch for opdev query support

## See Also

- `docs/runtime/control_server.md` — full endpoint catalog and protocol details
- `docs/runtime/runtime_api.md` — `RuntimeAPI` method signatures and buffered vs. immediate semantics
- `docs/LLM-INTEGRATION.md` — how MCP bridges layer on top of the control server
