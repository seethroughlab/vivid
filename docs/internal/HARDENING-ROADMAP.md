# Vivid Hardening Roadmap

## Purpose

This roadmap turns the completed audit into a practical hardening sequence.
It is not a feature roadmap. It is a stability, correctness, and maintainability roadmap based on the concrete audit findings and residual gaps recorded in `docs/internal/CODE-AUDIT-TRACKER.md`.

The priorities here assume the current architecture remains in place and that we want to improve confidence without pausing forward progress.

## Guiding Principles

1. Preserve the transactional runtime boundary.
2. Expand regression coverage where the audit found seam fragility.
3. Prefer contract clarification over implicit behavior.
4. Keep package and tooling surfaces aligned with core runtime behavior.
5. Treat editor/runtime consistency as a product feature, not UI polish.

## Priority Levels

- `P0`: can crash, corrupt runtime state, or silently invalidate user work
- `P1`: likely regression source or weak contract that should be hardened before major new features
- `P2`: meaningful confidence or maintainability improvement, but not blocking near-term work
- `P3`: cleanup/documentation/tooling improvements that make future work safer and faster

## Phase 1: Lock In The New Runtime Guarantees

Priority: `P0`

### Goal

Make the runtime guarantees established during the audit hard to regress.

### Scope

- Add regression coverage around the transactional rebuild paths we hardened:
  - package install/link/rebuild/unlink with live graphs
  - `reload()` and `apply_snapshot_json()` failure rollback
  - hot reload compatibility guard behavior
  - audio cross-domain snapshot integrity for float/custom/spread inputs
- Add one focused regression suite for the export path:
  - custom port type registration in standalone export
  - final output path correctness
  - sidecar copy placement
- Add coverage for app-update lifetime behavior and capture failure reporting.

### Why this is first

The audit fixed several seam failures, but some of those fixes are still protected only by targeted tests or by manual verification. The fastest way to keep the runtime from drifting back is to lock those guarantees into automated coverage.

### Acceptance criteria

- Export path has direct automated regression coverage.
- Runtime rollback and refresh paths are exercised through failure cases, not only happy paths.
- App-update and capture failure behavior are tested explicitly.
- No audit-fixed runtime seam depends only on manual verification.

## Phase 2: Finish Package-Test Contract Hardening

Priority: `P1`

### Goal

Make package validation consistent across core runtime expectations, manifests, and package-local CI.

### Scope

- Expand `PackageTestRunner` so it can cover more package-local C++ tests without depending on bespoke repo-local CMake wiring.
- Define and document the intended boundary between:
  - lightweight manifest-declared package tests
  - heavier package-local test binaries
- Add validation in core so manifest-declared `tests.cpp` entries fail clearly when the generic runner cannot support them.
- Add one package-ecosystem smoke job in core CI that exercises a representative linked package set against the current package contract.

### Why this is next

The sibling-repo pass cleaned up the package manifests and workflows, but the core package test surface is still weaker than the package-local environments in some repos. That is the biggest remaining ecosystem structural gap.

### Acceptance criteria

- `PackageTestRunner` supports the common package test shapes we expect packages to declare.
- Unsupported package test shapes fail deterministically with actionable messages.
- Package manifests, package CI, and core package-test semantics no longer drift by default.
- We have at least one automated ecosystem-level check, not only repo-local package CI.

## Phase 3: Tighten UI / Runtime Contract Surfaces

Priority: `P1`

### Goal

Reduce the remaining implicit behavior between the retained UI and runtime state.

### Scope

- Formalize `GraphSnapshot` as a documented contract surface:
  - broken connection representation
  - error state representation
  - package/browser snapshot semantics
  - metadata used for inspector/tooltips
- Add regression coverage for UI command transactional behavior:
  - chooser insertion
  - broken-wire visibility
  - package-browser list refresh behavior
- Decide whether additional UI mutations should return structured `CommandResult` instead of `void` and normalize the command boundary where useful.
- Audit the new text-edit subsystem integration so keyboard behavior is consistent across all popup/editor fields.

### Why this matters

The audit showed that UI bugs were really contract bugs: invisible broken wires, blind mutations, unsynchronized package-state reads. The recent fixes were good, but this seam still deserves one explicit hardening pass.

### Acceptance criteria

- `GraphSnapshot` behavior is documented and tested for broken-state visibility.
- Critical multi-step UI mutations are result-aware or deliberately fenced.
- Text editing and field focus behavior are consistent across the editor.
- Package-browser state is snapshot-driven everywhere it matters.

## Phase 4: Strengthen Loader / ABI / Custom-Port Tooling

Priority: `P1`

### Goal

Reduce the chance of subtle plugin/authoring regressions as the operator/package ecosystem grows.

### Scope

