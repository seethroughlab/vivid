# RuntimeAPI — High-Level C++ Interface

## Overview

`RuntimeAPI` (runtime_api.h/cpp) is the central command layer used by `ControlServer` to drive
all runtime mutations. It owns references to `Graph`, `RuntimeCore`, `AudioEngine`, and
`OperatorRegistry`, and provides a single `CommandResult`-returning API over all of them.

It also owns cross-cutting state that doesn't belong in any single subsystem:
undo/redo, graph metronome state, quantized variation switching, state-preset machine,
crossfade state, active presets.

## Construction

```cpp
RuntimeAPI(Graph& graph, RuntimeCore& runtime, AudioEngine& audio_engine,
           OperatorRegistry& registry, SystemMidiListener* system_midi = nullptr);
```
All parameters are references — `RuntimeAPI` does not own any of them.

## `CommandResult`

```cpp
struct CommandResult {
    bool ok;
    std::string message;  // JSON string on success, error text on failure
};
```

`inspect(node_id)` is the human-readable live-state query. For audio-buffer ports it now reports a
compact audio summary (`audio[ch=… peak=… frames=… active|idle]`) instead of a misleading scalar
snapshot, while non-audio ports continue to print their existing scalar/string/lane values. Audio
nodes also append an `audio_debug:` line with the latest per-node callback timing and retained
lane-state counts.

## No Compiled Graph State

Some control-server requests can arrive while `RuntimeCore` has no compiled graph, such as during
startup, reload, or after a failed rebuild. Live-state commands that require compiled nodes fail
cleanly with `"no compiled graph"` instead of dereferencing runtime state. Graph-only commands may
still update the authored `Graph`, and per-frame helpers such as quantized variation and
state-preset ticks no-op until compiled state is available again.

## Immediate vs. Buffered

**Immediate** (apply to live runtime in-place, no rebuild needed):
- `set_param()`, `set_string_param()` — update `NodeState::param_values` directly
- `set_node_layout()` — update `NodeDef::layout_x/y`
- `set_resolution()` — update `NodeDef::tex_width/height`, sets `needs_gpu_realloc_`

**Buffered topology** (sets `pending_topology_change_ = true`):
- `add_node()`, `remove_node()`, `connect()`, `disconnect()`
- Applied by `apply_pending()` which calls `RuntimeCore::build()` + `AudioEngine::build()`

`add_node()` now routes on-demand operator preparation through `OperatorPreparationService`
before mutating the graph. External behavior stays synchronous, but it no longer performs its own
ad hoc lazy dylib load.

```cpp
bool apply_pending(bool& has_gpu_ops, bool& has_audio);
bool has_pending() const { return pending_topology_change_; }
```

## Parameter Management

```cpp
CommandResult set_param(node_id, param, value);
CommandResult set_string_param(node_id, param, value);
CommandResult get_param(node_id, param);
CommandResult set_param_lock(node_id, param, flags);  // PARAM_LOCK_* flags
CommandResult get_param_lock(node_id, param);
```

File/path string params are handled specially by `set_file_param_internal()`:
- Absolute path stored in `NodeState::file_param_storage` (for runtime use)
- Relative path stored in `NodeDef::string_params` (for serialization, relative to graph file)

## Topology

```cpp
CommandResult add_node(type, id);
CommandResult remove_node(id);
CommandResult connect(from_addr, to_addr, semantic_defaults = false);
CommandResult disconnect(from_addr, to_addr);
CommandResult set_connection_remap(from_addr, to_addr, from_min, from_max, to_min, to_max, clamp);
```

Address format: `"node_id/port_name"`. Parsed by `split_addr()`.

`semantic_defaults`: if true, applies semantic-tag-based default remap when connecting ports
with matching semantic tags (e.g. frequency range remapping).

## Persistence

