# Post-Migration Clean-Break Cleanup Plan

## Summary

Phases 1-7 have landed the fixed-cadence architecture, but the repo still contains visible fossils of the previous dual-cadence model.

If the goal is for a newcomer to experience the codebase as if it had always been designed this way, the remaining work is not about behavior. It is about **removing conflicting stories** from:

- canonical docs
- active code comments and API language
- internal helper types that still present themselves as dual-cadence
- migration docs that are now stale or too prominent

This cleanup should be treated as one short, explicit convergence pass after the functional migration is complete.

## Cleanup Tracks

### 1. Rewrite the canonical docs so they teach only the fixed-cadence model

Update the docs a new contributor is most likely to read first:

- `docs/PRD.md`
- `docs/ARCHITECTURE.md`
- `docs/vivid-runtime-architecture.md`
- `docs/runtime/architecture.md`
- `docs/runtime/runtime_core.md`
- `docs/runtime/audio_engine.md`
- `mcp/opdev_docs/core_api.md`
- `mcp/opdev_docs/control_domain.md`
- `mcp/opdev_docs/audio_domain.md`
- `mcp/opdev_docs/advanced.md`

Make these changes consistently:

- replace `VIVID_PORT_SIGNAL` with `VIVID_PORT_SCALAR`
- replace `VIVID_PORT_AUDIO` with `VIVID_PORT_AUDIO_BUFFER`
- remove references to `CadenceBridge` and replace them with `AudioFrameBridge`
- remove `cadence_capability`, `audio-capable`, `dual-cadence`, and promotion language
- describe operators as fixed-cadence:
  - frame-only
  - audio-only
  - gpu
- describe frame/audio crossings as explicit bridge edges only
- update all example code snippets to use the fixed-cadence names and current port constants

Also update diagrams under `docs/diagrams/` that still show:

- `CadenceBridge`
- dual-cadence terminology
- old port names

The canonical doc set should become self-consistent enough that the migration docs are no longer required to understand the runtime.

### 2. Scrub active code comments and API language that still teach the old model

The active source tree still contains comments that describe promotion and audio-capable operators. Remove or rewrite those comments so the code explains only the current model.

Priority files:

- `src/operator_api/operator.h`
- `src/runtime/subgraph_module.cpp`
- `src/runtime/audio_executor.cpp`
- `src/runtime/graph_compiler.cpp`

Required changes:

- remove “audio-capable” terminology
- remove comments that describe frame-to-audio promotion
- remove comments that describe `SCALAR` outputs as writing audio buffers “when promoted”
- update helper comments so they describe explicit bridge handling and fixed-cadence execution only

This is important because many contributors will learn the system from comments in these files before they read the migration notes.

### 3. Remove or fully reframe the remaining internal dual-cadence helper types

The biggest remaining code-level fossil is the internal operator reuse layer.

Current examples:

- `operators/control/lfo/lfo.h`
- `operators/control/envelope/envelope.h`
- `operators/control/smooth/smooth.h`
- `src/operator_api/embedded_op.h`

These still present themselves as retained dual-cadence types for internal embedding. That may be technically workable, but it prevents the codebase from reading as natively fixed-cadence.

Make this cleanup explicit:

- stop retaining internal helper types that implement both `FrameProcessable` and `AudioProcessable`
- move shared logic into cadence-neutral implementation helpers (`*_core.h`, utility structs, or pure helper functions)
- keep the public execution wrappers cadence-specific:
  - `<name>_fr`
  - `<name>_au`
- update `ChildOp` / embedded-op support so it embeds:
  - a fixed frame operator implementation, or
  - a fixed audio operator implementation
  instead of an old dual-cadence class
- rename helper fields and comments in `embedded_op.h` so they describe whether an embedded operator has an audio interface, not whether it “uses audio cadence” in the old sense

The target state is:

- no internal class remains whose primary identity is “the old dual-cadence operator”
- shared behavior exists only as implementation reuse, not as a preserved historical abstraction

### 4. Demote the migration docs from active guidance to historical record

Right now `docs/fixed-cadence-migration/` still reads like the main explanation for the architecture, and some review notes inside it are already stale.

Make the migration folder explicitly historical:

- update stale review notes so they no longer contradict current code
- add a short banner to `docs/fixed-cadence-migration/fixed-cadence-migration.md` saying this directory is an implementation record for the migration, not the primary source of runtime truth
- once the canonical docs are updated, move the fixed-cadence migration directory under an archive path such as:
  - `docs/archive/fixed-cadence-migration/`

After that move:

- remove links to the migration docs from any primary architecture doc
- keep them available only as historical implementation notes

This preserves history without making the old architecture part of the newcomer path.

## Acceptance Criteria

The cleanup is complete when all of the following are true:

- a newcomer can read `docs/PRD.md`, `docs/ARCHITECTURE.md`, and the runtime docs without encountering:
  - `CadenceBridge`
  - `VIVID_PORT_SIGNAL`
  - `VIVID_PORT_AUDIO`
  - `cadence_capability`
  - `audio-capable`
  - `dual-cadence`
  - promotion language
- active code comments do not describe the old execution model
- internal helper code no longer preserves old dual-cadence operator types as first-class abstractions
- migration docs are clearly archival, not the default explanation of the system

Use grep gates on active code and primary docs. Outside the archive directory, these should return no hits:

- `CadenceBridge`
- `CadenceOverride`
- `VividCadenceCapability`
- `AUDIO_CAPABLE`
- `VIVID_PORT_SIGNAL`
- `VIVID_PORT_AUDIO\\b`
- `audio-capable`
- `dual-cadence`
- `cadence_capability`
- `input_float_values`
- `output_float_values`

## Assumptions and Defaults

- This cleanup is about **contributor clarity**, not runtime compatibility.
- Backward compatibility is not required.
- Historical migration notes may remain in the repo, but only under an explicit archive path.
- The preferred end state is not merely “the old model is disabled.” It is “the old model is no longer the way the repo explains itself.”
