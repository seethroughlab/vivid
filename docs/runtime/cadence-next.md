# Cadence Runtime: Next Steps

This document describes four improvements to the cadence-aware runtime that would bring it closer to a from-scratch ideal. They are listed in rough priority order. Each is independent; none blocks another.

## 1. CadenceOverride::Auto inference from downstream connections

### Problem

`CadenceOverride::Auto` currently defaults to `Cadence::Frame` for all audio-capable operators (graph_compiler.cpp:384-387). The user must manually set `CadenceOverride::Audio` on every control operator they want promoted to audio rate. This is correct but verbose, and it means the graph doesn't communicate timing intent through its wiring.

The cadence report proposed that `Auto` should mean "the runtime infers the right cadence from downstream connections." If an audio-capable operator feeds an audio-cadence consumer, it should be promoted to audio rate automatically. If it only feeds frame-cadence consumers, it should stay at frame rate.

### Design

#### Inference rule

For each audio-capable node where `cadence_override == Auto`:

1. Walk its downstream edges (direct consumers in the `Graph::connections` list).
2. If any downstream node has `active_cadence == Audio`, set this node to `Audio`.
3. Otherwise, keep it at `Frame`.

This is a propagation problem: promoting one node may cause its upstream suppliers to promote too. The compiler should iterate to a fixed point or resolve in topological order (upstream-first), which the existing topo sort already provides.

#### Interaction with explicit overrides

- `CadenceOverride::Frame` always wins — the user explicitly pinned this node to frame rate.
- `CadenceOverride::Audio` always wins — the user explicitly pinned this node to audio rate.
- `CadenceOverride::Auto` defers to inference.
- `VIVID_CADENCE_FRAME_ONLY` operators are never promoted regardless of downstream demand.
- `VIVID_CADENCE_AUDIO_ONLY` operators are always audio regardless of override.

#### Compiler implementation

Today the compiler determines cadence in Pass 1 (node initialization, lines 372-388) before edges are built in Pass 2. Inference requires edges to exist first. Two approaches:

**Option A: Two-pass cadence resolution.** Build nodes with a provisional cadence of `Frame` for all `Auto` nodes (as today). Build edges. Then run a second cadence pass that walks `frame_to_audio_edges` looking for `Auto` source nodes feeding `Audio` destinations, promoting them and re-classifying affected edges. This may cascade: a promoted node's upstream `Auto` suppliers must be re-checked.

**Option B: Resolve cadence from the connection list before compilation.** Before entering `GraphCompiler::compile()`, scan `Graph::connections` and each node's `cadence_override` / `cadence_capability` to pre-compute the resolved cadence for every node. Pass the resolved map into the compiler so Pass 1 can use it directly. This avoids retrofitting the existing pass structure but requires duplicating some descriptor lookup logic.

Option A is more contained — it works within the existing compiler and only adds a fixup loop after Pass 2.

#### Mismatch surfacing

When inference promotes a node, this should be visible to the user rather than silent. Two levels:

1. **Passive:** The node's cadence badge in the inspector already shows the active cadence. If the override is `Auto` and the resolved cadence is `Audio`, the badge should show "Audio (auto)" or similar. This requires threading the distinction between "explicitly Audio" and "inferred Audio" into `NodeSnapshot`.

2. **Active (future):** The MCP analysis hints system could emit a hint when a frame-rate audio-capable node feeds an audio consumer: "consider promoting X to audio cadence for sample-accurate modulation." This doesn't require compiler changes — it can reason over the snapshot.

#### Cost visibility

Promoting a node from 60 Hz to 48 kHz is an ~800x increase in invocations. The inspector's cadence section should show a brief note when a node is running at audio rate, especially if inferred. No runtime budget system exists yet, but the `audio_load` metric on `AudioExecutor` already measures callback budget usage. If audio load exceeds a threshold after a topology change that promoted nodes, a warning could surface.

#### Stability rule

The cadence report was clear: a node's cadence should not silently change when a downstream consumer is disconnected. Inference should only promote (frame -> audio), never demote. If a user disconnects the last audio consumer from an inferred-Audio node, the node stays at Audio until the user explicitly sets it to `Auto` or `Frame`. This prevents the graph from silently changing timing behavior on rewire.