```cpp
CommandResult save();                             // save to source_path
CommandResult save_as(const std::string& path);
CommandResult reload(bool& has_gpu_ops, bool& has_audio);
CommandResult new_graph(bool& has_gpu_ops, bool& has_audio);
CommandResult new_project(const std::string& dir_path, ...);
CommandResult apply_snapshot_json(const std::string& graph_json, ...);
```

`graph_dirty_` is tracked by comparing the live graph JSON to `last_saved_graph_json_`.
`capture_saved_snapshot()` stores the current serialized form; `refresh_graph_dirty_from_saved_snapshot()`
recomputes the dirty flag without modifying anything.

For runtime-owned topology commits that happen outside the normal buffered mutation path
(for example, prepared-build adoption in the graph UI), `RuntimeAPI` exposes:

```cpp
void notify_external_graph_mutation();
void finalize_external_graph_load();
```

This clears pending topology/crossfade state, bumps `reload_serial_`, preserves undo history
for the caller, and refreshes the dirty flag against the last saved graph snapshot.

`finalize_external_graph_load()` is the graph-load counterpart used by the async UI open/reload
path after a prepared graph build has been adopted. It updates the active graph identity from the
committed `Graph::source_path()`, clears pending topology state, bumps `reload_serial_`, and
captures a fresh saved snapshot so graph-switches remain transactional from the UI's perspective.

For same-graph async reloads, `RuntimeAPI` also exposes a small preserved-runtime-state helper:

```cpp
RuntimeAPI::PreservedRuntimeState capture_preserved_runtime_state_for_path(path) const;
void apply_preserved_runtime_state(const PreservedRuntimeState& state);
```

This preserves the existing synchronous reload contract: only reloads that target the active graph
identity restore live float params, string/file params, and param-lock flags after a rebuild.

Recent hardening guarantees in this area:

- `save_as()` retargets the live graph/source identity instead of only writing bytes
- `reload()` is restore-on-failure: it snapshots the current graph first and rolls back if load or rebuild fails
- `apply_snapshot_json()` is transactional for graph/runtime rebuild purposes and preserves source-path identity
- package/runtime refresh flows that rebuild the graph route through the same transactional restore logic

## Variations

```cpp
CommandResult save_variation(name);      // snapshot all params from live runtime
CommandResult recall_variation(name);    // apply variation params to runtime nodes
CommandResult recall_variation_idx(idx);
CommandResult update_variation(name);    // overwrite with current params
CommandResult queue_variation(name, quantize);  // schedule for next beat/bar
CommandResult set_graph_metronome(bpm, beats_per_bar);
CommandResult set_quantize_clock(node_id);  // deprecated compatibility shim
GraphMetronomeSample current_metronome_sample() const;
void tick_quantized_switch();            // call each frame to fire pending switches
```

Quantize modes: `"instant"`, `"beat"`, `"bar"`, `"4bar"` (`"four_bar"` is still accepted as a
legacy alias).

Quantized switching is now graph-metronome-backed:

- beat/bar/4-bar boundaries are derived from the graph metronome's `bpm` and `beats_per_bar`
- `tick_quantized_switch()` compares the current metronome beat count against the queued target
  boundary instead of watching a hidden designated clock node

`set_quantize_clock()` remains for backward compatibility with older graphs and tooling, but the
runtime no longer uses that hidden clock-node reference to schedule variation recalls.

## Graph Metronome

```cpp
CommandResult set_graph_metronome(float bpm, int beats_per_bar);
GraphMetronomeSample current_metronome_sample() const;
```

The graph metronome is optional shared transport metadata stored on `Graph`. It is intentionally
not a timeline and not a special operator. Its job is to provide a common pulse for:

- quantized variation switching
- clocks that opt into metronome sync mode
- transport UI/status surfaces

Clocks remain first-class local temporal contexts. A clock can still free-run with its own BPM, or
it can sync to the graph metronome and derive its phase from shared BPM + meter while exposing note
division controls instead of a local tempo knob.

The same contract now extends to other time-based operators:

