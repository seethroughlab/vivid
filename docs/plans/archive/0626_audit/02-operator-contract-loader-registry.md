# Audit 02: Operator Contract, Loader & Registry

**Date:** 2026-06-26
**Status:** Re-audited (maintainability) 2026-06-05 (verify-gated; 10 candidates → 5 confirmed, 5 dismissed). Prior correctness pass retained below; Round-2 maintainability section at end.

## Purpose

Audit the public operator contract and runtime operator lifecycle for authoring friction, descriptor correctness, stale artifact detection, and registry/loader consistency.

## Re-Audit Mandate

The prior pass should be treated as a correctness/robustness audit, not a complete code-quality audit.
Run this audit again with equal weight on maintainability: structure, duplication, ownership boundaries,
API clarity, dependency direction, and ease of future change.

Do not mark the audit complete until every checklist item is annotated as `[x]` done, `[~]` partially
covered, or `[ ]` intentionally deferred with a short note. Findings must include both confirmed defects
and structural risks that make future defects likely.

## Scope

- `src/operator_api/`
- `src/runtime/operators/`
- `docs/runtime/operator_loader.md`
- `docs/OPERATOR-LOADING.md`
- `src/operator_api/CLAUDE.md`
- Operator loader, registry, descriptor, source-doc, and creation tests
- Representative seed operators used as contract examples

## Primary Questions

- [x] Is the public operator API minimal, clear, and stable enough for generated operators? → Mostly yes; round 1 found legacy macro drift (02-F5/02-F10), while round 2 found public-header minimality clean.
- [x] Do runtime headers leak implementation details that operators should not depend on? → No confirmed leak; the `operator_info_cache.h` → `ui/graph/graph_snapshot.h` hypothesis was refuted as a shared boundary value-type, not an operator-facing dependency.
- [x] Are descriptors validated consistently before runtime use? → Yes for live load/probe paths; round 1 added validation docs/tests and corrected descriptor-hash documentation.
- [x] Does `operator_codegen` own the `extern "C"` boundary without duplicate manual registration paths? → No; round 1 found macro duplication (02-F5), and round 2 found runtime registration-path divergence (02-R2-F3).
- [x] Does stale artifact detection catch realistic mismatch cases without pretending to guarantee binary compatibility? → Yes; ABI behavior was verified as a staleness detector, not a binary-compatibility promise.
- [x] Are operator type lookup, aliasing, metadata, and diagnostics consistent across seed and package operators? → Partially; descriptor metadata validation is improved, but registration/probe paths still diverge (02-R2-F3/02-R2-F9).
- [x] Are authoring errors reported in a way an LLM or user can act on? → Mostly yes after round-1 descriptor-validation docs/tests; remaining risk is registration-contract drift across operator paths.

## Subsystem Checklist

- [x] Review `operator.h`, domain mixins, port/type headers, and editor helper headers for public surface creep. → Public headers are acceptable; legacy registration macro duplication remains a maintainability risk.
- [x] Trace dylib load/probe/registration from file discovery to registry lookup. → Traced; four divergent registration/probe shapes are captured in 02-R2-F3.
- [x] Inspect descriptor validation and hash/staleness checks for clear failure modes. → Covered by round 1 fixes and `test_operator_descriptor_validation`.
- [x] Compare source-derived docs with runtime operator metadata and MCP `operator_docs`. → Covered in round 1; no remaining high-severity drift found.
- [x] Check operator scaffolding paths for generic names, package placement, and reusable output. → Covered; `operator_creator.cpp` is cohesive but oversized (02-R2-F1).
- [x] Verify tests cover missing symbols, stale ABI, bad descriptors, duplicate types, and package operator precedence. → Covered at integration level; round 2 still records refactor-safety gaps for `deep_copy_descriptor`, lazy upgrade, and cross-registration optional entry points.
- [x] Identify contract docs that are stale or too implicit for LLM-generated operators. → Round 1 added descriptor-validation documentation; round 2 recommends a unified ABI/entry-point contract doc.

## Audit Checklist

- [x] Read the relevant subsystem docs and navigation guides.
- [x] Inspect the main source files and ownership boundaries.
- [x] Review tests that claim to cover the subsystem.
- [x] Check docs/code/test contract drift.
- [x] Identify correctness, robustness, and maintainability findings.
- [x] Identify oversized files, mixed responsibilities, fragile seams, and unclear ownership.
- [x] Identify duplicated logic or repeated patterns that should be shared or intentionally documented.
- [x] Check dependency direction and public/private API boundaries.
- [x] Check whether tests make future refactors safe, not just whether they cover the latest fix.
- [x] Record findings with severity, category, evidence, and recommendation.
- [x] Propose immediate, near-term, and backlog follow-up work.

## Required Maintainability Review