An alternative is to re-infer on every topology change and allow demotion, but flag it in the UI. This is simpler to implement (just re-run inference) but violates the stability principle from the report. The conservative choice is: **inference promotes, only explicit action demotes.**

Implementation note: this means the compiler needs to distinguish between "Auto, never inferred" and "Auto, previously inferred as Audio." One clean way: introduce a `CadenceOverride::InferredAudio` value (3) that the compiler writes back to the `NodeDef` after inference. The UI treats it like `Auto` for display but the compiler treats it as `Audio` on subsequent builds. The user can cycle through Auto -> Frame -> Audio as before; InferredAudio is never user-selectable, only compiler-assigned.

### Files involved

| File | Change |
|------|--------|
| `src/runtime/cadence_types.h` | Possibly add `InferredAudio` to `CadenceOverride` |
| `src/runtime/graph_compiler.cpp` | Add post-edge cadence inference loop |
| `src/runtime/graph.cpp` | Serialize `InferredAudio` (or treat it as `Auto` on save) |
| `src/ui/graph_snapshot.h` | Add `cadence_inferred` bool to `NodeSnapshot` |
| `src/ui/node_graph_draw.cpp` | Show "(auto)" suffix on inferred cadence badge |
| `src/cli/analysis_hints.cpp` | Optional: emit hint for promotable nodes |

---

## 2. Remove VividExecutionEnv as a separate concept

### Problem

`VividExecutionEnv` (`VIVID_ENV_FRAME` / `VIVID_ENV_AUDIO` / `VIVID_ENV_GPU`) is a holdover from the ABI v15 transition. In the current model, execution environment is fully determined by the capability flags:

```cpp
static inline VividExecutionEnv vivid_execution_env(const VividOperatorDescriptor* d) {
    if (d->has_process_gpu)                              return VIVID_ENV_GPU;
    if (d->has_process_audio && !d->has_process_frame)   return VIVID_ENV_AUDIO;
    return VIVID_ENV_FRAME;
}
```

This derivation function (types.h:132-136) already prevents stale state, but the enum itself conflates two orthogonal concepts:
- **Cadence** (Frame vs Audio) — the execution rate
- **GPU** — whether the operator submits GPU commands

A from-scratch design would use `CadenceCapability` for the first and `has_process_gpu` for the second. `VividExecutionEnv` exists because the old `VividDomain` was replaced in two steps: first by `VividExecutionEnv` (v15), then the stored field was removed (v18), leaving only the derivation function.

### Current usage

`VividExecutionEnv` appears in three subsystems, none of which are runtime-critical:

1. **Operator creation** (operator_creator.cpp): Selects directory (`audio/`, `control/`, `gpu/`), C++ template, and CMake insertion point based on `env`. Used in `env_subdir()`, `cmake_insertion_marker()`, and the template switch.

2. **Control server** (control_server.cpp): Maps env to string labels (`"control"`, `"audio"`, `"gpu"`) for the introspection protocol. Parses env from the `create_operator` command.

3. **UI create dialog** (node_graph_input.cpp): Stores a `create_env_sel_` index (0-2) that maps to `VividExecutionEnv`.

### Design

Replace `VividExecutionEnv` with a simpler vocabulary that matches the actual decision being made: **which directory/template/label does this operator belong to?**

#### Option A: Keep the enum, rename to OperatorKind

Rename `VividExecutionEnv` to `VividOperatorKind` (or `VividOperatorCategory`) with values `VIVID_OP_CONTROL`, `VIVID_OP_AUDIO`, `VIVID_OP_GPU`. Keep the derivation function. This is the smallest change — it's a rename, not a removal.

Pros: Minimal diff, no functional change, communicates that this is a *classification* not a *runtime concept*.

Cons: Still a separate concept from `CadenceCapability`. Still three enum values when the actual axes are (cadence: frame/audio) x (gpu: yes/no).

#### Option B: Eliminate the enum, use capability flags directly

