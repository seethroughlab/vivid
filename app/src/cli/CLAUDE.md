# `app/src/cli/` — the MCP control server

The loopback HTTP backend the agent bridge (`mcp/vivid_mcp.py`) drives. Flow +
codes in [`../../ARCHITECTURE.md`](../../ARCHITECTURE.md) §5.

- **`control_server.{h,cpp}`** — cpp-httplib server on a background thread bound to
  `127.0.0.1:$VIVID_PORT` (default 9876). `POST /<method>` parses the JSON body,
  enqueues `{method, body, promise}`, and **blocks on the future**; the main loop's
  `process_pending(ctx)` drains the queue once per frame and dispatches via a
  `method → handler` table — so **all mutations run on the UI thread**. The handler
  `ControlCtx` carries `App`'s session/graph/vgraph/transport + the `Window`'s
  metric pointers.
- **`control_errors.h`** — `ok()` / `err(code, msg)` and the **stable error codes**
  (`bad_json`, `unknown_method`, `no_session/graph/vgraph/transport`, `bad_arg`,
  `out_of_range`, `not_found`, `io_error`, `internal`, `timeout`). Clients branch on
  `code`, not the prose. Codes are pinned by `tests/test_control_errors.cpp`.
- **`control_parse.h`** — pure helpers (`in_range`, `kind_index`,
  `char_id_from_source` — the mapping-source wire encoding). Unit-tested headlessly
  (`tests/test_control_parse.cpp`); keep new pure parse/validation logic here.
- **`control_handlers_*.cpp`** — the handlers, grouped by domain, register into the
  dispatch table. `control_handlers_edit.cpp` provides `undo`/`redo` (ADR-0017), and
  `edit_methods.{h,cpp}` is the table of which methods the `EditGateway` captures at the
  `process_pending` chokepoint — read-only/performance methods are excluded there.

Handlers validate every track/scene/device/effect index and return `out_of_range`
rather than silently no-op'ing (the engine is also internally guarded — belt & braces).