- Add explicit tests for:
  - stable custom type metadata in introspection
  - loader failure behavior for malformed custom-type registration
  - deferred probe fidelity across more descriptor fields
- Extend operator scaffolding validation to catch more contract mistakes at creation time.
- Improve tooling/docs around custom-port authoring:
  - stable type ids
  - `CUSTOM_VALUE` vs `CUSTOM_REF`
  - audio-safe payload expectations
- Consider a small loader diagnostics surface in control-server/MCP for package/plugin debugging.

### Why this is here

The core custom-port hardening moved the architecture in the right direction, but long-term usefulness depends on package authors getting clear, enforceable rules and good diagnostics.

### Acceptance criteria

- Custom-port authoring errors fail early and clearly.
- Introspection exposes enough metadata to debug custom-port mismatches.
- Loader failure modes are covered and user-visible.
- Package authors have one clear custom-port contract to follow.

## Phase 5: Runtime Docs As Living Engineering Contract

Priority: `P2`

### Goal

Keep the new runtime and audit documentation accurate enough to support day-to-day development.

### Scope

- Review the new `docs/runtime/` set against current code after the recent hardening commits settle.
- Remove any remaining duplicated or stale architectural guidance from older docs.
- Add a small “how to update runtime docs when changing core behavior” rule to `AGENTS.md`.
- Keep `CODE-AUDIT-TRACKER.md` lightweight by converting completed audit outcomes into stable reference docs where appropriate.

### Why this matters

The audit produced a much better shared map of the system. If those docs drift immediately, we lose much of the value.

### Acceptance criteria

- Runtime docs match current code for the major subsystems.
- `AGENTS.md` points developers to the right runtime docs and expectations.
- The audit tracker remains a tracker, not a permanent dumping ground for architecture docs.

## Phase 6: Long-Run Reliability And Stress Validation

Priority: `P2`

### Goal

Validate that the system behaves well under sustained use, not just unit/integration scenarios.

### Scope

- Add longer-running stress scenarios for:
  - package link/rebuild/unlink churn
  - hot reload churn
  - repeated graph load/reload/new/save-as cycles
  - long-running audio + GPU + control mixed graphs
- Add a small set of “stability demos” that serve as long-run smoke fixtures.
- Track regressions in:
  - reload serial correctness
  - audio underruns
  - package reload behavior
  - editor responsiveness under repeated graph mutation

### Why later

This phase is valuable, but it depends on the earlier contract hardening so we are not stress-testing moving goalposts.

### Acceptance criteria

- We have a repeatable stress suite for the highest-risk seams.
- Long-run regressions are easier to catch before they show up in manual testing.
- The system remains stable under repeated mutation-heavy workflows.

## Phase 7: Cleanup And Simplification Pass

Priority: `P3`

### Goal

Remove temporary complexity introduced while moving quickly through the rewrite and audit.

### Scope

- Revisit audit-era helper additions and simplify where the final pattern is now clear.
- Prune obsolete comments, stale compatibility assumptions, and dead paths exposed by the audit.
- Normalize naming where temporary stopgap terminology still leaks through.
- Fold small one-off diagnostics into consistent shared helpers where that improves clarity.

### Why last

Cleanup is valuable, but it should follow hardening, not compete with it.

### Acceptance criteria

- Temporary workaround code is reduced where safe.
- Naming and diagnostics are more consistent.
- The codebase is easier to navigate after the audit-era fixes settle.

## Recommended Execution Order

1. Phase 1: Lock In The New Runtime Guarantees
2. Phase 2: Finish Package-Test Contract Hardening
3. Phase 3: Tighten UI / Runtime Contract Surfaces
4. Phase 4: Strengthen Loader / ABI / Custom-Port Tooling
5. Phase 5: Runtime Docs As Living Engineering Contract
6. Phase 6: Long-Run Reliability And Stress Validation
7. Phase 7: Cleanup And Simplification Pass

## Suggested Near-Term Milestone

If we want a concrete “next checkpoint,” it should be:

### Hardening Milestone A

- runtime rollback/export/package mutation regressions fully covered
- package test contract clarified and partially generalized in core
- UI/runtime seam documented and regression-protected for broken wires and chooser mutations

That milestone would put Vivid in a much safer place to continue feature work without reopening the same class of boundary bugs.

## Out Of Scope For This Roadmap

These are important, but they are not the primary focus of this hardening roadmap:

- major new operator features
- redesigning the movie/media architecture again
- large UI redesign work
- broad packaging/distribution product strategy changes
- cross-platform expansion beyond what current contracts already imply

## Relationship To The Audit Tracker

Use this roadmap together with:

- `docs/internal/CODE-AUDIT-TRACKER.md`
- `docs/CODE_REVIEW.md`
- `docs/internal/CODE-REVIEW-PHASE*.md`

The tracker records what happened.
This roadmap says what to harden next.
