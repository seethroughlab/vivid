# Plan: Remove Role Bindings, Replace with Owned Modulation + Ordinary Ports

Follows `/docs/RETURN-TO-EMBEDDABLE-OPS.md`.

## Context

Role bindings were introduced (Mar 21-22) to replace the earlier `ChildOp<T>`-based embedded operator pattern (introduced Feb 28). The 7 GPU operators using role bindings (Particles, InstancedShapes, Flocking, Trails, Fluid, ReactionDiffusion, CellularAutomata) were all **created in the same commit series as role bindings** — they never existed before.

In practice, role bindings don't deliver on their promise of cross-host sharing:
- PER_VOICE operators (Particles, etc.) create private internal copies anyway
- SHARED operators can achieve the same result with ordinary wire connections
- The real audio-visual parity comes from spread ports and signal wiring, not shared role-bound nodes

The result is that presets, lifecycle, and copy/paste all require elaborate workarounds to make independent nodes behave like owned ones. The doc proposes removing role bindings entirely.

## Git history as removal guide

| Commit | Date | Description |
|--------|------|-------------|
| `d0f72f55` | — | **Pre-role-binding baseline** (last clean state) |
| `9dd2b3aa` | Mar 21 | Add role binding operator API (`BoundControlInstance`, descriptor fields) |
| `74cba1fb` | Mar 21 | Add role binding runtime infrastructure (scheduler, audio engine) |
| `553dba7e` | Mar 21 | Add role binding UI (drawing, interaction, overlay) |
| `aa8169ea` | Mar 21 | Add role binding support to Envelope, LFO, Clock |
| `b732800a` | Mar 21 | Add role binding tests |
| `e9f6065d` | Mar 21 | Build config + docs |
| `a44fc9a8` | Mar 22 | Multi-output support and output-aware candidates |
| `e6fa9745` | Mar 22 | Update role binding lines |
| `c9288dcc` | Mar 22 | Add role binding RPCs, load_graph, interface capture |

