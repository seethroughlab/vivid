# Replace Embedded Operator Slots with Role Bindings

## Context

The current "embedded operator slot" model has host operators owning hidden child instances (e.g. WavetableSynth owns per-voice Envelope instances). The new "role binding" model replaces this: hosts declare named roles, and the graph stores lightweight references to existing nodes. Bound nodes remain normal graph nodes — visible, editable, and reusable across multiple hosts.

This is a full replacement. The spec is at `docs/ROLE-BINDINGS.md`. The current embedded-op system spans 21 source files, 6 test files, 2 operator plugins, and 1 external package. All `embedded_*` public terminology must be removed.

Key architectural difference: embedded ops store `{type_name, params, bypassed}` per slot on the host. Role bindings store `{target_node_id, target_output_name}` per role — params live on the target node itself.

---

## Phase 1: New Types + Authoring Hook

**Goal:** Define new C ABI types and C++ hook. Old types coexist. No behavioral change.

### Files

| File | Change |
|------|--------|
| `src/operator_api/types.h` | Add `VividRoleBindingDescriptor` (same fields as `VividEmbeddedSlotDescriptor` but renamed: `role_id`, `runtime_scope` instead of `instance_scope`). Add `VividRoleBindingRuntimeConfig` (same shape as `VividEmbeddedSlotRuntimeConfig` but with `role_id` and `bound_node_type`/`bound_output_name`). Keep `VIVID_SLOT_SHARED`/`VIVID_SLOT_PER_VOICE` values as `VIVID_ROLE_SHARED`/`VIVID_ROLE_PER_VOICE` (same uint32_t values). Do NOT remove old types yet. Do NOT bump ABI yet. |
| `src/operator_api/operator.h` | Add `virtual void collect_role_bindings(std::vector<VividRoleBindingDescriptor>&) {}` to `OperatorBase`. Keep `collect_embedded_slots`. |

### Checkpoint
Build passes. All existing tests pass. New types compile but are unused.

---

## Phase 2: Graph Model + Serialization

**Goal:** Add `role_bindings` to the graph alongside `embedded_ops`. Both coexist.

### New types

```cpp
// In graph.h, on NodeDef:
struct RoleBindingState {
    std::string target_node_id;
    std::string target_output_name;
};
std::unordered_map<std::string, RoleBindingState> role_bindings; // role_id → state
```

### JSON format

```json
{
  "role_bindings": {
    "amp_env": { "target_node_id": "env_1", "target_output_name": "value" }
  }
}
```

### Files

| File | Change |
|------|--------|
| `src/runtime/graph.h` | Add `RoleBindingState` struct and `role_bindings` map to `NodeDef` |
| `src/runtime/graph.cpp` | Add JSON read/write for `role_bindings` key (parallel to `embedded_ops`) |

### Tests
- Add `role_bindings` round-trip serialization tests in `tests/test_graph.cpp`

### Checkpoint
Build passes. Graph can save/load role bindings. Existing embedded_ops serialization unchanged.

---

## Phase 3: Runtime Infrastructure (single commit)

**Goal:** Wire role bindings through registry → scheduler → audio engine → RuntimeAPI. Remove embedded-op equivalents. Bump ABI. This is the largest phase (~20 files) done as one atomic swap — intermediate states don't compile because the ABI types change.

### 3a. Registry + Loader rename

Rename the embeddable concept to bindable. No behavioral change beyond naming.

