# Phase 0 — Post-Switch Audit Baseline And Triage Setup

## Scope Reviewed

Phase 0 establishes the baseline for the current audit only. It does not attempt subsystem signoff.

This pass answers:

- what codebase is now under audit
- what the current architecture assumptions are
- what is already known vs newly discovered
- what counts as a release blocker for the current audit
- where later findings should be recorded

Primary current-architecture references:

- `docs/EMBEDDED-OPERATOR-SLOTS.md`
- `docs/SIMPLIFICATION-AND-CONSOLIDATION.md`
- `docs/ARCHITECTURE-GUARDRAILS.md`
- `docs/runtime/*.md`
- `docs/release/*.md`
- `docs/testing/*.md`

## Evidence Gathered

### Current repo state at audit start

- branch: `master`
- worktree: not clean
- current in-flight change families include:
  - runtime and operator API changes related to the role-binding removal / embedded-composition switch
  - UI and snapshot-model cleanup
  - demo graph updates
  - test updates
  - documentation restructuring and audit reset work

### Architecture assumptions at audit start

The codebase under audit now assumes:

- no role bindings
- owned embedded composition for host-local behavior
- ordinary ports for graph transport
- explicit outputs for graph-visible sharing

### Historical boundary

- the previous audit of the earlier role-binding-era architecture now lives in:
  - `docs/audit-history/role-binding-era/`
- that older audit can inform what to inspect, but it is not release evidence for this audit

## Known Issues At Audit Start

### 1. The worktree is mid-switch, so this audit starts from an actively changing codebase

- Classification: `known at audit start`
- Current read:
  - this is acceptable, but it means all later findings should distinguish architectural changes in flight from newly discovered defects

### 2. Release confidence must be re-established for the current architecture

- Classification: `known at audit start`
- Current read:
  - the earlier audit covered a materially different architecture
  - current release confidence must come from fresh evidence gathered against the embedded-composition-era codebase

### 3. Demo, preset, and inspector behavior need fresh verification under the new model

- Classification: `known at audit start`
- Current read:
  - these were heavily affected by the switch and should be treated as high-signal areas in the new audit

## Release Blocker Rubric

Treat any of the following as a release blocker unless explicitly reclassified:

1. crash, hang, data loss, or graph corruption
2. broken save/load/reload or broken host-local state restoration
3. broken package/operator loading or rebuild flow in core workflows
4. broken export or release/update path
5. major unusable UI workflow
6. graph truth or host-local composition behaving differently from the current architectural contract

Non-blocking issues can still be tagged as:

- `required before release`
- `deferred`

## Audit Phase Map

### Phase 1 — Runtime Core Stability

- startup / shutdown
- rebuild / reload / scheduler / audio / GPU lifecycle
- hot reload and runtime coherence

### Phase 2 — Graph Correctness And Mutation Surfaces

- graph serialization
- runtime mutations
- control-server mutation paths
- undo/redo
- embedded-slot / host-state correctness
- signal-port discipline
- snapshot consistency

### Phase 3 — Domain Pipelines And Cross-Domain Behavior

- control/audio/GPU/media behavior
- domain bridges
- timing-sensitive and analysis-sensitive workflows
- explicit-output correctness across domains

### Phase 4 — UI And Interaction Audit

- node graph editing
- inspector system
- overlays and choosers
- session/variation workflows
- post-switch layout/readability/interaction resilience

### Phase 5 — Operator/Package/Ecosystem Audit

- loader and ABI surfaces
- package lifecycle
- metadata fidelity
- package authoring and extension workflows

### Phase 6 — Export, Release Surfaces, And Final Readiness

- export pipeline
- app update path
- demo graphs and shipped examples
- release checklist alignment
- final blocker/defer decision

## Signoff Status

- `pending`
