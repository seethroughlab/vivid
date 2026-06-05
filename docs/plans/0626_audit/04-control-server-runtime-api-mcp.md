# Audit 04: Control Server, RuntimeAPI & MCP

**Date:** 2026-06-26
**Status:** Re-audited (maintainability) 2026-06-05 (verify-gated; 7 candidates → 5 confirmed, 2 dismissed). Prior correctness pass retained below; Round-2 maintainability section at end.

## Purpose

Audit the command and tool surfaces that mutate or inspect Vivid at runtime, with emphasis on HTTP/MCP contract parity, graph mutation safety, error reporting, and LLM workflow reliability.

## Re-Audit Mandate

The prior pass should be treated as a correctness/robustness audit, not a complete code-quality audit.
Run this audit again with equal weight on maintainability: structure, duplication, ownership boundaries,
API clarity, dependency direction, and ease of future change.

Do not mark the audit complete until every checklist item is annotated as `[x]` done, `[~]` partially
covered, or `[ ]` intentionally deferred with a short note. Findings must include both confirmed defects
and structural risks that make future defects likely.

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
- [ ] Identify oversized files, mixed responsibilities, fragile seams, and unclear ownership.
- [ ] Identify duplicated logic or repeated patterns that should be shared or intentionally documented.
- [ ] Check dependency direction and public/private API boundaries.
- [ ] Check whether tests make future refactors safe, not just whether they cover the latest fix.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Required Maintainability Review

- [x] Map HTTP, RuntimeAPI, MCP, CLI, validation, and docs responsibilities and identify mixed ownership. → `dispatch()` monolith (04-R2-F2), `internal.h` god-header (04-R2-F3), `query.cpp` mixed concerns (04-R2-F4).
- [x] Look for duplicated parsing, address resolution, type checking, command validation, and error-shaping logic. → **the headline**: per-handler field-extraction/validation boilerplate across 136 dispatch handlers (04-R2-F1).
- [x] Check whether command APIs are coherent, composable, and hard to misuse. → mostly yes; the gap is no central handler registry (a new RuntimeAPI method silently unreachable if the dispatch entry is forgotten — 04-R2-F7).
- [x] Check whether runtime/control code depends on UI concepts or MCP-specific assumptions. → **clean**: RuntimeAPI is transport-agnostic (`CommandResult`), control→UI only via the abstract `ui_command_sink.h` interface + shared `graph_snapshot.h`; MCP thinly forwards (validates once); `runtime_command_sink` wraps (not duplicates).
- [x] Identify code that is correct today but fragile under likely tool-surface, module, package, or graph-schema changes. → adding an endpoint = +1 hand-written branch in the 136-way chain with copy-pasted validation (04-R2-F1/F2/F7). (The "partial modulation validation" candidate was refuted — structurally impossible.)
- [x] Produce refactor candidates with priority and expected payoff, separate from bug fixes. → see Round-2 Refactor Candidates below.

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

---

# Maintainability Re-Audit (Round 2) — 2026-06-05

Verify-gated maintainability pass per the Re-Audit Mandate (round 1 was correctness-focused). **7 candidates
→ 5 confirmed (2 Medium, 3 Low), 2 dismissed.** The headline is the **hand-rolled monolithic dispatch
layer** — `dispatch()` is a 136-branch `if/else` chain where every mutation handler copy-pastes the
field-presence/type-check/extract/error-wrap boilerplate. **Dependency direction is clean** (RuntimeAPI is
transport-agnostic; MCP forwards; `runtime_command_sink` wraps, not duplicates — all confirmed). The verify
pass refuted 2 candidates: a "variations catch-all" finding that **fabricated** sticky-note handlers (they
live in `graph.cpp`, not the cited file) and a lane-adjacent "partial modulation validation" claim that is
structurally impossible (`update_mod_assignment` only changes amount/polarity/curve, never source/dest).

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 04-R2-F1 | Medium | Maintainability | **Dispatch boilerplate duplication** — every mutation handler repeats `if(!root_valid) json_err` → per-field `.contains()/.is_string()/.is_number()` → `.get<T>()` → `command_result_to_json`; helpers exist (`json_ok`/`json_err`/`command_result_to_json`/`split_addr_local`) but **field extraction/validation is not factored** | `control_server_dispatch.cpp:94-111, 690-731, 732-778` (×136 handlers) |
| 04-R2-F2 | Medium | Maintainability | `dispatch()` is one **1916-line / 136-branch** `if/else` chain over `method ==` with no handler table/registry | `control_server_dispatch.cpp:17-1916` |
| 04-R2-F3 | Low | Maintainability | `control_server_internal.h` is a **917-line god-header** (36+ includes, enum→string converters, JSON builders, validation helpers, 20+ handler forward-decls) shared by 5 control TUs | `control_server_internal.h:1-917` |
| 04-R2-F4 | Low | Maintainability | `control_server_query.cpp` (2336 lines) mixes introspection + operator metadata + package browsing + build-console + source-search handlers in one TU with no module boundaries | `control_server_query.cpp:1-2336` |
| 04-R2-F7 | Low | Robustness | No central handler registry — adding a RuntimeAPI method but forgetting the `dispatch()` branch fails silently (`unknown method` at `:1910`); a registry/table would make missing wiring detectable | `control_server_dispatch.cpp:1909-1913` |

