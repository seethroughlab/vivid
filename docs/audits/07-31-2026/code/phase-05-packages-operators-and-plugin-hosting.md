# Phase 5: Packages, Operators, And Plugin Hosting

Status: done (audited 2026-07-31)

## Verdict

**PASS with follow-ups — no new P0/P1** (the plugin runtime-crash P0 is Phase 2's P0-01,
fixed in PR #190). The native operator/package system is release-grade: one shared
`validate_descriptor` for built-in and packaged ops (parity by construction, packaged ops
validated *more*), a **rollback-first hot-reload** that validates before touching the graph
and rejects layout-incompatible swaps cleanly (no stale param pointers), and a deterministic,
atomically-persisted plugin scan cache with out-of-process probing. The weak area is
**hosted-plugin parameter metadata**: it is inconsistent across surfaces (normalized vs
plain, live-value vs authored-base, name vs index vs id) and its pins/authored-base are
index-keyed, so a plugin rescan silently misaligns or drops them — while native operators
key on `name` everywhere and are immune. Findings are 6×P2 + 5×P3, concentrated on the
plugin surface — reinforcing Phase 2's recommendation to state plugin support precisely.

## Purpose

Verify that built-in operators, external packages, hot reload, and hosted audio plugins behave like a
release-supported extension system rather than a development-only mechanism.

## User Task

Browse, load, edit, fail, reload, and remove operators or packages while the host remains stable and
the project remains understandable.

## Hypothesis

If extension boundaries are healthy, Vivid can ship with credible creative breadth without letting
bad content compromise the host.

## Pressure Test

Audit operator descriptors, package manifests, hot reload, file watching, validation, quarantine,
plugin scan/cache behavior, hosted plugin params, presets, and package CLI/control flows.

## Scope

- Built-in operators, packaged operators, package manifests, descriptor validation, package manager,
  compiler, file watcher, hot reload, quarantine, operator catalog, plugin scan/cache, VST3/CLAP
  hosting, plugin windows, params, presets, and operator-authoring docs.
- Package and plugin behavior visible through UI, CLI, and MCP/control APIs.

Out of scope: exhaustive third-party plugin certification unless a format is named as
release-supported.

## Audit Procedure

1. Inventory release-supported built-in operators, packaged operators, example packages, and hosted
   plugin formats.
2. Run descriptor and manifest validation against good, malformed, missing-field, and incompatible
   examples.
3. Exercise load, unload, hot reload, package rebuild/link/uninstall, and file watching while a
   project references affected operators.
4. Review plugin scan/cache flows for determinism, failure isolation, and user-visible diagnostics.
5. Trace parameter metadata from operator/plugin definition through UI, persistence, agent/control
   APIs, and reload.

## Evidence To Collect

- Operator/package inventory with release status.
- Validation output for representative good and bad content.
- Hot reload transcript with project state before and after.
- Plugin scan/cache notes, including failed plugin behavior.
- Param metadata trace for at least one built-in operator and one hosted plugin if supported.

## Deliverables

- Extension-system readiness report.
- Bad-content containment findings.
- Operator/plugin metadata consistency matrix.

## Acceptance Criteria

- Invalid operators and packages fail validation with actionable diagnostics.
- Hot reload cannot leave dangling runtime or UI references.
- Built-in and packaged operators follow the same descriptor and parameter rules.
- Plugin scan/cache behavior is deterministic enough for release support.
- External plugin windows and parameters remain isolated from host stability.

## Failure Modes

- A bad operator crashes the host or corrupts a project.
- Hot reload succeeds partially and leaves invisible stale state.
- Package metadata diverges from operator metadata.
- Plugin-hosted parameters cannot be saved, restored, or inspected consistently.

## Evidence Log

Method: two source sweeps (validation + hot-reload + scan/cache; and a full param-metadata
consistency matrix), building on Phase 1 (operator load/ABI) and Phase 2 (plugin isolation).
Paths relative to `app/src/`.

### A. Descriptor + manifest validation

`operator_descriptor_validation.cpp` (single `validate_descriptor`) — actionable diagnostics
naming the offending param/port: null descriptor, empty name, null param/port arrays,
duplicate param/port names, choice/file/audio-shape rules, and a full uniform-layout check.
**Built-in and packaged ops share this one function** (parity by construction — packaged
dylibs are validated *twice*: raw at load `operator_loader.cpp:199`, and rebuilt at
`create()`). Gaps: no numeric-range check (`min>max` or default-outside-range passes clean)
and no `param.type`/`port.type` enum-range check (→ P2-01). Manifest validation
(`package_manifest.cpp`) is actionable but first-error-only, doesn't name the offending
operator entry for missing name/source, and parses `abi` without checking it (enforced only
at load) (→ P3-01); `discover_packages` silently drops bad-manifest packages with no
diagnostic (→ P2-02).

### B. Hot-reload (rollback-first — clean)

`hot_reload_manager.cpp:124-155`: `validate()` runs **first**; a layout-incompatible reload
(param not append-only, or any port change — `operator_loader.cpp:28-59`) is rejected before
any node is touched, the node is badged, and the old op keeps running. Only on success does
it release instances (stashing params **by name**), load, invalidate the cached descriptor
(whose `char*` fields would dangle into the unloaded dylib), then rebuild and rematch params
by name. **No stale-param-pointer path found.** Compiled *audio* ops are deliberately
excluded from hot-watch (RT-thread safety) — a documented, acceptable gap. `unregister_type`
(`op_runtime.cpp:119-124`) has no code-level guard against retiring a live-referenced type —
convention-only, though a wrong caller degrades gracefully to `op_missing` (→ P3-02).

### C. Plugin scan / cache

Deterministic + atomic: keyed by path + `(mtime, size)` with a `kPluginCacheVersion`
invalidation, temp+rename save, sticky `kClassCrashed`, corrupt-cache→empty
(`plugin_cache.cpp`). Out-of-process probe with fd-3 verdict + 30 s timeout→SIGKILL +
crash-class cache + sentinel (Phase 2). Two gaps: catalog **row order is non-deterministic
for identically-named plugins** (tiebreak only on name length → falls back to `readdir`
order; UI addresses rows by index) (→ P3-03); and a plugin that **passes scan but fails
runtime instantiation** never demotes its cache/catalog verdict — scan verdict and load
reality diverge indefinitely, surfaced only as a transient error string
(`vst3_host_clap_loader.cpp:155-179`) (→ P2-03).

### D. Parameter-metadata consistency matrix (the deliverable)

**Native operators — consistent.** `ParamBase` (keyed by `name`) is the single source of
truth; the UI inspector reads the *same* live `ParamBase` the C-ABI descriptor mirrors, and
persistence, MCP, and hot-reload all key on `name`. Identity/range/default agree on every
surface; hot-reload rematches by name. Native audio ops likewise. Clean.

**Hosted plugins — inconsistent (→ P2-04, P2-05).** Divergences, all evidenced:
- *Range units differ per backend on the same API:* the curated graph-node inspector reports
  VST3 params as normalized `[0,1]` but CLAP as plain `[min,max]`, and stores `host_base`
  normalized (VST3) vs plain (CLAP) — so `range`/`value` mean different things by format
  (`vst3_host_params.cpp:83-94`; `host_base` `vst3_host_common.h:553` vs `clap_host.h:160`).
- *Same VST3 param described two ways:* normalized in the inspector vs plain in the
  `_vst3_params` catalog (`vst3_host_common.h:713-717`).
- *Two persistence keys + two value semantics:* the linear fx chain persists by numeric `id`
  and records the plugin's **live** value (bypassing the ADR-0030 authored base); the
  graph-node path persists by display **name** and records the authored base
  (`persist.cpp:131-139` vs `:200-205`; `vst3_host.cpp:4089-4100`).
- *Compacted-index instability:* the cached `params` vector skips hidden params, so its index
  has no stable meaning, yet `host_base`/`has_base` and curated **pins are index-keyed**
  (`vst3_host_common.h:672,548-555`). A rescan/`restartComponent` **resets every authored
  base to "not authored"** (`:684-687`) and re-points index-keyed pins — silently losing
  authored plugin-param values and misaligning curation. Visual pins persist by name
  (reorder-safe); audio/plugin pins by raw index (`persist.cpp:206-209` vs `:333-335`). This
  corroborates the known "rescan nukes VST3 state" behavior.
- `set_param_by_intent` resolves via three addressing modes — name (visual), index (graph
  audio), id-from-index (hosted device) (`control_handlers_introspection.cpp:1193-1208`).

### E. Findings

#### P2-01: Descriptor validation accepts nonsensical numeric ranges + garbage type enums
`validate_descriptor` never checks `min_value`/`max_value` (inverted or default-out-of-range
passes) nor bounds `param.type`/`port.type` enums. Bad content — the exact thing validation
exists to catch — loads clean. Fix: add range + enum-bound checks. Owner/status: Unassigned | P2.

#### P2-02: Bad-manifest packages are silently dropped
`discover_packages` skips any package whose manifest fails to parse with **no diagnostic**;
`install_package` never runs descriptor validation. ADR-0019 "nothing fails silently" gap for
the package surface. Fix: surface a diagnostic for dropped packages. Owner/status: Unassigned | P2.

#### P2-03: A plugin that passes scan but fails runtime instantiation is never demoted
The scan verdict is set only by the probe; a runtime `clap_load_plugin`/instantiate failure
writes only a transient `clap_last_error` — the catalog/cache keep the OK verdict and
re-attempt every load. Fix: on runtime instantiate failure, demote + persist the verdict.
Owner/status: Unassigned | P2.

#### P2-04: Hosted-plugin param metadata is inconsistent across surfaces
Normalized (VST3) vs plain (CLAP) range on the same API; VST3 range described two ways; live-
value vs authored-base persistence; name vs index vs id addressing (§D). For an MCP-native
product where agents drive params, an agent can't reason uniformly about a plugin param's
range/value. Fix: normalize the MCP/inspector contract (one unit convention + one addressing
key) across formats. Owner/status: Unassigned | P2.

#### P2-05: Plugin pins + authored base are index-keyed → rescan/reorder silently loses them
`host_base` and curated pins are aligned to the compacted `params` index, which is unstable
across rescan/`restartComponent` (resets base to "not authored", re-points pins), unlike
native ops (name-keyed). Authored plugin-param values are silently lost on rescan. Fix: key
plugin authored-base + pins by stable plugin param `id`, not compacted index. Owner/status:
Unassigned | P2 (corroborates the known rescan-nukes-VST-state issue).

#### P3-01: Manifest validation is first-error-only + doesn't name the operator + ignores `abi`
`package_manifest.cpp:25,36`. Fix: collect all errors, name the entry, sanity-check `abi`.
Owner/status: Unassigned | P3.

#### P3-02: `unregister_type` has no code-level live-reference guard
Convention-only (`op_runtime.cpp:119-124`); a wrong caller degrades gracefully to
`op_missing` but the invariant isn't enforced. Fix: assert/refuse on live references.
Owner/status: Unassigned | P3.

#### P3-03: Plugin catalog row order is non-deterministic for identically-named plugins
Tiebreak only on name length → `readdir` order; UI addresses rows by index
(`plugin_catalog.cpp:61-68`). Fix: add a total-order tiebreak (path). Owner/status:
Unassigned | P3.

#### P3-04: Load-time descriptor rejection surfaces only the first issue
`operator_loader.cpp:201` reports `issues.front()` only. Fix: surface all. Owner/status:
Unassigned | P3.

#### P3-05: Compiled audio operators are excluded from hot-reload (documented)
`hot_reload_manager.cpp:45-49` — deliberate RT-safety choice (audio-op edits need a project
reload). Not a defect; **document it for operator authors** so the asymmetry with visual-op
hot-reload isn't a surprise. Owner/status: Unassigned | P3 (docs).

## Open Questions (answered / flagged)

- **Which packages are bundled/supported/dev-only at first release?** Not code-derivable —
  the loader treats bundled, user, and project operators uniformly. Owner decision; recommend
  an explicit manifest of release-supported packages (the operator-audit harness, ADR-0042,
  is the natural gate for "supported").
- **Which plugin formats/capabilities are public release surface?** VST3 and CLAP are hosted
  equally, but the audit found the plugin *param* surface materially weaker than native ops
  (P2-04/P2-05) and Phase 2 found the runtime-crash P0 (now fixed, #190) plus a CLAP
  param-queue gap (P2-01). Recommend: state plugin hosting as supported **with the caveat**
  that authored plugin-param values are not rescan-stable until P2-05 lands; keep CLAP at
  parity only once its Phase-2 param-queue fix ships.
- **Should hot reload be documented for users/authors/maintainers?** For **operator
  authors** — it's an authoring affordance (ADR-0020), and the visual-vs-audio asymmetry
  (P3-05) must be documented so authors know audio-op edits need a project reload.

## Follow-Up Plans

- P2 fixes as their own gated PRs: descriptor range/enum validation (P2-01), surface
  dropped-package diagnostics (P2-02), demote plugins that fail runtime instantiation
  (P2-03), normalize the plugin param MCP/inspector contract (P2-04), re-key plugin
  authored-base + pins by stable param `id` (P2-05 — the highest-value plugin fix).
- P3 cleanups: manifest all-errors + `abi` check, `unregister_type` guard, catalog total
  order, all-issues on load rejection, and an operator-author note on the audio-op
  hot-reload exclusion (P3-05).
- Cross-refs: this phase's plugin findings compound Phase 2 (P0-01 fixed #190; CLAP param
  queue P2-01) and Phase 4 (P1-01 plugin-window UAF on undo; P1-02 missing-op param loss).
  The operator-audit harness (ADR-0042) is the natural per-operator release gate.