- [x] Map the operator contract layers and identify APIs/macros/helpers that own too many responsibilities. → `operator_creator.cpp` god-file (02-R2-F1).
- [x] Look for duplicated descriptor, ABI, validation, metadata, source-doc, and registry lookup logic. → dlopen/dlsym resolution duplicated (02-R2-F9); the descriptor-reification "duplication" was refuted (different source types — 02-R2-F2).
- [x] Check whether public headers are minimal and whether runtime internals leak into operator-facing APIs. → clean. Operators don't include ui/control/package headers; the `operator_info_cache.h`→`ui/graph/graph_snapshot.h` include is a shared boundary *value-type*, not a layering violation.
- [x] Check whether codegen, fallback registration, package operators, and built-ins follow one coherent contract. → **NO** — four divergent registration paths (02-R2-F3, the headline).
- [x] Identify code that is correct today but fragile under likely operator API, metadata, or package changes. → adding an optional entry point needs ~5 coordinated edits; builtins are locked to 4 entry points (02-R2-F3).
- [x] Produce refactor candidates with priority and expected payoff, separate from bug fixes. → see Round-2 Refactor Candidates below.

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

---

# Maintainability Re-Audit (Round 2) — 2026-06-05

Verify-gated maintainability/code-quality pass per the Re-Audit Mandate (round 1 above was
correctness-focused). **10 candidates → 5 confirmed (1 Medium, 4 Low), 5 dismissed.** Dependency direction
and public-header minimality are **clean**. The verify pass refuted 5 candidates — including two of the
pre-audit recon hypotheses (the `operator_info_cache`→UI include is a shared **value-type**, not a layering
violation; the descriptor "duplication" is superficial — different source types) and three fabricated
test-gaps (`OperatorPreparationService` *does* have tests; the factory-preset test was mis-cited; an
"OOB clamp" claim that is factually inverted — the clamp precedes the loop).

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 02-R2-F3 | Medium | Maintainability | **No unified operator-registration contract** — the four runtime paths (codegen-dylib / builtin / WGSL / deferred-probe) diverge in entry-point support. `init_builtin` wires only 4 of ~19 entry points (rest `unload()`ed), so **builtin operators cannot export** process_audio/gpu/thumbnail/inspector/editor/etc.; adding a new optional entry point needs ~5 coordinated edits | `operator_registry.{h,cpp}` (`register_builtin`), `operator_loader.cpp` (`init_builtin` 394-404, `init_wgsl_operator` 405-502), `operator_registry_scan.cpp` (`scan_deferred`) |
| 02-R2-F1 | Low | Maintainability | `operator_creator.cpp` (1116 lines, single TU) owns name/port validation, codegen templates, CMakeLists patching, file IO, and editor launch | `src/runtime/operators/operator_creator.cpp:1-1116` |
| 02-R2-F9 | Low | Maintainability | dlopen/dlsym symbol-resolution is hand-rolled in two places (19 dlsym calls in the loader, 5 in the scanner) — same dlopen→dlsym→cast→check pattern, no shared resolver; a new entry point needs both updated | `operator_loader.cpp:254-387` + `operator_registry_scan.cpp:447-513` |
| 02-R2-F4 | Low | Test gap | `deep_copy_descriptor()` (~335 lines, anonymous-namespace, called only from `scan_deferred`) has no isolated unit test — metadata reification is only covered via integration | `operator_registry_scan.cpp:63-397`; `tests/ops/test_operator_loader.cpp` |
| 02-R2-F8 | Low | Test gap | `OperatorInfoCache` lazy-upgrade (re-fetch inspector/editor flags after a deferred op becomes fully loaded) has no isolated test | `operator_info_cache.h:63-86`; `tests/ops/test_operator_info_cache.cpp` |