Each usage site already has access to the descriptor. Replace:
- `env_subdir(env)` with a function that takes `(has_process_gpu, cadence_capability)` and returns the subdirectory
- `env_str(env)` with inline logic: `is_gpu ? "gpu" : (cap == AUDIO_ONLY ? "audio" : "control")`
- The UI's `create_env_sel_` with a struct `{ bool gpu; bool audio; }` or a 3-value enum local to the UI

Pros: Removes a type from the operator ABI header. Makes the two axes explicit.

Cons: Slightly more code at each call site. The operator creation flow and control server protocol still need a 3-way distinction (control/audio/gpu), so the logic doesn't actually simplify — it just moves from an enum to inline conditionals.

#### Recommendation

Option A (rename to `VividOperatorKind`) is the pragmatic choice. It's the smallest change, it removes the misleading "execution environment" naming, and it doesn't pretend that the 3-way classification will go away. The directory structure is `control/` / `audio/` / `gpu/` and will stay that way.

If a broader operator creation refactor happens (e.g., supporting audio-capable control operators that live in `control/` but have `process_audio`), revisit at that time.

### Files involved

| File | Change |
|------|--------|
| `src/operator_api/types.h` | Rename enum + defines + derivation function |
| `src/runtime/operator_creator.cpp` | Update all references |
| `src/runtime/control_server.cpp` | Update `env_str()` and parsing |
| `src/ui/node_graph_input.cpp` | Update create dialog |
| `src/runtime/main.cpp` | Update any remaining references |

---

## 3. Edge-centric snapshot indexing

### Problem

The current snapshot system is indexed by audio node — `ParamSnapshot` and `AnalysisSnapshot` both use `[audio_node_idx]` as their primary dimension, where `audio_node_idx` is a dense index into the set of audio-cadence nodes.

This means:
- `CadenceBridge::build()` allocates per-node arrays (params, float inputs, spreads, strings, custom ports) for every audio node, even if that node has no cross-cadence edges.
- `push_to_audio()` iterates `frame_to_audio_edges` but writes into per-node slots via `node_to_snapshot_idx_`.
- `pull_from_audio()` iterates `audio_to_frame_edges` and `analysis_mappings_` to read from per-node slots.

The per-node indexing is simple and correct, but it has two inefficiencies:

1. **Wasted allocation.** An audio-only subgraph with no frame-to-audio edges still gets full `ParamSnapshot` arrays. The bridge allocates `node_params[i]`, `float_input_values[i]`, `spread_inputs[i]`, `input_string_values[i]`, and `custom_inputs[i]` for every audio node regardless of whether any snapshot edge targets it.

2. **Indirect access pattern.** `push_to_audio()` iterates edges but must indirect through `node_to_snapshot_idx_` to find the write slot. An edge-centric layout would iterate edges and write directly to edge-indexed slots, which is more cache-friendly when the edge count is much smaller than the node count times port count.

### Design

#### Core idea

Replace per-node snapshot arrays with per-edge snapshot slots. Each cross-cadence edge owns exactly one slot in a flat array. The bridge iterates edges directly, reading from the source `CompiledNode` and writing to the slot. The audio executor reads from slots directly, indexed by a precomputed edge-to-slot mapping.

#### ParamSnapshot becomes EdgeSnapshot

```cpp
struct EdgeSnapshotSlot {
    uint32_t from_node;       // graph node index (frame side)
    uint32_t to_node;         // graph node index (audio side)
    uint32_t from_port;       // source port index
    uint32_t to_port;         // dest port index (or param index)
    bool targets_param;       // true if dest is a param, not an input port
    // Data (union or variant depending on port type)
    float float_value;
    SpreadSnapshot spread;
    std::string string_value;
    CustomPortSnapshot custom;
};

struct FrameToAudioSnapshot {
    std::vector<EdgeSnapshotSlot> slots;  // one per frame_to_audio_edge
    // Per-audio-node param arrays still needed for non-wired params
    std::vector<std::vector<float>> node_params;  // [audio_node_idx][param_idx]
};
```

The per-node `node_params` array can't be fully eliminated because audio nodes need their base parameter values even when no frame->audio edge targets them. But the input-side arrays (`float_input_values`, `spread_inputs`, etc.) can move entirely to edge slots.