| File | Change |
|------|--------|
| `src/operator_api/operator.h` | Rename `VIVID_EMBEDDABLE` macro to `VIVID_BINDABLE`. Export new dlsym symbols: `vivid_create_bindable`/`vivid_destroy_bindable`. Replace `collect_embedded_slots` call in `VIVID_REGISTER` with `collect_role_bindings`. Replace all `s_embedded_slots`/`s_slot_*` storage vectors with `s_role_bindings`/`s_role_*`. Replace `desc.embedded_slot_count`/`desc.embedded_slots` with `desc.role_binding_count`/`desc.role_bindings`. Remove `collect_embedded_slots` virtual method from `OperatorBase`. |
| `src/operator_api/types.h` | Bump ABI to 12. Remove `VividSlotScope`, `VIVID_SLOT_SHARED`, `VIVID_SLOT_PER_VOICE`, `VividEmbeddedSlotDescriptor`, `VividEmbeddedSlotRuntimeConfig`. Remove `VividCreateEmbeddedFn`/`VividDestroyEmbeddedFn` typedefs, add `VividCreateBindableFn`/`VividDestroyBindableFn` (same signatures). On `VividOperatorDescriptor`: replace `embedded_slot_count`/`embedded_slots` with `role_binding_count`/`role_bindings` (pointing to `VividRoleBindingDescriptor`). On `VividAudioContext`/`VividProcessContext`: replace `embedded_slot_count`/`embedded_slots` with `role_binding_count`/`role_binding_configs` (pointing to `VividRoleBindingRuntimeConfig`). |
| `src/runtime/operator_loader.h` | Rename `create_embedded_fn_` → `create_bindable_fn_`, `destroy_embedded_fn_` → `destroy_bindable_fn_`. Rename accessors: `has_create_embedded` → `has_create_bindable`, etc. |
| `src/runtime/operator_loader.cpp` | Update member names. Update dlsym names to `vivid_create_bindable`/`vivid_destroy_bindable` (matching the new exported symbols). |
| `src/runtime/operator_registry.h` | Rename `EmbeddedDeleter` → `BindableDeleter`, `EmbeddedOpHandle` → `BindableOpHandle`, `EmbeddedSlotValidation` → `RoleBindingValidation` (kSlotNotFound → kRoleNotFound, kNotEmbeddable → kNotBindable, kTypeNotAllowed stays). Rename methods: `create_embedded` → `create_bindable`, `is_embeddable` → `is_bindable`, `embeddable_candidates` → `bindable_candidates`. On `DeferredEntry`: rename `embeddable` → `bindable`, rename all `slot_*` → `role_*` vectors, change `VividEmbeddedSlotDescriptor` → `VividRoleBindingDescriptor`. Rename `validate_embedded_assignment` → `validate_role_binding`. |
| `src/runtime/operator_registry.cpp` | Implement all renamed methods with same logic. `bindable_candidates()` takes `const VividRoleBindingDescriptor*`. |
| `src/runtime/operator_info_cache.h` | Replace `embedded_slots` iteration with `role_bindings` iteration on descriptor. Call `bindable_candidates()` instead of `embeddable_candidates()`. |

### 3b. Scheduler + Audio Engine

Replace embedded slot runtime with role binding runtime. Key difference: instead of reading params from `NodeDef::embedded_ops`, resolve the **target node's** current params from the graph/scheduler.

| File | Change |
|------|--------|
| `src/runtime/scheduler.h` | Replace `EmbeddedSlotRuntime` with `RoleBindingRuntime` (fields: `role_id`, `bound_node_id`, `bound_output_name`, `type_name`, `create_fn`, `destroy_fn`, `param_names`, `param_values`, `c_config`). Rename `embedded_slot_runtime` → `role_binding_runtime`, `embedded_slot_configs` → `role_binding_configs`. |
| `src/runtime/scheduler.cpp` | Remove `EmbeddedSlotRuntime::rebuild_c_view()`. Add `RoleBindingRuntime::rebuild_c_view()` (same logic, new field names). In `build()`: replace embedded_ops validation block. For each role declared by descriptor: check `ndef.role_bindings[role_id]`, resolve `target_node_id` → find that node's loader, get `create_bindable_fn`/`destroy_bindable_fn`, read target node's current param values from `NodeDef::params`. Call `validate_role_binding()`. Build flat `role_binding_configs` array. |
| `src/runtime/audio_engine.h` | Rename `EmbeddedSlotSnapshot` → `RoleBindingSnapshot`. Update `ParamSnapshot::embedded_slots` → `role_bindings`. |
| `src/runtime/audio_engine.cpp` | In `push_params()`: snapshot `role_binding_configs` from scheduler NodeState. In audio callback: pass `role_binding_count`/`role_binding_configs` to `VividAudioContext`. Update both processing paths (normal + channel-dup). Update snapshot resize in `build()` and `reload_operator()`. |

### 3c. RuntimeAPI + Command Sinks

Replace embedded-op commands with role binding commands.