> 04-R2-F1/F2/F7 are facets of **one root cause** — the dispatch layer is a hand-rolled monolith. A single
> refactor (handler-table + a shared `extract_field` helper) addresses all three. 04-R2-F3 was filed Medium
> and **downgraded to Low** (it's a god-header, but well-sectioned). All findings are **non-lane** → fixable
> normally.

### Evidence & Recommendation

**04-R2-F1 / F2 / F7 — the monolithic dispatch layer** (Medium/Low, Maintainability)
- *Evidence:* `dispatch()` spans `control_server_dispatch.cpp:17-1916`; **136** `else if (method == "...")`
  branches (grep) plus read-only early-returns (38-49); the `unknown method` fallthrough is at 1910. Each
  mutation handler repeats the same validate→extract→wrap shape (e.g. `add_node` 94-103, `remove_node`
  104-111, `add_midi_mapping` 690-707, `add_mod_assignment` 732-752). Field extraction (`root["x"].get<T>()`
  guarded by `.contains()/.is_*()`) is **not** factored, though the response helpers are.
- *Impact:* adding/changing an endpoint = a new hand-written branch with copy-pasted validation; a change to
  the error envelope or validation rule means editing ~136 blocks; a forgotten branch silently disables a
  method for MCP clients; no test guards the per-handler validation shape.
- *Recommendation (one refactor, addresses F1+F2+F7):* (a) a generic
  `template<class T> ExtractResult<T> extract_field(const json&, const char* name)` (+ a small required/
  optional set), shrinking each handler body to a few lines; (b) a `std::unordered_map<std::string,
  Handler>` registry populated once, replacing the 136-branch chain and making missing wiring detectable.
  **High ROI but LARGE** (136 handlers) — stage it (helper first, migrate handlers in batches, table last),
  behavior-neutral, guarded by the control tests + a new error-shape-invariance test.

**04-R2-F3 — `internal.h` god-header** (Low) — split into focused headers (`enum_converters.h`,
`json_helpers.h`, `validation_helpers.h`, and keep handler decls in `internal.h`). Reduces the
include-everything ripple across the 5 control TUs. Priority low, payoff medium.

**04-R2-F4 — `query.cpp` mixed concerns** (Low) — split by domain
(`control_server_introspection.cpp` / `_metadata.cpp` / `_package.cpp` / `_source.cpp`). Priority low,
payoff medium; pairs with per-domain test files.

### Refactor Candidates (priority + payoff — separate from bug fixes)
1. **Dispatch handler-table + `extract_field` helper** (04-R2-F1/F2/F7) — **priority medium, payoff high**,
   but **large** (136 handlers → staged migration). The one structural win; behavior-neutral; needs an
   error-shape-invariance test to guard it.
2. **Split `control_server_internal.h`** (04-R2-F3) — priority low, payoff medium.
3. **Split `control_server_query.cpp` by domain** (04-R2-F4) — priority low, payoff medium.

### Test Gaps (refactor-safety)
- No test for dispatch field-validation paths (missing field, type mismatch e.g. `cc` as string), nor for
  **error-response-shape invariance** across handlers — a dispatch-boilerplate refactor could silently
  change the error envelope undetected. Add before/with the F1/F2 refactor.

### Dismissed (verification-refuted)
- **04-R2-F5** (`runtime_api_variations.cpp` catch-all incl. sticky notes) — refuted: **fabricated** — the
  sticky-note handlers it cites are **not in that file** (they live in `graph.cpp`); the split is coherent
  by domain.
- **04-R2-F6** (modulation lane-shape validation partial — `update_mod_assignment` skips the check) —
  refuted: `update_mod_assignment` only changes amount/polarity/curve, never source/destination, so it is
  **structurally incapable** of introducing a shape violation. (Lane-adjacent; correctly dismissed.)

## Round-2 Follow-up — ✅ **DONE 2026-06-05** (full refactor; behavior-neutral, parity-proven)
Merged to master (`2b99ef03`), staged across `dispatch shape guard → handler-table → header split → query
split`:
- **F1/F2/F7:** `dispatch()` is now a **144-entry handler-table registry** (was a 1916-line / 136-branch
  if/else); each handler is a non-capturing lambda over a shared `DispatchContext`; the
  `require_*`/`optional_*` field helpers (in `control_server_validation.h`) collapse the per-handler
  validation boilerplate; `dispatch_legacy` removed. Missing wiring is now structural (a method is reachable
  iff it's in the table). Migration done in 5 build+test-gated batches via subagents.
- **F3:** `control_server_internal.h` (917) split into `control_server_enums.h` (126) /
  `control_server_json.h` (49) / `control_server_validation.h` (242); internal.h (→622) includes them. Pure
  moves.
- **F4:** `control_server_query.cpp` (2336→1695); source-search/build handlers → `control_server_query_source.cpp`
  (355), package/catalog/update handlers → `control_server_query_packages.cpp` (300). CMake updated.
- **Guard:** new `tests/control/.../dispatch_shape.inc` pins the response-envelope contract; the only
  intended behavior change is **more specific per-field error messages** (envelope shape preserved; no test
  depended on the old strings). Parity proven by the full `tests/control/` suite + `test_demo_graphs`.
