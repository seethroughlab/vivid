# Audit 04: Control Server, RuntimeAPI & MCP

**Date:** 2026-06-26
**Status:** Audited 2026-06-05 (verify-gated; 8 candidates → 4 confirmed, 4 dismissed)

## Purpose

Audit the command and tool surfaces that mutate or inspect Vivid at runtime, with emphasis on HTTP/MCP contract parity, graph mutation safety, error reporting, and LLM workflow reliability.

## Scope

- `src/runtime/control/`
- `mcp/`
- `docs/runtime/control_server.md`
- `docs/LLM-INTEGRATION.md`
- `docs/MCP-COMPOSITION-COOKBOOK.md`
- Control server, RuntimeAPI, MCP bridge, CLI, and integration tests

## Primary Questions

- [ ] Do HTTP endpoints, `RuntimeAPI` commands, and MCP tools agree on behavior and error shape?
- [ ] Are graph mutations atomic enough for UI, audio, and MCP clients?
- [ ] Are invalid tool calls rejected with diagnostics that are actionable for an LLM?
- [ ] Are MCP-only tools and raw HTTP endpoints clearly documented?
- [ ] Does bridge startup/restart behavior avoid stale tool surfaces?
- [ ] Are runtime capture, package, preset, modulation, and session commands scoped consistently?
- [ ] Are command routing and persistence paths tested independently of UI behavior?

## Subsystem Checklist

- [ ] Trace representative add/connect/set/save/load commands from MCP or HTTP to `RuntimeAPI`.
- [ ] Compare MCP tool definitions with control-server endpoint docs.
- [ ] Review dispatch/query/check layers for duplicated validation or inconsistent errors.
- [ ] Inspect graph file I/O boundaries, including any runtime-to-UI type dependencies.
- [ ] Check crash/reporting endpoints and health responses for useful operational detail.
- [ ] Verify tests cover malformed JSON, missing nodes/ports, package command failures, and concurrent-looking command sequences.
- [ ] Identify tool contract drift that could break composition workflows.

## Audit Checklist

- [ ] Read the relevant subsystem docs and navigation guides.
- [ ] Inspect the main source files and ownership boundaries.
- [ ] Review tests that claim to cover the subsystem.
- [ ] Check docs/code/test contract drift.
- [ ] Identify correctness, robustness, and maintainability findings.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Findings

Split per Completion Criteria into **HTTP/MCP contract & docs** vs **RuntimeAPI implementation**. One
Medium (a silent-success robustness bug), three Low. HTTP↔RuntimeAPI↔MCP error shapes are otherwise
consistent (`CommandResult` → `json_err`/`json_ok_msg` → MCP `_compact_envelope()`).

### RuntimeAPI implementation

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 04-F1 | Medium | Robustness | `load_graph()` reports **success** when the graph rebuilds but the audio engine fails to start — partial state (graph live, audio not running) is not rolled back and not surfaced | `src/runtime/control/runtime_api_persistence.cpp:67-205` |
| 04-F5 | Low | Robustness | `connect()` inline warnings cover unknown *port names* but not *type incompatibilities* (deferred to `get_graph_errors` / compile) | `src/runtime/control/control_server_dispatch.cpp:147-160`; `control_server_internal.h:564-627` |
| 04-F8 | Low | Maintainability | `resolve_module_param()` collapses 5 distinct failure paths into a single `std::nullopt`, so callers emit a misleading `"unknown node"` even when the node exists but isn't a module | `runtime_api.h:331-338`; `runtime_api_live.cpp:169-186`, callers at `:244,305,333,353,381` |

### HTTP / MCP contract & docs

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 04-F4 | Low | Docs | No unified API-surface matrix showing which methods are HTTP-only, HTTP+MCP, or MCP-only | `docs/runtime/control_server.md`, `mcp/CLAUDE.md` (info exists in prose, no table) |

### Evidence & Recommendation