| File | Change |
|------|--------|
| `src/runtime/runtime_api.h` | Remove 4 `set/clear_embedded_op*` methods. Add `set_role_binding(node_id, role_id, target_node_id, target_output_name)` and `clear_role_binding(node_id, role_id)`. |
| `src/runtime/runtime_api.cpp` | Remove embedded-op implementations. Implement `set_role_binding`: validate role on host descriptor, validate target node exists in graph, validate binding compatibility via `validate_role_binding()`, store in `ndef->role_bindings[role_id]`, set `pending_topology_change_ = true`. Implement `clear_role_binding`: erase from `role_bindings`, set `pending_topology_change_`. |
| `src/runtime/runtime_command_sink.h` | Remove 4 embedded-op overrides. Add `set_role_binding`/`clear_role_binding` overrides delegating to RuntimeAPI with undo capture. |
| `src/ui/ui_command_sink.h` | Remove 4 embedded-op virtual methods. Add `set_role_binding(node_id, role_id, target_node_id, target_output_name)` and `clear_role_binding(node_id, role_id)` virtuals. |

### 3d. Operators + Package

Update the two embeddable operators and the WavetableSynth consumer.

| File | Change |
|------|--------|
| `operators/control/envelope/envelope.cpp` | `VIVID_EMBEDDABLE(Envelope)` → `VIVID_BINDABLE(Envelope)` |
| `operators/control/lfo/lfo.cpp` | `VIVID_EMBEDDABLE(LFO)` → `VIVID_BINDABLE(LFO)` |
| `vivid-wavetable/src/wavetable_synth.cpp` | `collect_embedded_slots()` → `collect_role_bindings()` with `VividRoleBindingDescriptor` (field renames: `slot_id` → `role_id`, `instance_scope` → `runtime_scope`). Config sync: read from `ctx->role_binding_configs` / `ctx->role_binding_count` instead of `ctx->embedded_slots`. `slot_index_for_id()` → `role_index_for_id()`. `#include "embedded_control_instance.h"` → `#include "bound_control_instance.h"`. `EmbeddedControlInstance` → `BoundControlInstance`. |
| `tests/stubs/envelope_stubs.cpp` | `VIVID_EMBEDDABLE` → `VIVID_BINDABLE` |

### 3e. Tests

Update all embedded-op tests to use new APIs and terminology.