- internal-rate operators use `rate_mode = free | external | metronome`
- beat-driven operators use `clock_source = external | metronome`
- metronome-synced note lengths reuse the clock operator's shared musical division vocabulary
- the current metronome snapshot is exposed to frame, audio, and GPU operators through their
  runtime contexts, so custom operators in any domain can read shared transport state

This keeps explicit `beat_phase` wiring fully supported while making graph-metronome sync a
first-class, discoverable option in operator inspectors.

The editable `Graph` still stores the persisted metronome metadata, but live execution now reads
from a runtime-owned transport state in `RuntimeCore`:

- `set_graph_metronome()` updates the persisted graph metadata and the live runtime transport
  immediately
- BPM changes are phase-continuous: the current beat position is preserved and the new tempo takes
  effect from that instant forward
- meter changes restart the bar immediately at beat 0 / bar 0

That live transport state is what frame, audio, and GPU operators now receive in their runtime
contexts, so metronome-aware operators retime without waiting for `apply_pending()` or a graph
rebuild.

## Per-Operator Presets

```cpp
CommandResult save_preset(node_id, name);
CommandResult recall_preset(node_id, name);
CommandResult update_preset(node_id, name);
CommandResult remove_preset(node_id, name);
CommandResult rename_preset(node_id, old_name, new_name);
CommandResult list_presets(node_id);
CommandResult list_factory_presets(node_id);
const std::string& active_preset(node_id) const;
```

Presets are stored in `Graph::node_presets_`. Recall applies param values to the live `NodeState`,
respecting `PARAM_LOCK_PRESETS` flags.

## State-Preset Mapping

```cpp
CommandResult set_state_preset(sm_node, state_idx, target_node, preset_name);
CommandResult remove_state_preset(sm_node, state_idx, target_node);
CommandResult clear_state_presets(sm_node);
CommandResult inspect_state_presets(sm_node);
void tick_state_presets();   // call each frame to detect state machine transitions
```

`tick_state_presets()` reads the StateMachine operator's output port each frame.
When a transition fires, it calls `recall_preset()` for each target node in the mapping.
Active crossfades (`active_crossfades_`) interpolate param values over multiple frames.

```cpp
CommandResult ensure_state_mapping(sm_node);
CommandResult queue_state_transition(sm_node, state_idx, quantize);
int           queued_state_for(sm_node);  // -1 if none pending
void          tick_quantized_state_transitions();  // call each frame alongside tick_quantized_switch
```

`ensure_state_mapping()` registers `sm_node` in `state_preset_mappings_` with an empty entry
so the snapshot builder includes it in `GraphSnapshot::clip_machines` (Session view clip grid).
Idempotent — safe to call if a mapping already exists.

`queue_state_transition()` queues a jump to `state_idx` on the named StateMachine. If
`quantize == "instant"` it calls `set_param(sm_node, "force_state", state_idx)` immediately;
otherwise it enqueues a `PendingStateTransition` to fire at the next beat/bar/4-bar boundary.
The StateMachine detects the `force_state` param change and jumps, triggering preset recalls.

## Solo Mode

```cpp
CommandResult set_solo(const std::string& node_id);  // empty = clear
std::string solo_node_id() const;
```

Delegates to `RuntimeCore::set_solo()`. Session-only, not serialized.

## MIDI

```cpp
CommandResult add_midi_mapping(node_id, param, cc, channel, range_min, range_max);
CommandResult remove_midi_mapping(node_id, param);
CommandResult update_midi_mapping(node_id, param, range_min, range_max);
void apply_midi_mappings();   // call each frame to apply live CC values
```

`apply_midi_mappings()` reads from `SystemMidiListener` (if set) and calls `set_param()`
for each registered mapping.

## Reload Serial

```cpp
uint64_t reload_serial() const;
```
Incremented each time the graph is rebuilt (topology change or reload). Used by callers to
detect that their cached node indices are stale.