**04-F1 — `load_graph()` silently succeeds on audio-engine-start failure** (Medium, Robustness)
- *Evidence:* `load_graph()` (`runtime_api_persistence.cpp:67`) shuts down the audio engine (`:116`),
  `graph_.load()` (`:122`), `core_.build()` (`:127`), then `audio_engine_.build()` + `start()`
  (`:193-199`). The `restore_previous_state()` lambda (`:82-112`) is invoked **only** on `graph_.load()`
  or `core_.build()` failure — **not** on audio start failure. When `audio_engine_.start()` fails at
  `:195`, execution falls through to `:201-205` and returns `{true, "reloaded from " + path}` — a
  **success** result (confirmed routed verbatim to the client at `control_server_dispatch.cpp:601-603`).
  The graph is fully rebuilt (CompiledGraph live, GPU works) but audio isn't running, and nothing in the
  response says so.
- *Impact:* If the audio device is unavailable at load time, `load_graph` returns success; a subsequent
  `save_graph` persists this partial state; `has_audio=false` is ambiguous (no audio nodes **vs** audio
  engine failed), so an MCP/LLM client composes against wrong assumptions. *Example failing input:* load
  any audio-bearing graph while the audio device is unavailable → `{ok:true, "reloaded from …"}`,
  `has_audio:false`.
- *Recommendation:* Make audio-start failure **visible** — keep the non-fatal partial-load behavior (a
  graph without audio is more useful than a hard rollback), but return a warning/flag in the result
  (e.g. `audio_unavailable:true` + message) and document that `has_audio=false` can mean "engine
  unavailable", not just "no audio nodes". (Two-phase commit — verify audio can start before
  `graph_.load()` — is the heavier alternative.)

**04-F5 — `connect()` warnings omit type incompatibilities** (Low, Robustness)
- *Evidence:* On a successful `connect()`, `control_server_dispatch.cpp:147-160` calls
  `connect_port_issue()` for the from/to addresses, which only checks **port-name existence/direction**
  (`control_server_internal.h:564-627`) — no port-type comparison. `RuntimeAPI::connect`
  (`runtime_api_live.cpp:557-607`) validates address format, node existence, and duplicates, but no type
  compatibility. A type-mismatched wire (e.g. `audio_float` → `gpu_texture`) returns `ok=true` and is
  only flagged later by `get_graph_errors`/compile.
- *Impact:* Tight-loop MCP clients get `ok=true` for a semantically invalid connection, discovering it a
  round-trip later. *Example:* `connect("osc/out"(audio_float) → "blur/in"(gpu_texture))` → `ok:true`,
  no warning. Not a correctness bug (compiler drops it), a UX/feedback gap.
