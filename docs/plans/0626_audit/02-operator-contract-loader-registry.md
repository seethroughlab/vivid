# Audit 02: Operator Contract, Loader & Registry

**Date:** 2026-06-26
**Status:** Audited 2026-06-04 (verify-gated; 9 candidates → 4 confirmed + 1 verifier-surfaced, 5 dismissed)

## Purpose

Audit the public operator contract and runtime operator lifecycle for authoring friction, descriptor correctness, stale artifact detection, and registry/loader consistency.

## Scope

- `src/operator_api/`
- `src/runtime/operators/`
- `docs/runtime/operator_loader.md`
- `docs/OPERATOR-LOADING.md`
- `src/operator_api/CLAUDE.md`
- Operator loader, registry, descriptor, source-doc, and creation tests
- Representative seed operators used as contract examples

## Primary Questions

- [ ] Is the public operator API minimal, clear, and stable enough for generated operators?
- [ ] Do runtime headers leak implementation details that operators should not depend on?
- [ ] Are descriptors validated consistently before runtime use?
- [ ] Does `operator_codegen` own the `extern "C"` boundary without duplicate manual registration paths?
- [ ] Does stale artifact detection catch realistic mismatch cases without pretending to guarantee binary compatibility?
- [ ] Are operator type lookup, aliasing, metadata, and diagnostics consistent across seed and package operators?
- [ ] Are authoring errors reported in a way an LLM or user can act on?

## Subsystem Checklist

- [ ] Review `operator.h`, domain mixins, port/type headers, and editor helper headers for public surface creep.
- [ ] Trace dylib load/probe/registration from file discovery to registry lookup.
- [ ] Inspect descriptor validation and hash/staleness checks for clear failure modes.
- [ ] Compare source-derived docs with runtime operator metadata and MCP `operator_docs`.
- [ ] Check operator scaffolding paths for generic names, package placement, and reusable output.
- [ ] Verify tests cover missing symbols, stale ABI, bad descriptors, duplicate types, and package operator precedence.
- [ ] Identify contract docs that are stale or too implicit for LLM-generated operators.

## Audit Checklist

- [ ] Read the relevant subsystem docs and navigation guides.
- [ ] Inspect the main source files and ownership boundaries.
- [ ] Review tests that claim to cover the subsystem.
- [ ] Check docs/code/test contract drift.
- [ ] Identify correctness, robustness, and maintainability findings.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Findings

Split per Completion Criteria into **public-API / contract** risks and **loader/registry
implementation** risks.

### Public API & operator contract

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 02-F5 | Medium | Maintainability | `VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR` and `VIVID_REGISTER` duplicate ~80% of the ABI scaffolding; a fix to one path won't propagate to the other | `src/operator_api/operator.h:537-695` & `715-960` |
| 02-F10 | Low | Correctness | The non-codegen `VIVID_REGISTER` fallback builds a descriptor without the v3 metadata fields (`display_name`/`keywords`/`summary`) — a concrete instance of the F5 divergence | `src/operator_api/operator.h:715-817` |
| 02-F3 | Low | Docs | Descriptor-validation error codes are inline string literals (not named constants) and undocumented; message formats are inconsistent (index- vs name-based) | `src/runtime/operators/operator_descriptor_validation.cpp:24-186`; no validation doc in `operator_api/CLAUDE.md` |

### Loader / registry implementation

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 02-F4 | Low | Robustness | `operator_descriptor_hash()` (the lockfile fingerprint) excludes v3 metadata; the header doc-comment claims it changes on "any semantic-metadata change", which overstates coverage | `src/runtime/operators/operator_descriptor_hash.{h:11-14,cpp:60-83}` |
| 02-F7 | Low | Test gap | `validate_descriptor()` (~25 issue codes) has no test exercising its codes/messages | `src/runtime/operators/operator_descriptor_validation.cpp:19-189`; no `tests/.../test_operator_descriptor_validation.cpp` |

> Severities reflect the verify pass: 02-F3 and 02-F4 were filed Medium and **downgraded to Low**
> (the validation diagnostics are already surfaced as structured `{code,message}` via the
> `validate_operators` MCP tool, and the hash gap is presentation-only — graphs still bind/run).
> 02-F10 was **surfaced by the verifier while refuting the (dismissed) docs finding 02-F9**: the
> finding misframed it as a docs gap, but the underlying code defect is real.

### Evidence & Recommendation

