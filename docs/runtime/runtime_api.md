# RuntimeAPI — High-Level C++ Interface

## Overview

`RuntimeAPI` (runtime_api.h/cpp) is the central command layer used by `ControlServer` to drive
all runtime mutations. It owns references to `Graph`, `Scheduler`, `AudioEngine`, and
`OperatorRegistry`, and provides a single `CommandResult`-returning API over all of them.

It also owns cross-cutting state that doesn't belong in any single subsystem:
undo/redo, quantized variation switching, state-preset machine, crossfade state, active presets.

## Construction

```cpp
RuntimeAPI(Graph& graph, Scheduler& scheduler, AudioEngine& audio_engine,
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

## Immediate vs. Buffered

**Immediate** (apply to live scheduler in-place, no rebuild needed):
- `set_param()`, `set_string_param()` — update `NodeState::param_values` directly
- `set_node_layout()` — update `NodeDef::layout_x/y`
- `set_resolution()` — update `NodeDef::tex_width/height`, sets `needs_gpu_realloc_`

**Buffered topology** (sets `pending_topology_change_ = true`):
- `add_node()`, `remove_node()`, `connect()`, `disconnect()`
- Applied by `apply_pending()` which calls `Scheduler::build()` + `AudioEngine::build()`

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

Recent hardening guarantees in this area:

- `save_as()` retargets the live graph/source identity instead of only writing bytes
- `reload()` is restore-on-failure: it snapshots the current graph first and rolls back if load or rebuild fails
- `apply_snapshot_json()` is transactional for graph/runtime rebuild purposes and preserves source-path identity
- package/runtime refresh flows that rebuild the graph route through the same transactional restore logic

## Variations

```cpp
CommandResult save_variation(name);      // snapshot all params from live scheduler
CommandResult recall_variation(name);    // apply variation params to scheduler nodes
CommandResult recall_variation_idx(idx);
CommandResult update_variation(name);    // overwrite with current params
CommandResult queue_variation(name, quantize);  // schedule for next beat/bar
CommandResult set_quantize_clock(node_id);
void tick_quantized_switch();            // call each frame to fire pending switches
```

Quantize modes: `"instant"`, `"beat"`, `"bar"`, `"four_bar"`.
`PendingVariation::beats_remaining` counts down each frame using `prev_beat_phase_` to detect beat crossings.

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

## Solo Mode

```cpp
CommandResult set_solo(const std::string& node_id);  // empty = clear
std::string solo_node_id() const;
```

Delegates to `Scheduler::set_solo()`. Session-only, not serialized.

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
