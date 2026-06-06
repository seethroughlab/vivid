# Clean-Break Lane System Redesign Plan

## Summary

Replace the current lane system with a first-class value and multiplicity model. In the new system, "many values" are not represented by special port types, side-channel lane buffers, bridge-only scratch state, or execution-strategy glue. Instead, every runtime value carries:

- payload type: float, audio, texture, string, or custom
- multiplicity: scalar or many
- identity metadata: none, positional, or stable IDs
- storage policy: CPU, GPU, audio block, bridge slot, string storage, or custom storage

This is a clean break from the old system. The implementation should remove the old lane API rather than shim it. Old package operators, old graph lane surfaces, and old lane-oriented runtime paths should fail loudly until migrated.

The goal is to make multiplicity a general property of values across all domains, not a separate transport model that only some payloads understand.

## Public Interface Changes

Replace lane-specific public surfaces with payload type plus multiplicity metadata.

- Remove public dependency on `VIVID_PORT_LANE_ARRAY` and `VIVID_PORT_STRING_LANES`.
- Remove old lane-specific operator context surfaces such as `VividLaneView`, `VividLaneOutput`, `VividStringLaneView`, and `VividStringLaneOutput`.
- Introduce value views and output builders with a shared envelope, for example `VividValueView` and `VividValueOutput`, plus typed helpers for float, audio, texture, string, and custom payloads.
- Add explicit value metadata fields for payload type, multiplicity, identity mode, lane/value count, storage kind, and flags.
- Treat audio channel count as audio payload layout, not value multiplicity.

Operators should declare multiplicity behavior directly instead of relying on runtime lane heuristics.

- `ScalarOnly`: accepts one value and emits one value.
- `Map`: applies the same operation per element and preserves identity.
- `Reduce`: consumes many values and emits one value.
- `Generate`: emits many values from scalar/control input.
- `Collect`: aggregates several scalar inputs into one many-valued output.
- `Preserve`: forwards multiplicity and identity unchanged where applicable.

The operator ABI and graph schema should be bumped as part of the transition. Old operators and old graph lane surfaces should be rejected with actionable diagnostics rather than silently adapted.

## Phased Implementation

### Phase 0: Design Lock — ✅ DONE 2026-06-05

**Deliverables:** the canonical contract [`docs/runtime/value-model.md`](../runtime/value-model.md) (4 value
axes + 6→7 multiplicity behaviors + locked enum names + identity semantics + the **complete old→new mapping
table** from a full catalog of every current lane surface + the migration checklist) and the **inert**
vocabulary header `src/operator_api/value_model.h` (locked enums; `#include`d by nothing yet — Phase 1 wires
it). No execution-code change; build green.