> Severity adjustments by the verify pass: **02-R2-F1** Medium→Low (the file *is* divided by explicit
> section banners — "no clear boundaries" was overstated; it's a cohesive-but-dense god-file).
> **02-R2-F3** High→Medium (real divergence, but no live defect). **02-R2-F4** High→Low and **02-R2-F8**
> Medium→Low (integration tests partially cover the behavior; "zero tests" was overstated).

### Evidence & Recommendation

**02-R2-F3 — no unified registration contract** (Medium, Maintainability — *the headline*)
- *Evidence:* `init_builtin` (`operator_loader.cpp:394-404`) sets only `desc/create/destroy/process_frame`
  and `unload()`s the other ~15 function pointers, so a builtin operator can never expose audio/GPU/
  thumbnail/inspector/editor/MIDI entry points. The codegen-dylib path resolves ~19 symbols via dlsym, WGSL
  synthesizes a descriptor in memory, and deferred-probe reads a subset — four shapes, no shared contract.
  Adding one optional entry point (e.g. `vivid_prepare_assets_v2`) touches the codegen template, the loader
  dlsym block, `init_builtin`'s signature, `init_wgsl_operator`, and `scan_deferred` — ~5 edits, with no
  test asserting all paths support it.
- *Relationship:* this is the **runtime** sibling of the deferred round-1 **02-F5** (the
  `VIVID_REGISTER` vs `VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR` *macro* duplication) — F5 is the codegen/ABI
  scaffolding; F3 is the loader-side path divergence. Both point at "one operator-ABI contract."
- *Recommendation (refactor candidate):* document the operator-ABI/entry-point contract in one place
  (`docs/runtime/operator_loader.md` / a new `OPERATOR-ABI.md`); give builtin/WGSL a way to register optional
  entry points (e.g. a `VividOptionalEntryPoints` struct passed to `init_builtin`/`init_wgsl_operator`); add
  a test that each registration path honors a representative optional entry point. **Larger effort —
  sequence with the deferred sibling-codegen migration.**

**02-R2-F1 — `operator_creator.cpp` god-file** (Low, Maintainability)
- *Evidence:* one 1116-line TU spanning validation (903-955), templates (326-800), codegen (934-1000),
  CMake patching (818-902), file IO, editor launch (1098-1116). It *is* sectioned by banner comments, so the
  navigation cost is moderate; the issue is testability/ownership, not legibility.
- *Recommendation:* extract `OperatorValidator` / `OperatorCodeGenerator` / `CMakePatcher` / file-IO helpers
  into separate units. **Priority low** — already sectioned; do it opportunistically.

**02-R2-F9 — dlsym resolution not extracted** (Low, Maintainability)
- *Evidence:* `operator_loader.cpp:254-387` (19 dlsym) and `operator_registry_scan.cpp:447-513` (5 dlsym)
  repeat the dlopen→dlsym→reinterpret_cast→null-check pattern with the required/optional symbol list inlined
  in each.
- *Recommendation:* a small `resolve_operator_symbols(handle) → OperatorSymbols` helper (or an X-macro list
  of symbol names) so the required/optional symbol set lives in one place. Priority low, payoff medium —
  pairs naturally with the 02-R2-F3 contract work.

### Refactor Candidates (priority + payoff — separate from bug fixes)
1. **Unify the operator-registration/entry-point contract** (02-R2-F3, + 02-R2-F9 symbol list, + the
   deferred 02-F5 macro dedup) — **priority medium, payoff high.** The one structural item; best sequenced
   with the sibling-codegen migration. A single source of truth for the ABI entry points + a builtin/WGSL
   optional-entry-point mechanism removes the ~5-edit fan-out and the builtin capability gap.
2. **Extract a dlsym symbol-resolver** (02-R2-F9) — priority low, payoff medium; a self-contained first step
   toward #1.
3. **Split `operator_creator.cpp` by concern** (02-R2-F1) — priority low, payoff low/medium; it's already
   banner-sectioned, so this is cleanup, not unblocking.

### Test Gaps (refactor-safety)
- `deep_copy_descriptor` isolated field-reification test (02-R2-F4) — would catch a dropped field when the
  descriptor grows or the function is extracted.
- `OperatorInfoCache` lazy-upgrade isolated test (02-R2-F8).
- A cross-registration-path test that each path supports a representative optional entry point (would guard
  the 02-R2-F3 contract).

### Dismissed (verification-refuted)
- **02-R2-F2** (descriptor param/port reification "duplicated" in `init_wgsl_operator` vs
  `deep_copy_descriptor`) — refuted: different source types (`WgslOperatorConfig` vs C-ABI
  `VividParamDescriptor`) and different field sets; the similarity is structural, and a shared builder would
  fight the type difference. (Recon hypothesis #2 — correctly killed.)
- **02-R2-F5** (`OperatorPreparationService` zero tests) — refuted: tests exist; the "grep = 0 hits" claim
  is false.
- **02-R2-F6** (`OperatorLoader` move semantics untested) — refuted: the "integration-only / wouldn't catch
  a missing member" claim is false.
- **02-R2-F7** (factory-preset JSON minimal test) — refuted: cited the wrong line (Test 22f); the real
  Test 25 validates preset names/params.
- **02-R2-F10** ("clamp after null-check → OOB" in `deep_copy_descriptor`) — refuted: the clamp
  (`std::min(param_count, 256u)`) is computed *before* the null-check and loop; the claim is inverted.
- *(Recon hypothesis #1 — the `operator_info_cache`→`ui/graph/graph_snapshot.h` include — was answered in
  the question pass as **not a violation**: a shared boundary value-type, not a runtime→UI dependency.)*

## Round-2 Follow-up
- **Near-term / sequenced refactor:** 02-R2-F3 (unified registration contract + builtin/WGSL optional entry
  points) together with the deferred 02-F5 macro dedup. This is the headline structural item; it is not an
  emergency bug fix, but it should be the next Audit-02 design follow-up when operator-contract work resumes.
- **Standalone first step:** 02-R2-F9 (extract dlsym resolver) is the cleanest incremental move toward the
  unified contract and can be done before the larger codegen migration.
- **Backlog:** 02-R2-F1 (split `operator_creator.cpp` opportunistically); the 02-R2-F4/F8 refactor-safety
  tests.
