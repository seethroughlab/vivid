# Graph — Serializable Scene Description

## Overview

`Graph` (graph.h/cpp) is a pure data model — it does **not** hold operator instances or execute anything.
It is the serialized representation of the scene: what nodes exist, how they connect, and their stored parameter values.

The `RuntimeCore` and `AudioEngine` are built **from** a `Graph` at load time, and written back to a `Graph`
on save. The `Graph` is the source of truth for persistence.

## Core Structs

### `NodeDef`
```cpp
struct NodeDef {
    std::string id;          // unique node ID (user-visible, e.g. "lfo1")
    std::string type;        // operator type name (e.g. "lfo", "shape")
    std::unordered_map<std::string, float> params;
    std::unordered_map<std::string, std::string> string_params;
    float layout_x, layout_y;       // NaN = auto-layout
    uint32_t tex_width, tex_height;  // GPU resolution (0 = inherit default 800x600)
    std::unordered_map<std::string, uint8_t> param_lock_flags; // sparse
    std::string pkg_name, pkg_version; // provenance (empty for core operators)
};
```

### `ConnectionDef`
```cpp
struct ConnectionDef {
    std::string from_node, from_port;
    std::string to_node, to_port;
    float from_min, from_max;   // remap range on source side (default 0..1)
    float to_min, to_max;       // remap range on destination side (default 0..1)
    bool clamp;
};
```
`has_remap()` returns true if any remap fields differ from their defaults.

### `MidiMappingDef`
Maps a MIDI CC to a node parameter: `node_id`, `param_name`, `cc_number` (0-127), `channel` (0=omni), `range_min/max`.

### `VariationDef`
Named parameter snapshot for all nodes: `name` + `params[node_id][param_name] = value`.
`active_variation_` (-1 = none) tracks which variation is active.

### `OperatorPreset`
Per-operator named preset: `name` + `params[param_name] = value` + `string_params`.
Stored in `node_presets_[node_id]`.

### `StatePresetMapping`
Maps StateMachine operator state transitions to preset recalls.
`state_machine_node` + `state_presets[state_idx][target_node_id] = preset_name`.

## `Graph` Class API

### Load / Save
```cpp
bool load(const char* path);                          // parse JSON file
bool load_from_string(const char* json, size_t len);  // parse in-memory JSON
bool save(const char* path) const;
bool save_to_string(std::string& out_json) const;
```
Uses nlohmann/json for JSON parsing. Schema version checked against `GRAPH_SCHEMA_VERSION` (currently 3).
`load_diagnostics` is populated with package version mismatches after load.

### Mutation
```cpp
bool add_node(id, type, params, string_params);
bool remove_node(id);
bool add_connection(from_node, from_port, to_node, to_port);
bool remove_connection(...);
bool set_connection_remap(..., from_min, from_max, to_min, to_max, clamp);
```
All mutations return false on error (e.g. duplicate ID, missing node).

### Lookup
```cpp
const NodeDef* find_node(const std::string& id) const;
NodeDef*       find_node(const std::string& id);       // mutable
```

### Viewport
```cpp
float viewport_pan_x, viewport_pan_y, viewport_zoom;  // NaN = not set
bool has_viewport() const;
void set_viewport(float px, float py, float z);
```
Viewport is UI-only state, persisted but ignored by the runtime.

## Schema Version

`GRAPH_SCHEMA_VERSION` is defined as `3` in graph.h. Graphs saved with a newer schema version
are hard-rejected on load. Graphs without a `schema_version` field are treated as version 1.

## Load Diagnostics

After `load()`, `graph.load_diagnostics` contains entries for nodes whose saved `pkg_version`
differs from the installed version. Each entry has: `node_id`, `pkg_name`, `saved_version`,
`installed_version`, `classification` (`"compatible_update"` or `"incompatible_update"`).

## JSON Format (key fields)

```json
{
  "schema_version": 3,
  "vivid_version": "1.2.3",
  "nodes": [
    { "id": "lfo1", "type": "lfo", "params": {"freq": 1.5}, "x": 100, "y": 200 }
  ],
  "connections": [
    { "from": "lfo1/value", "to": "shape1/rotation" }
  ],
  "midi_mappings": [...],
  "variations": [...],
  "active_variation": -1,
  "node_presets": { "lfo1": [{ "name": "slow", "params": {"freq": 0.1} }] }
}
```

Shader-backed operators are persisted exactly like any other operator: a node stores the concrete
operator `type` (for example `"Blur"`), and the shader source lives in a real `.wgsl` file under
core `filters/`, a package `filters/`, or the project-local `<graph_dir>/filters/` directory.