**Audit findings folded in** (per `project_lane_value_clean_break` memory): **01-R2-F7** (bridge >1024 clamp →
bridge value slots + explicit bounded overflow), the **round-1 lane-state-across-recompile gap** (→
`VividIdentityMode::StableIds` keys state on the identity token, not the node index), and **02-R2-F3** (→
unify the operator registration contract at the Phase-1 ABI bump; `VIVID_OPERATOR_ABI_VERSION` is currently
5). Two open decisions surfaced for sign-off: keep **Kernel** as a 7th behavior (don't fold into Map/Reduce);
make **IdentityMode 3-valued** (None/Positional/StableIds).

Original Phase-0 spec (now satisfied by the above):

Write the new value/multiplicity contract before changing execution code.

- Define the canonical vocabulary in `src/operator_api` and the runtime docs.
- Decide exact enum names for value type, multiplicity, identity mode, storage kind, and operator multiplicity behavior.
- Document the invariant that multiplicity belongs to values, not to payload-specific port types.
- Document that graph edges carry value type plus multiplicity, while operators declare what multiplicities they accept and produce.
- Document how identity behaves for map, reduce, generate, collect, and preserve operations.
- Add a migration checklist for seed operators, demo graphs, MCP tools, UI inspection, package scaffolding, and docs.

Phase 0 is complete when the old-to-new concept mapping is explicit and no naming or semantic decisions are left to later phases.

### Phase 1: New API Skeleton

Introduce the new public API and descriptor shape before wiring it into runtime execution.

- Add new public value view/output structs in `src/operator_api`.
- Add new descriptor fields for payload type and multiplicity acceptance.
- Add operator multiplicity behavior declarations to descriptors.
- Update descriptor probing and `operator_codegen` so generated boundaries expose the new contract.
- Bump the operator ABI so stale old operators are rejected.
- Add compile examples for scalar, map, reduce, generate, collect, preserve, and string-many operators.

Phase 1 is complete when new descriptors are visible through registry/probing, new API examples compile, and old operators fail for the right reason once the ABI bump is active.

### Phase 2: Compiler Rewrite

Replace lane propagation with value type and multiplicity inference.

- Replace lane-set planning with a value-flow pass.
- Make compiled edges carry payload type, multiplicity, identity lineage, and storage requirements.
- Derive node evaluation mode from operator multiplicity behavior and domain constraints.
- Remove lane execution strategy as a public planning concept.
- Separate audio channel negotiation from value multiplicity inference.
- Emit compile diagnostics for incompatible multiplicity, identity loss, unsupported reductions, and bridge capacity risks.

Phase 2 is complete when the compiler can plan mixed scalar/many graphs without using `VIVID_PORT_LANE_ARRAY`, and audio channel count inference is independent from multiplicity.

### Phase 3: Unified Runtime Storage

Replace lane-buffer-centered transport with general value storage.

- Add runtime-internal `ValueRef` and `ValueBuffer` storage for float, audio, texture, string, and custom payloads.
- Add per-domain arenas or pools for frame, audio, GPU, and bridge ownership.
- Replace bridge lane slots with fixed-capacity bridge value slots.
- Preserve real-time safety: audio execution must not allocate, grow vectors, lock, or perform descriptor lookup.
- Move GPU promotion to value storage policy instead of lane-specific special handling.
- Add runtime health counters for bridge overflow, identity truncation, and dropped many-valued payloads.

Phase 3 is complete when frame/audio/bridge storage can represent scalar and many values uniformly, and no runtime path needs separate float-lane and string-lane storage concepts.

### Phase 4: Frame Executor Migration

Move frame, control, and GPU execution onto the new value model.

- Replace frame executor lane normalization with value preparation.
- Pass new value views to operators.
- Implement map, reduce, generate, collect, and preserve execution paths.
- Preserve identity through map and preserve operators.
- Require explicit identity behavior for reductions and collections.
- Update GPU value routing so many textures and many float controls use the same multiplicity path.

Phase 4 is complete when control and GPU operators can consume and produce scalar or many values through the new API, scalar graphs still behave the same after operator migration, and frame execution no longer depends on `input_lane_refs`, `output_lane_refs`, or lane set IDs.

### Phase 5: Audio Executor And Bridge Migration

Move audio execution and cross-cadence transport to the same value model.

- Replace audio lane metadata with value views plus audio payload layout.
- Keep channel count negotiation separate from multiplicity.
- Pre-plan all audio value slots at compile time.
- Ensure map-style audio operators can run per value without runtime allocation.
- Ensure reductions from many audio/control values into audio-rate control are explicit.
- Replace audio bridge lane slots with value bridge slots.
- Add tests for dynamic bridge overflow and recovery.

Phase 5 is complete when the audio callback uses precomputed value slots only, audio channel layout and multiplicity cannot be confused by compiler or operator API, and bridge overflow produces bounded behavior plus visible diagnostics.

### Phase 6: Operator, Graph, UI, MCP, And Docs Migration

Migrate the product surface in one coordinated sweep.

- Update seed operators in `operators/audio`, `operators/control`, `operators/gpu`, and `operators/shared`.
- Update scaffolding templates so newly generated operators use the new API only.
- Update checked-in graph JSON descriptors and demo graphs to use payload type plus multiplicity metadata.
- Update UI port rendering to show multiplicity independently from payload domain.
- Update inspector views to display value count, identity mode, and storage kind.
- Update MCP and control-server graph mutation tools so they no longer create lane-specific ports or assume old lane names.
- Update runtime graph docs, operator authoring docs, package docs, and LLM integration docs.

Phase 6 is complete when all checked-in operators and graphs use the new model, UI and MCP can create/inspect/connect/mutate many-valued ports, and no new user-facing path emits old lane terminology except migration notes.

### Phase 7: Legacy Removal

Delete the old lane system rather than maintaining a compatibility layer.

- Remove `VIVID_PORT_LANE_ARRAY`, `VIVID_PORT_STRING_LANES`, old lane views, old lane outputs, lane set IDs, lane execution strategies, and old lane bridge slots.
- Remove or rewrite stale tests that validate old lane behavior.
- Remove old docs or rewrite them as migration notes.
- Add `rg`-based cleanup checks for forbidden active-code terms.
- Keep historical mentions only in archived docs or explicit migration documentation.

Phase 7 is complete when active source no longer depends on old lane-specific surfaces, old operators fail at descriptor/probe time with actionable ABI/schema messages, and runtime has exactly one multiplicity model.

### Phase 8: Hardening And Performance

Treat the new model as production runtime infrastructure.

- Add performance benchmarks for scalar graphs, many-valued frame graphs, audio many-valued graphs, and bridge-heavy graphs.
- Compare against the current baseline before legacy removal where possible.
- Add stress tests for high multiplicity, identity churn, graph recompilation, hot reload, and bridge overflow.
- Audit allocation behavior in audio paths.
- Add debug visualization for value flow and identity lineage if it can be done without delaying the core migration.

Phase 8 is complete when scalar-path performance has no meaningful regression without explicit acceptance, the audio path remains real-time safe, and multiplicity diagnostics are easier to act on than the current lane diagnostics.

## Test Plan

Add compiler tests for:

- scalar-to-scalar flow
- scalar-to-many generation
- many-to-many map behavior
- many-to-one reduce behavior
- collect behavior
- preserve behavior
- incompatible multiplicity diagnostics
- identity loss diagnostics
- audio channel count independent from value multiplicity

Add operator API tests for:

- descriptors
- probing
- codegen
- scaffolding
- ABI rejection for old operators
- typed helpers for float, audio, texture, string, and custom values

Add runtime tests for:

- frame execution
- audio execution
- GPU promotion
- string many-values
- custom payload many-values
- bridge transport
- bridge overflow
- graph recompilation
- hot reload

Add migration and regression tests for:

- representative demo graphs migrated from the old lane system
- seed operators in each domain
- control-server graph mutation
- MCP graph mutation
- UI/inspector representation of multiplicity

Add real-time safety checks for the audio callback:

- no allocation
- no locks
- no descriptor lookup
- no unbounded growth

Add cleanup checks using `rg` for removed public surfaces and old lane-specific terms in active source.

## Acceptance Criteria

- Multiplicity is represented as metadata on first-class runtime values, not as lane-specific port types or side-channel lane buffers.
- The runtime has one value transport model for scalar and many-valued payloads.
- Old lane public surfaces are removed from active operator API and runtime code.
- Old package operators and old graph lane surfaces fail loudly with actionable ABI or schema diagnostics.
- All checked-in seed operators build against the new API.
- All checked-in demo graphs are migrated.
- UI, inspector, control server, MCP, docs, and scaffolding use the new vocabulary.
- Audio channel layout is independent from value multiplicity.
- Audio execution remains real-time safe.
- Bridge overflow and capacity limits are explicit, bounded, and observable.
- Tests cover compiler inference, operator API contracts, frame execution, audio execution, bridge behavior, graph migration, and legacy cleanup.

## Assumptions

- A clean break is acceptable: old packages and operators must be updated rather than supported through runtime compatibility.
- Checked-in graphs and seed operators will be migrated in the same project, not lazily over time.
- A one-time source/schema migration helper is acceptable, but runtime compatibility shims are not.
- User-visible graph behavior should remain equivalent where the old behavior was well-defined.
- macOS-first remains the target; cross-platform concerns should not block the redesign unless they affect public API shape.
- The implementation should prefer a smaller, explicit new contract over preserving old naming or concepts for familiarity.