#### AudioExecutor consumption

Currently the audio executor copies snapshot data per-node at the start of each buffer (audio_executor.cpp:207-232). With edge-centric indexing, it would iterate edge slots instead:

```cpp
for (const auto& slot : snap.slots) {
    auto& cn = cg.nodes[slot.to_node];
    if (slot.targets_param) {
        cn.param_values[slot.to_port] = slot.float_value * slot.scale;
    } else {
        cn.audio->float_input_values[slot.to_signal_ordinal] = slot.float_value;
    }
}
```

This is simpler and avoids the bulk copy of entire per-node arrays when only a few ports are wired.

#### AnalysisSnapshot stays per-node

The audio->frame direction is different. RMS, peak, and waveform are per-node properties computed unconditionally by the audio executor for all audio nodes (for the waveform display and meter). These don't correspond to edges — they're intrinsic to every audio node. So `AnalysisSnapshot` should remain per-node indexed.

Float and spread outputs that cross back to frame via `audio_to_frame_edges` could use edge-centric slots, but the audio executor already computes `float_output_values` per-node regardless. The only thing `pull_from_audio()` does with edges is route those values to frame-side `CompiledNode`s. This routing step is already edge-indexed (it iterates `audio_to_frame_edges`). Moving the *storage* to edge slots would save nothing because the audio executor needs the per-node arrays anyway.

#### Migration path

1. Add `EdgeSnapshotSlot` struct alongside existing `ParamSnapshot`.
2. In `push_to_audio()`, populate edge slots instead of per-node arrays (keeping per-node params).
3. In the audio executor, consume edge slots instead of per-node input arrays.
4. Remove unused per-node arrays from `ParamSnapshot` (`float_input_values`, `spread_inputs`, `input_string_values`, `custom_inputs`).
5. Keep `AnalysisSnapshot` unchanged.

### When to do this

This is a performance optimization that matters when:
- Audio node count is high (>10) with sparse cross-cadence wiring
- The per-node allocation pattern causes memory pressure or cache misses

For typical Vivid graphs (2-5 audio nodes), the current approach is fine. Defer until profiling shows snapshot overhead matters, or until the node count grows significantly.

### Files involved

| File | Change |
|------|--------|
| `src/runtime/snapshot_types.h` | Add `EdgeSnapshotSlot`, slim `ParamSnapshot` |
| `src/runtime/cadence_bridge.h` | Update bridge to use edge slots |
| `src/runtime/cadence_bridge.cpp` | Rewrite `push_to_audio()`, update `build()` |
| `src/runtime/audio_executor.cpp` | Consume edge slots instead of per-node arrays |

---

## 4. Declarative analysis ports

### Problem

Audio-cadence nodes get three implicit output ports — `rms`, `peak`, `waveform` — injected by the graph compiler at init time (graph_compiler.cpp:82-90):

```cpp
if (cn.active_cadence == Cadence::Audio) {
    cn.audio->analysis_output_port_indices["rms"]      = cn.output_port_count++;
    cn.audio->analysis_output_port_indices["peak"]     = cn.output_port_count++;
    cn.audio->analysis_output_port_indices["waveform"] = cn.output_port_count++;
}
```

These ports are invisible to the operator descriptor — they don't appear in `VividOperatorDescriptor::ports`. The compiler, bridge, and audio executor all contain special-case logic to handle them:

- **graph_compiler.cpp:82-90** — injects ports, bumps port count
- **cadence_bridge.cpp:62-80** — builds `AnalysisMapping` structs by looking up port names in the map
- **cadence_bridge.cpp:303-317** (`pull_from_audio()`) — copies RMS/peak/waveform from `AnalysisSnapshot` into these ports
- **audio_executor.cpp:630-655** — computes RMS/peak, fills waveform ring buffer, writes to analysis snapshot

The special-case code works, but it means:
- Tooling (MCP, analysis hints) can't discover analysis ports from the descriptor alone
- Adding a new analysis output (e.g., spectral centroid, zero-crossing rate) requires touching the compiler, bridge, and executor
- The port count depends on runtime cadence, not just the descriptor