- *Recommendation:* Extend the `connect()` warning path to compare port types and warn on mismatch
  immediately (the descriptor port types are already available in `connect_port_issue`'s context).

**04-F8 — `resolve_module_param` failure paths collapse to "unknown node"** (Low, Maintainability)
- *Evidence:* `resolve_module_param` (`runtime_api_live.cpp:169-186`) has 5 distinct failure exits (null
  module registry, node not in graph, node type not a registered module, internal binding not found,
  compiled internal node/param missing) all returning bare `std::nullopt`. Every caller (`set_param:244`,
  `set_string_param:305`, `get_string_param:333`, `get_param:353`, `set_param_lock:381`) then emits
  `"unknown node '<id>'"`.
- *Impact:* An LLM setting a param on a node that exists but isn't a module gets `"unknown node"`, which
  is misleading. *Example:* `set_param("reverb/mix", …)` where `reverb` exists but is a plain op →
  `"unknown node 'reverb'"`. Error-clarity only; no functional consequence.
- *Recommendation:* Give `resolve_module_param` an out error string (or a result struct with
  `error_message`), and have callers distinguish "node not found" from "node is not a module" / "no such
  module param".

**04-F4 — No HTTP/MCP API-surface matrix** (Low, Docs)
- *Evidence:* `control_server.md` catalogs HTTP endpoints; `mcp/CLAUDE.md` separates the three bridges and
  states MCP-only tools exist — but there's no single `Method | HTTP | MCP | Notes` table. The
  information exists in prose (`control_server.md:23-41` "Relationship To MCP"; `mcp/CLAUDE.md:33-39`),
  just not consolidated.
- *Impact:* Integration friction ("is X reachable over raw HTTP?") requires reading two docs. Mild.
- *Recommendation:* Add a surface-map table to `control_server.md` (or `MCP-COMPOSITION-COOKBOOK.md`).

### Test Gaps

Reported separately from findings (note: several finder-proposed gaps were **refuted** as already
covered — see Dismissed):

- Partial audio-engine-startup failure on `load_graph` — verify the result surfaces audio-unavailable
  (ties to 04-F1).
- Type-mismatch on `connect` — assert the warning (after 04-F5) / `get_graph_errors` message for
  incompatible port types.
- `resolve_module_param` error differentiation — `set_param` on a non-module node returns a
  module-specific error, not "unknown node" (after 04-F8).
- Lockfile-mode enforcement under `load_graph` (nodes marked `locked_unavailable` on critical findings).
- Large topology batches (e.g. 50 nodes + 100 connections → single `apply_pending`).

### Docs to Update

- `docs/runtime/control_server.md` — "Buffered vs Immediate Command Timing" (set_param immediate;
  add_node/connect/disconnect buffered until `apply_pending`); + the 04-F4 API-surface matrix.
- `src/runtime/control/runtime_api_persistence.cpp` — comment on `load_graph`'s non-transactional
  audio-start behavior (04-F1).
- `mcp/CLAUDE.md` — the bridge tool-restart requirement is **already** documented (basis of dismissed
  04-F3); no change needed.

## Follow-up

**Immediate** — none (04-F1 is Medium, not a crash/data-loss; surfacing fix is small but not urgent).

**Near-term**
- 04-F1: surface audio-unavailable state from `load_graph` (warning/flag + doc); add the partial-failure test.

**Backlog**
- 04-F5: type-mismatch warning in `connect()`.
- 04-F8: differentiated `resolve_module_param` errors.
- 04-F4: API-surface matrix + buffered-vs-immediate doc section.
- Work the remaining Test Gaps as those paths change.

### Dismissed (verification-refuted)

Four candidates were refuted by the verify pass:

- **04-F2** (schema_version "not versioned across endpoints") — `schema_version` is **intentionally
  per-endpoint** and documented (`docs/plans/archive/production-gate/*`); `run_diagnostics`/`get_runtime_health`
  already return `2`. The MCP default-to-1 is correct back-compat, not masking. The recommended global
  envelope version conflicts with the deliberate convention.
- **04-F3** (MCP bridge stale-tool surface "under-documented") — already a **bolded, dedicated paragraph**
  in `mcp/CLAUDE.md:33-39` naming the exact symptom + a raw-HTTP workaround. The recommended "document
  prominently" is already done.
- **04-F6** (no test for buffered add_node+connect+set_param) — already covered
  (`tests/control/test_runtime_api.cpp:169-205` buffers add+connect → one `apply_pending` and asserts
  `a/scale` preserved at 7.0). The finder's proposed sequence is also invalid (`set_param` is immediate,
  so it can't target a not-yet-compiled buffered node).
- **04-F7** (no test for `load_graph` failure modes) — already covered: malformed-JSON reload + restore is
  tested at `test_runtime_api.cpp:460-504`, and `apply_snapshot` restore at `:651-716`. Missing-file /
  missing-operator funnel into the same already-tested `restore_previous_state` branch.

## Completion Criteria

- [x] Findings table is filled in or explicitly marked with no findings.
- [x] HTTP/MCP parity issues are listed separately from RuntimeAPI implementation issues.
- [x] Error-reporting findings include example failing inputs where possible.
- [x] Docs that must change with tool behavior are identified.
- [x] Follow-up work is grouped into immediate, near-term, and backlog.