**02-F5 — Duplicate ABI scaffolding across the two registration macros** (Medium, Maintainability)
- *Evidence:* `VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR` (operator.h:537-695, ~159 lines, used by
  codegen-generated `*_generated_registration.cpp`) and `VIVID_REGISTER` (operator.h:715-960, the
  non-codegen fallback, becomes a no-op when `VIVID_CODEGEN_ACTIVE`) define near-identical
  `_VividInstance`, `_vivid_sync_params` (byte-identical incl. the FILE/TEXT branch), the three
  `_vivid_dispatch_*` templates, and `vivid_create/destroy/process_*/main_thread_update/prepare_instance_assets`.
  Verified: `_vivid_sync_params` and `vivid_main_thread_update` appear in both bodies.
- *Impact:* A bug fixed in one macro path silently lags in the other; an author building without codegen
  gets different behavior than a codegen build. No current correctness defect — a maintenance/bug-propagation risk.
- *Recommendation:* Extract the shared ABI scaffolding into one internal implementation both macros wrap;
  or deprecate `VIVID_REGISTER` in favor of requiring codegen. If it stays, add a sync-warning comment.

**02-F10 — `VIVID_REGISTER` omits v3 metadata** (Low, Correctness)
- *Evidence:* In `VIVID_REGISTER`'s `_vivid_get_descriptor()` (operator.h:715-817) the descriptor sets
  `desc.name = ClassName::kName` but never populates `display_name`/`keywords`/`summary`, even though the
  SFINAE getters (`get_display_name<>` / `get_keywords_*<>` / `get_summary<>`) exist and the codegen path
  uses them. Spot-check confirmed: no `display_name`/`keywords`/`summary` assignment in that range.
- *Impact:* An operator built via the non-codegen fallback presents with no display name/keywords/summary.
  Low because codegen is the standard (and effectively sole) build path — `VIVID_REGISTER` is legacy/unused
  in practice — so this is latent. It is, however, a concrete proof of the 02-F5 divergence.
- *Recommendation:* Populate the v3 fields in `VIVID_REGISTER` via the existing SFINAE getters (fold into
  the 02-F5 consolidation so both paths share one descriptor-build).

**02-F3 — Validation codes/messages unstandardized and undocumented** (Low, Docs)
- *Evidence:* `validate_descriptor()` returns `{code,message}` with inline literal codes (`"missing_name"`,
  `"duplicate_param_name"`, …, operator_descriptor_validation.cpp:24-186); messages mix index-based
  (`"param[0] is missing a name"`) and name-based (`"param 'name' is file/text but default_string is null"`).
  Codes aren't exported as constants; `operator_api/CLAUDE.md` documents no validation contract.
- *Impact:* Operator authors / LLMs lack a reference for what makes a descriptor valid; codes could be
  renamed without notice. Mitigated by the structured `validate_operators` MCP surface
  (`control_server_query.cpp:2102`, `vivid_mcp.py:5630`) which already emits `{code,message}`.
- *Recommendation:* Hoist codes to named `static const` strings in the validation header; add a
  `docs/OPERATOR-DESCRIPTOR-VALIDATION.md` listing each code → condition → fix → example, referenced from
  `operator_api/CLAUDE.md`. Normalize message style (always name the offending param/port).

**02-F4 — Descriptor hash excludes v3 metadata; doc-comment overclaims** (Low, Robustness)
- *Evidence:* `operator_descriptor_hash()` (operator_descriptor_hash.cpp:60-83) hashes name, flags,
  params, ports (incl. per-param `semantic_*`) but **not** the operator-level `display_name`/`keywords`/
  `summary`. It is the lockfile drift detector (`operator_registry.cpp:10-13` → `project_lockfile.cpp:372`,
  mismatch is Critical at :584-593). The header comment (`.h:11-14`) says it changes on "any
  semantic-metadata change", which a reader could take to include v3 metadata.
- *Impact:* A `kDisplayName`/`kSummary`-only change passes lockfile checks. Presentation-only — the
  graph still binds and runs identically; excluding it is defensible. The real nit is the misleading comment.
- *Recommendation:* Either include the v3 fields (keywords sorted for determinism) and bump a hash-version
  comment, **or** (preferred) leave behavior as-is and correct the header comment to state the hash covers
  the binding-relevant interface only, not presentation metadata.

**02-F7 — No tests for descriptor-validation codes/messages** (Low, Test gap)
- *Evidence:* `validate_descriptor()` emits ~25 distinct codes (null/missing/duplicate names, choice-label
  consistency, file/text defaults, custom-port type names, 11 uniform-layout checks). Repo-wide grep:
  `validate_descriptor`/`DescriptorValidationIssue` appear only in source, never in `tests/`. (The nearby
  `test_operator_descriptor_hash.cpp` tests a *different* function.)
- *Impact:* A future refactor could weaken a message (drop the name/index) or drop a check with no
  regression catch. Pure coverage gap — the logic itself reads correct and runs at load/registry/query time.