**Strategy**: For each infrastructure file, `git diff d0f72f55..HEAD -- <file>` shows exactly what was added for role bindings. Remove those additions. For the 7 GPU operators (which didn't exist pre-role-bindings), rewrite modulation sections with owned params while preserving rendering/simulation code.

### What stays from the role binding series

- **`ChildOp<T>` improvements** (audio child op support, spread sync) — these enhance the embedded pattern and are needed for owned modulation
- **`BoundControlInstance`** — useful for per-voice instantiation in the owned model (Particles creating N envelope instances)
- **New control operators** (MSEG, StepSeq, RandomSH, Macro) — these were introduced alongside role bindings but are useful as standalone graph nodes
- **The 7 GPU operators' rendering/simulation code** — shaders, compute pipelines, particle systems. Only the modulation plumbing changes.

### What gets deleted

- **All `role_bindings` in `NodeDef`** — `RoleBindingState`, serialization, graph JSON support
- **`VividRoleBindingDescriptor`** and `collect_role_bindings()` on the operator contract
- **`VividRoleBindingRuntimeConfig`** injection in scheduler/audio engine/GPU context
- **`set_role_binding` / `clear_role_binding`** APIs, control server handlers, MCP tools
- **Preset role binding machinery** — `RoleBindingPreset`, `PendingRoleAction`, `apply_preset_role_bindings()`
- **Role binding UI** — role chooser, "Referenced By" panel, role binding overlay, candidate lists
- **Role binding snapshot fields** — `RoleBindingSnapshot`, `ReferencedByEntry`, `RoleBindingInfo`
- **`is_bindable` flag** on operators, registry bindable-candidate resolution
- **`sync_role_binding_params()`** in scheduler
- **Role binding tests** — `test_role_binding_commands.cpp`, `test_role_binding_registry.cpp`
- **Role binding docs** — already archived
- **Demo graphs** — `particle_envelope_demo.json`, `instanced_shapes_demo.json` (rewrite with owned params)

## Replacement design (from the doc)

### Owned internal modulators

Each GPU operator gets host params with standard naming:

**LFO-backed roles**:
- `<role>_enabled`, `<role>_amount`, `<role>_rate`, `<role>_waveform`, `<role>_offset`

**Envelope-backed roles**:
- `<role>_enabled`, `<role>_amount`, `<role>_attack`, `<role>_decay`, `<role>_sustain`, `<role>_release`

Host operator owns lifecycle and trigger semantics. No external graph node involved.

### Ordinary signal ports

Where a generic external control value is useful (e.g., an LFO output modulating Fluid viscosity), expose an ordinary signal input port instead of a role slot.

### Explicit outputs

When host-local behavior must become graph-visible (e.g., particle trigger events), expose an output port rather than the internal mechanism.

## Execution phases

### Phase 1: Revert the 3 simplifications added this session
Revert only the auto-apply, cascade delete, and copy/paste awareness changes (added in this conversation). Keep the earlier uncommitted preset role-binding code for reference — it'll be removed in Phase 2 along with the rest.

Files to selectively revert (my changes only):
- `src/runtime/runtime_api.cpp` — revert auto-apply logic in `recall_preset()`
- `src/runtime/graph.cpp` — revert cascade delete in `remove_node()`
- `src/ui/node_graph.cpp` — revert copy/paste role binding awareness
- `mcp/vivid_mcp.py` — revert docstring updates

Execute all phases in one pass.

### Phase 2: Remove role binding infrastructure
Using `git diff d0f72f55..HEAD` per-file as a guide:
- `graph.h/cpp` — remove `RoleBindingState`, `RoleBindingPreset`, `OperatorPreset.role_bindings`, serialization
- `runtime_api.h/cpp` — remove `set_role_binding`, `clear_role_binding`, `apply_preset_role_bindings`, preset role binding logic
- `scheduler.h/cpp` — remove `RoleBindingRuntime`, `sync_role_binding_params`, role binding config injection
- `audio_engine.h/cpp` — remove role binding config plumbing
- `operator_registry.h/cpp` — remove bindable candidate resolution, factory preset role binding scanning
- `control_server.cpp` — remove role binding handlers
- `operator.h` — remove `collect_role_bindings()`, `VividRoleBindingDescriptor`
- `types.h` — remove `VividRoleBindingRuntimeConfig`, `VividRoleScope`, etc.
- `gpu_context.cpp/h` — remove role binding config injection
- `main.cpp` — remove role binding snapshot population, `ReferencedByEntry` building
- `graph_snapshot.h` — remove `RoleBindingSnapshot`, `ReferencedByEntry`, `RoleBindingInfo`
- UI files — remove role chooser, role binding drawing, "Referenced By" panel, overlay logic
- MCP — remove `apply_preset_role_bindings` tool, update `recall_preset` docstring
- Tests — delete `test_role_binding_commands.cpp`, `test_role_binding_registry.cpp`; remove role binding sections from `test_graph.cpp`, `test_control_server.cpp`, etc.

### Phase 3: Convert Envelope/LFO/Clock back from "bindable"
Remove the `is_bindable` / `create_bindable` / `destroy_bindable` from these operators. They remain normal graph operators — they just lose the special "I can be bound to a role" flag.

### Phase 4: Rewrite GPU operators with owned modulation
For each of the 7 operators:
1. Remove `collect_role_bindings()` descriptor
2. Add host params with `<role>_` prefix naming
3. Replace `VividRoleBindingRuntimeConfig` reads with internal `ChildOp<T>` or `BoundControlInstance` instances driven by the new host params
4. Keep all rendering/simulation/shader code unchanged

### Phase 5: Update demo graphs and factory presets
- Rewrite `particle_envelope_demo.json` and `instanced_shapes_demo.json` with owned-param format
- Update factory presets for the 7 operators
- Add `role_bindings` rejection to graph load (like the existing `embedded_ops` rejection)

### Phase 6: Verify
- Build compiles
- All existing tests pass (with role binding tests deleted)
- GPU operators still produce correct visual output
- Presets save/recall as flat param snapshots (no role binding machinery)
- Graph load rejects `role_bindings` with clear error

## Files to modify (complete list)

**Delete entirely**: `test_role_binding_commands.cpp`, `test_role_binding_registry.cpp`, `test_role_binding_commands.json`

**Heavy modification** (~14 files):
- `src/runtime/graph.h`, `graph.cpp`, `runtime_api.h`, `runtime_api.cpp`
- `src/runtime/scheduler.h`, `scheduler.cpp`
- `src/runtime/control_server.cpp`, `main.cpp`
- `src/operator_api/types.h`, `operator.h`
- `src/ui/graph_snapshot.h`, `node_graph_draw.cpp`, `node_graph_input.cpp`
- `src/runtime/operator_registry.h`, `operator_registry.cpp`

**Moderate modification** (~10 files):
- `src/runtime/audio_engine.h`, `audio_engine.cpp`
- `src/runtime/gpu_context.h`, `gpu_context.cpp`
- `mcp/vivid_mcp.py`
- UI files: `node_graph.h`, `node_graph.cpp`, `ui_command_sink.h`
- `src/runtime/runtime_command_sink.h`
- `src/runtime/operator_info_cache.h`

**Operator rewrites** (7 files):
- `operators/gpu/particles/particles.cpp`
- `operators/gpu/instanced_shapes/instanced_shapes.cpp`
- `operators/gpu/flocking/flocking.cpp`
- `operators/gpu/trails/trails.cpp`
- `operators/gpu/fluid/fluid.cpp`
- `operators/gpu/reaction_diffusion/reaction_diffusion.cpp`
- `operators/gpu/cellular_automata/cellular_automata.cpp`

**Operator cleanup** (3 files):
- `operators/control/envelope/envelope.h` — remove bindable
- `operators/control/lfo/lfo.h` — remove bindable
- `operators/control/clock/clock.h` — remove bindable

**Test updates** (~10 files):
- `test_graph.cpp`, `test_control_server.cpp`, `test_graph_snapshot_contract.cpp`, `test_signal_port.cpp`, `test_bound_control_instance.cpp`, `test_inspector_layout.cpp`, others with role binding sections