### Design

#### Declare analysis ports in the operator descriptor

Add analysis ports to the `VividPortDescriptor` array returned by `collect_ports()`. Use a new port type or semantic tag to distinguish them from user-defined ports.

**Option A: Semantic tag.** Use the existing `semantic_tag` field on `VividPortDescriptor`:

```cpp
out.push_back({"rms",      VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
out.push_back({"peak",     VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
out.push_back({"waveform", VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SPREAD, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
```

Pros: No ABI change. Uses existing infrastructure. Tooling can discover ports.
Cons: Every audio operator must declare these three ports in `collect_ports()`. Boilerplate.

**Option B: Trait mixin.** Provide a `HasAnalysisPorts` mixin (or a helper function) that appends the standard analysis ports:

```cpp
struct HasAnalysisPorts {
    static void append_analysis_ports(std::vector<VividPortDescriptor>& out) {
        out.push_back({"rms",      VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT, ...});
        out.push_back({"peak",     VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT, ...});
        out.push_back({"waveform", VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT, ...});
    }
};
```

Operators that want analysis ports call `HasAnalysisPorts::append_analysis_ports(out)` at the end of `collect_ports()`. The compiler no longer needs to inject ports — they're already in the descriptor.

Pros: Opt-in, explicit, discoverable, no per-operator boilerplate beyond one function call.
Cons: Operators that forget the call won't get analysis ports. (But that could be a feature — not every audio node needs a waveform display.)

**Option C: Auto-append in VIVID_REGISTER.** The `VIVID_REGISTER` macro already detects `AudioProcessable`. It could auto-append analysis ports to the descriptor's port list for any operator with `has_process_audio`:

```cpp
// In VIVID_REGISTER expansion:
if constexpr (std::is_base_of_v<vivid::AudioProcessable, ClassName>) {
    // Append analysis ports to desc.ports
}
```

Pros: Zero boilerplate. Every audio operator automatically gets analysis ports. Matches current behavior.
Cons: Requires the macro to manage a mutable port array (currently ports are collected via `collect_ports()` and stored as a pointer + count). This would need the registration to own the storage. Also, operators that don't want analysis ports can't opt out.

#### Recommendation

Option B (trait mixin) is the cleanest. It's explicit, discoverable, and doesn't require ABI changes. Operators opt in with a single function call. The compiler can verify at build time that audio operators have analysis ports and warn if they don't.

#### Removing compiler injection

Once analysis ports are in the descriptor:

1. Remove the injection block in `graph_compiler.cpp:82-90`.
2. The `analysis_output_port_indices` map in `AudioNodeState` can be populated from the descriptor's port list by scanning for ports with `semantic_tag == "analysis"`.
3. `CadenceBridge::build()` no longer needs special analysis mapping construction — it finds analysis ports the same way it finds any other port, by name.
4. The audio executor's RMS/peak/waveform computation remains unchanged — it writes to the same port indices, just discovered differently.

#### Extensibility

With declarative analysis ports, adding a new analysis output (e.g., spectral centroid) becomes:

1. Add a new port to the `HasAnalysisPorts` mixin.
2. Compute the value in the audio executor.
3. Done — the bridge, compiler, and UI discover it automatically.

No compiler or bridge changes needed.

### When to do this

This is worth doing when:
- A new analysis output is needed (spectral centroid, zero-crossing rate, loudness)
- The MCP/analysis system needs to enumerate available analysis ports from descriptors
- An operator wants to opt out of analysis ports (e.g., a lightweight utility audio node)

### Files involved

| File | Change |
|------|--------|
| `src/operator_api/operator.h` | Add `HasAnalysisPorts` mixin or helper |
| `operators/audio/*/` | Add `append_analysis_ports()` call to each audio operator |
| `src/runtime/graph_compiler.cpp` | Remove injection block, discover ports from descriptor |
| `src/runtime/cadence_bridge.cpp` | Build analysis mappings from port semantic tags |
| `src/runtime/audio_executor.cpp` | No change (writes to same port indices) |
| `src/runtime/compiled_graph.h` | Possibly remove `analysis_output_port_indices` map |