- *Recommendation:* Add `tests/ops/test_operator_descriptor_validation.cpp` exercising each code with a
  triggering descriptor and asserting the code + an actionable message.

### Test Gaps

(02-F7 above is the verified subset; the broader list, reported separately from implementation findings:)

- Descriptor validation at load time — exercise `DescriptorValidationIssue` paths, not just load pass/fail.
- `operator_descriptor_hash()` stability across v3-metadata changes (tie to the 02-F4 decision).
- Hot-reload **workflow** with descriptor field *reordering* (not just count changes) — `classify_hot_reload`
  is unit-tested, but `OperatorRegistry::reload_operator()` end-to-end with descriptor changes is not.
- Custom port type registration (`vivid_describe_custom_types`) end-to-end through load/create/process.
- `validate_descriptor()` uniform-layout edge cases (zero-size struct, misaligned offsets, contradictory
  transport+payload_size).
- Operator aliasing (`resolve_operator_alias`) with realistic legacy→modern rename + param-rewrite scenarios.
- Codegen `VIVID_DEFINE_OP` metadata extraction **error** cases (malformed block, missing class name).
- Metadata propagation (`lane_behavior`, `strategy_independent`) across codegen → descriptor → registry for
  multi-lane / control-domain operators.

### Docs to Update

- `src/operator_api/CLAUDE.md` — add a "Validation Contract" pointer (codes + how to fix); reference the new
  validation doc. (02-F3)
- `docs/OPERATOR-DESCRIPTOR-VALIDATION.md` *(new)* — enumerate every `validate_descriptor()` code with
  condition / fix / failing example / fix example. (02-F3)
- `docs/OPERATOR-LOADING.md` — note that descriptors **are** validated on dylib load (loader.cpp:302) and
  errors surface via `last_error()`. (counters the dismissed 02-F2's false premise.)
- `src/runtime/operators/operator_descriptor_hash.h:11-14` — correct the "any semantic-metadata change"
  comment to scope it to the binding-relevant interface. (02-F4)
- `src/operator_api/operator.h:~697` — comment that `VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR` and
  `VIVID_REGISTER` duplicate ABI scaffolding; changes must be applied to both (until consolidated). (02-F5)

## Follow-up

**Immediate** — none. No confirmed High-severity / production-path issue.

**Near-term**
- 02-F5 + 02-F10: consolidate the two registration macros onto one shared ABI-scaffolding implementation
  (or deprecate `VIVID_REGISTER`); ensure the single descriptor-build populates v3 metadata via the SFINAE
  getters. Closes both the duplication risk and the metadata-omission bug.

**Backlog**
- 02-F3: hoist validation codes to named constants + author `docs/OPERATOR-DESCRIPTOR-VALIDATION.md`.
- 02-F4: decide include-vs-document for the hash; correct the header comment either way.
- 02-F7: add `test_operator_descriptor_validation.cpp`; work through the broader test-gap list.
- Apply the doc updates above.

### Dismissed (verification-refuted)

Five candidates were refuted by the verify pass:

- **02-F1** (ABI "covers add/remove but not reorder") — false distinction; the integer
  `VIVID_OPERATOR_ABI_VERSION` is the *only* mechanism for **all** incompatible changes equally, by design
  (a documented hot-reload staleness detector, not a compatibility promise). No coverage gap.
- **02-F2** ("`load()` never calls `validate_descriptor()`") — **false**: `operator_loader.cpp:302` calls it
  and returns false (`dlclose`, `set_last_error("invalid_descriptor", …)`) on any issue, including the exact
  `param_count=1, params=null` example the finding claimed would crash.
- **02-F6** (ABI-boundary docs "implicit") — already documented: `operator_api/CLAUDE.md:33-41` formalizes the
  three-layer design and states operators never include runtime headers; `OPERATOR-LOADING.md` covers ABI versioning.
- **02-F8** ("no `OperatorCreator` test") — **false**: `tests/ops/test_operator_creator.cpp` (745 lines, 20
  tests, registered at `cmake/tests/10-runtime-control-graph.cmake:314`) covers name validation, collisions,
  filesystem errors, all three domains, custom ports. Only compile-and-load of the generated op is absent.
- **02-F9** (VIVID_DEFINE_OP "not documented with examples") — already documented (operator.h:962-974 comment
  + CLAUDE.md:43-51 + 40+ live examples). *But its refutation surfaced the real 02-F10 bug above.*

## Completion Criteria

- [x] Findings table is filled in or explicitly marked with no findings.
- [x] Public API risks are separated from loader/registry implementation risks.
- [x] Operator-authoring friction points are captured with concrete examples.
- [x] Test gaps are mapped to loader, registry, descriptor, and API-contract behavior.
- [x] Follow-up work is grouped into immediate, near-term, and backlog.