| File | Change |
|------|--------|
| `tests/test_embedded_control_instance.cpp` → rename to `tests/test_bound_control_instance.cpp` | Update include to `bound_control_instance.h`, rename `EmbeddedControlInstance` → `BoundControlInstance` throughout. |
| `tests/test_embedded_registry.cpp` → rename to `tests/test_role_binding_registry.cpp` | Test `create_bindable()`, `is_bindable()`, `bindable_candidates()`, `validate_role_binding()`. |
| `tests/test_embedded_op_commands.cpp` → rename to `tests/test_role_binding_commands.cpp` | Test `set_role_binding`, `clear_role_binding`. Graph must have target nodes for bindings to reference. Remove param/bypass command tests (those operations don't exist on bindings — params live on the target node). |
| `tests/operators/test_op_with_slots.cpp` → rename to `tests/operators/test_op_with_roles.cpp` | Use `collect_role_bindings()`. |
| `tests/graphs/test_embedded_commands.json` → rename to `tests/graphs/test_role_binding_commands.json` | Replace `embedded_ops` with `role_bindings` JSON format. Must include target nodes that the bindings reference. |
| `tests/test_graph.cpp` | Remove old `embedded_ops` serialization tests. Keep `role_bindings` tests from Phase 2. |
| `CMakeLists.txt` | Update test target names, source files, `add_test` registrations. |

### Checkpoint
Build passes. All tests pass with new terminology. Role bindings functional end-to-end through RuntimeAPI. WavetableSynth reads role binding configs at runtime. No `embedded_slot`/`embedded_op` references remain in public API surfaces.

---

## Phase 4: UI Layer

**Goal:** Replace embedded-op inspector UI with role binding UI.

### Snapshot changes

| File | Change |
|------|--------|
| `src/ui/graph_snapshot.h` | Remove `EmbeddedSlotInfo`. Add `RoleBindingInfo` (role_id, label, runtime_scope, accepted_domain, allowed_operator_types, candidates). Remove `OperatorInfo::embedded_slots`, add `OperatorInfo::role_bindings`. Remove `NodeSnapshot::EmbeddedOpSnapshot`/`embedded_ops`. Add `NodeSnapshot::RoleBindingSnapshot` (role_id, target_node_id, target_output_name, target_type_name) and `role_bindings` vector. |
| `src/runtime/main.cpp` | Replace embedded_ops snapshot population with role_bindings: iterate `op_info->role_bindings`, look up `ndef->role_bindings[role_id]`, resolve target node type name from graph. |

### Inspector changes

| File | Change |
|------|--------|
| `src/ui/node_graph.h` | Remove `embedded_*` UI state (rects, chooser). Add `role_binding_*` equivalents: assign/clear/jump rects, chooser state (open, node_id, role_id, items, selection). Replace `draw_inspector_embedded_slots`/`draw_embedded_chooser` declarations with `draw_inspector_role_bindings`/`draw_role_chooser`. |
| `src/ui/node_graph.cpp` | Update state initialization/reset for renamed members. |
| `src/ui/node_graph_draw.cpp` | Remove `draw_inspector_embedded_slots()` (~180 lines). Replace with `draw_inspector_role_bindings()`: for each declared role, draw role label + scope badge + bound node chip (or "Bind..." button) + clear button + "Jump To" button. **No embedded param sliders** — params are edited on the target node's own inspector. Implement `draw_role_chooser()`: list compatible existing graph nodes (filtered by domain + allowed types + compatible outputs). Offer "Create & Bind" at bottom when no compatible node exists. |
| `src/ui/node_graph_input.cpp` | Remove embedded slot interaction (slider drag, chooser popup, assign/clear/bypass clicks). Add role binding interaction: bind click opens node chooser, clear removes binding, jump-to-node selects and scrolls to target. |

### Checkpoint
Build passes. Inspector shows role binding sections. Binding/clearing/jumping works through the inspector.

---

## Phase 5: Cleanup

**Goal:** Remove all remaining `embedded_*` terminology from source and docs.

| File | Change |
|------|--------|
| `src/runtime/graph.h` | Remove `NodeDef::EmbeddedOpState` and `NodeDef::embedded_ops` |
| `src/runtime/graph.cpp` | Remove `embedded_ops` JSON read/write |
| `src/operator_api/embedded_control_instance.h` | Rename file to `bound_control_instance.h`. Rename class `EmbeddedControlInstance` → `BoundControlInstance`. Update all includes and usages (wavetable_synth.cpp, test_embedded_control_instance.cpp → test_bound_control_instance.cpp). |
| `docs/EMBEDDED-OPERATOR-SLOTS.md` | Delete |
| `docs/EMBEDDED-OPERATOR-SLOTS-PLAN.md` | Delete |

### Checkpoint
`grep -rn "embedded_slot\|embedded_op\|VIVID_EMBEDDABLE\|VividEmbeddedSlot\|collect_embedded_slots\|EmbeddedControlInstance" src/` returns zero hits.

---

## Phase 6 (Optional): Canvas Binding Lines + Referenced-By

**Goal:** Visual binding lines on the graph canvas and "Referenced By" inspector section for bound nodes.

| File | Change |
|------|--------|
| `src/ui/graph_snapshot.h` | Add `NodeSnapshot::referenced_by` vector |
| `src/runtime/main.cpp` | Populate `referenced_by` during snapshot build (reverse lookup) |
| `src/ui/node_graph_draw.cpp` | Draw dashed binding lines host→target. Draw "Referenced By" section in target node inspector. |
| `src/ui/node_graph.h` | Add binding line hover/selection state |
| `src/ui/node_graph_input.cpp` | Handle click on binding lines |

---

## Sequencing

```
Phase 1 (types) → Phase 2 (graph) → Phase 3 (runtime, full swap) → Phase 4 (UI) → Phase 5 (cleanup)
                                                                                         ↓
                                                                                  Phase 6 (canvas, optional)
```

Phases 1–2 are small and safe (~2 files each). Phase 3 is the heavy lift (~20 files, single commit). Phase 4 is UI-only (~6 files). Phase 5 is deletion.

## Verification

```bash
# After each phase:
cmake --build build
cmake --build /Users/jeff/Developer/vivid-wavetable/build  # Phase 3+
ctest --test-dir build --output-on-failure

# After Phase 3:
ctest --test-dir build -R "role_binding" --output-on-failure
ctest --test-dir build -R "test_audio" --output-on-failure
ctest --test-dir build -R "test_graph" --output-on-failure

# After Phase 5:
grep -rn "embedded_slot\|embedded_op\|VIVID_EMBEDDABLE\|VividEmbeddedSlot\|collect_embedded_slots\|EmbeddedControlInstance" src/
# Expected: zero hits
```
